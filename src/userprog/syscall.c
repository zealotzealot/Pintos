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
struct lock fork_lock;
struct lock file_lock;



//Read a byte at user virtual address UADDR
static int
get_user_one_byte (const uint8_t *uaddr)
{
  if(uaddr >= PHYS_BASE)
    return -1;

  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}
  
static int
get_user (const uint8_t *uaddr)
{
  int total_result=0, result, i;
  for(i=0; i<4; i++){
    result = get_user_one_byte(((void *)uaddr)+i);
    if(result == -1){
      exit(-1);
    }
    total_result += result<<(i*8);
  }
  return total_result;
}

//Writes Byte to user address UDST
static bool
put_user_one_byte(uint8_t *udst, uint8_t byte)
{
  if(udst >= PHYS_BASE)
    return false;

  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static bool
put_user(uint8_t *udst, uint8_t value)
{
  int i;
  for (i=0; i<4; i++){
    if (!put_user_one_byte (((void *)udst)+i,*(char *)((void *)&value+i)&0xff)){
      exit(-1);
    }
  }
}


struct file_desc * get_file_desc(int fd) {
  if (fd<2 || fd>=file_desc_idx)
    exit(-1);

  struct process_sema *process = current_process_sema();
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
  lock_init(&fork_lock);
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
#ifdef VM
  thread_current()->esp = f->esp;
#endif

  switch( get_user ((int *)(f->esp))) {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      //printf("exit\n");
      exit (get_user ((int *)(f->esp)+1));
      break;
    case SYS_EXEC:
      //printf("exec\n");
      f->eax = exec (get_user ((int *)(f->esp)+1));
      break;
    case SYS_WAIT:
      //printf("wait\n");
      f->eax = wait (get_user ((int *)(f->esp)+1));
      break;
    case SYS_CREATE:
      f->eax = create (get_user ((int *)(f->esp)+1),
               get_user ((int *)(f->esp)+2));
      break;
    case SYS_REMOVE:
      f->eax = remove (get_user ((int *)(f->esp)+1));
      break;
    case SYS_OPEN:
      f->eax = open (get_user ((int *)(f->esp)+1));
      break;
    case SYS_FILESIZE:
      f->eax = filesize (get_user ((int *)(f->esp)+1));
      break;
    case SYS_READ:
      f->eax = read (get_user ((int *)(f->esp)+1),
                     get_user ((int *)(f->esp)+2),
                     get_user ((int *)(f->esp)+3));
      break;
    case SYS_WRITE:
      f->eax = write (get_user ((int *)(f->esp)+1),
                       get_user ((int *)(f->esp)+2),
                       get_user ((int *)(f->esp)+3));
      break;
    case SYS_SEEK:
      seek (get_user ((int *)(f->esp)+1),
            get_user ((int *)(f->esp)+2));
      break;
    case SYS_TELL:
      f->eax = tell (get_user ((int *)(f->esp)+1));
      break;
    case SYS_CLOSE:
      close (get_user ((int *)(f->esp)+1));
      break;
  }
}



void halt (void) {
  power_off();
}



void exit (int status) {
  if (!lock_held_by_current_thread(&fork_lock))
    lock_acquire(&fork_lock);
  printf("%s: exit(%d)\n", thread_current()->name, status);
  set_exit_status (status);
  lock_release(&fork_lock);
  thread_exit();
}

pid_t exec (const char *cmd_line) {
  lock_acquire(&fork_lock);
  int pid = process_execute (cmd_line);
  lock_release(&fork_lock);
  return pid;
}



int wait (pid_t pid) {
  return process_wait (pid);
}



bool create (const char *file, unsigned initial_size) {
  if (file == NULL)
    exit(-1);

  lock_acquire(&file_lock);
  bool result = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return result;
}



bool remove (const char *file) {
  lock_acquire(&file_lock);
  bool result = filesys_remove(file);
  lock_release(&file_lock);
  return result;
}



int open (const char *file) {
  if (file == NULL)
    exit(-1);

  lock_acquire(&file_lock);
  struct file *res_file = filesys_open(file);
  lock_release(&file_lock);
  if (res_file == NULL)
    return -1;

  struct file_desc *target = malloc(sizeof(struct file_desc));
  target->file = res_file;
  target->fd = file_desc_idx;
  strlcpy(target->name, file, strlen(file)+1);

  struct process_sema *process = current_process_sema();
  list_push_back(&(process->file_desc_list),
                 &(target->elem));

  return file_desc_idx++;
}



int filesize (int fd) {
  struct file_desc *target = get_file_desc(fd);

  lock_acquire(&file_lock);
  int result = file_length(target->file);
  lock_release(&file_lock);
  return result;
}



int read (int fd, void *buffer, unsigned size) {
  if (fd == 0) {
    return input_getc();
  }

  struct file_desc *target = get_file_desc(fd);

  lock_acquire(&file_lock);
  int result = file_read(target->file, buffer, size);
  lock_release(&file_lock);
  return result;
}



int write (int fd, const void *buffer, unsigned size) {
  // Write to console when fd == 1
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }

  struct file_desc *target = get_file_desc(fd);

  lock_acquire(&file_lock);
  int result = file_write(target->file, buffer, size);
  lock_release(&file_lock);
  return result;
}



void seek (int fd, unsigned position) {
  struct file_desc *target = get_file_desc(fd);

  lock_acquire(&file_lock);
  file_seek(target->file, position);
  lock_release(&file_lock);
}



unsigned tell (int fd) {
  struct file_desc *target = get_file_desc(fd);

  lock_acquire(&file_lock);
  unsigned result = file_tell(target->file);
  lock_release(&file_lock);
  return result;
}



void close (int fd) {
  if (fd<2 || fd>=file_desc_idx)
    exit(-1);

  struct file_desc *target = get_file_desc(fd);

  lock_acquire(&file_lock);
  file_close(target->file);
  lock_release(&file_lock);
  list_remove(&(target->elem));
  free(target);
}
