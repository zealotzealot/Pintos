#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "filesys/filesys.h"
#include "userprog/process.h"



struct file_desc * get_file_desc(int);
static void syscall_handler (struct intr_frame *);
void halt(void);
pid_t exec(const char *);
int wait(pid_t);
bool create(const char *, unsigned);
bool remove(const char *);
int open(const char *);
int filesize(int);
int read(int, void *, unsigned);
int write(int, const void *, unsigned);
void seek(int, unsigned);
unsigned tell(int);
void close(int);


int file_desc_idx=2;



//Read a byte at user virtual address UADDR
static int
get_user (const uint8_t *uaddr)
{
  if(uaddr >= PHYS_BASE)
    return -1;

  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}

//Writes Byte to user address UDST
static bool
put_user(uint8_t *udst, uint8_t byte)
{
  if(udst >= PHYS_BASE)
    return false;

  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


struct file_desc * get_file_desc(int fd) {
  if (fd<2 || fd>=file_desc_idx)
    exit(-1);

  struct process_sema *process = pid_to_process_sema(thread_current()->tid);
  struct list *target_list = &(process->file_desc_list);
  struct list_elem *e;
  struct file_desc *target;

  for (e = list_begin(target_list);
       e != list_end(target_list);
       e = list_next(e)) {
    target = list_entry(e, struct file_desc, elem);
    if (target->fd = fd) {
      return target;
    }
  }

  // Matching fd not found
}


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  switch(*(int *)(f->esp)) {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      //printf("exit\n");
      exit(*((int *)(f->esp)+1));
      break;
    case SYS_EXEC:
      //printf("exec\n");
      f->eax = exec(*((int *)(f->esp)+1));
      break;
    case SYS_WAIT:
      //printf("wait\n");
      f->eax = wait(*((int *)(f->esp)+1));
      break;
    case SYS_CREATE:
      f->eax = create(*((int *)(f->esp)+1),
               *((int *)(f->esp)+2));
      break;
    case SYS_REMOVE:
      f->eax = remove(*((int *)(f->esp)+1));
      break;
    case SYS_OPEN:
      f->eax = open(*((int *)(f->esp)+1));
      break;
    case SYS_FILESIZE:
      f->eax = filesize(*((int *)(f->esp)+1));
      break;
    case SYS_READ:
      f->eax = read(*((int *)(f->esp)+1),
                    *((int *)(f->esp)+2),
                    *((int *)(f->esp)+3));
      break;
    case SYS_WRITE:
      f->eax = write(*((int *)(f->esp)+1),
               *((int *)(f->esp)+2),
               *((int *)(f->esp)+3));
      break;
    case SYS_SEEK:
      seek(*((int *)(f->esp)+1),
           *((int *)(f->esp)+2));
      break;
    case SYS_TELL:
      f->eax = tell(*((int *)(f->esp)+1));
      break;
    case SYS_CLOSE:
      close(*((int *)(f->esp)+1));
      break;
  }
}



void halt (void) {
  power_off();
}



void exit (int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  set_exit_status (status);
  thread_exit();
}

pid_t exec (const char *cmd_line) {
  int pid = process_execute (cmd_line);
  return pid;
}



int wait (pid_t pid) {
  return process_wait (pid);
}



bool create (const char *file, unsigned initial_size) {
  if (file == NULL)
    exit(-1);

  return filesys_create(file, initial_size);
}



bool remove (const char *file) {
  return filesys_remove(file);
}



int open (const char *file) {
  if (file == NULL)
    exit(-1);

  struct file *res_file = filesys_open(file);
  if (res_file == NULL)
    return -1;

  struct file_desc *target = malloc(sizeof(struct file_desc));
  target->file = res_file;
  target->fd = file_desc_idx;
  strlcpy(target->name, file, strlen(file)+1);

  struct process_sema *process = pid_to_process_sema(thread_current()->tid);
  list_push_back(&(process->file_desc_list),
                 &(target->elem));

  return file_desc_idx++;
}



int filesize (int fd) {
  struct file_desc *target = get_file_desc(fd);

  return file_length(target->file);
}



int read (int fd, void *buffer, unsigned size) {
  if (fd == 0) {
    return input_getc();
  }

  struct file_desc *target = get_file_desc(fd);

  return file_read(target->file, buffer, size);
}



int write (int fd, const void *buffer, unsigned size) {
  // Write to console when fd == 1
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }

  struct file_desc *target = get_file_desc(fd);

  if (strcmp(target->name, thread_current()->name) == 0)
    return 0;

  return file_write(target->file, buffer, size);
}



void seek (int fd, unsigned position) {
  struct file_desc *target = get_file_desc(fd);

  file_seek(target->file, position);
}



unsigned tell (int fd) {
  struct file_desc *target = get_file_desc(fd);

  return file_tell(target->file);
}



void close (int fd) {
  if (fd<2 || fd>=file_desc_idx)
    exit(-1);

  struct file_desc *target = get_file_desc(fd);

  file_close(target->file);
  list_remove(&(target->elem));
  free(target);
}
