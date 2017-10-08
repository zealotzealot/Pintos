#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

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
      printf("halt\n");
      break;
    case SYS_EXIT:
      exit(*((int *)(f->esp)+1));
      break;
    case SYS_EXEC:
      printf("exec\n");
      break;
    case SYS_WAIT:
      printf("wait\n");
      break;
    case SYS_CREATE:
      printf("create\n");
      break;
    case SYS_REMOVE:
      printf("remove\n");
      break;
    case SYS_OPEN:
      printf("open\n");
      break;
    case SYS_FILESIZE:
      printf("filesize\n");
      break;
    case SYS_READ:
      printf("read\n");
      break;
    case SYS_WRITE:
      printf("write\n");
      break;
    case SYS_SEEK:
      printf("seek\n");
      break;
    case SYS_TELL:
      printf("tell\n");
      break;
    case SYS_CLOSE:
      printf("close\n");
      break;
  }
}



void halt (void) {
}



void exit (int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}



pid_t exec (const char *cmd_line) {
}



int wait (pid_t pid) {
}



bool create (const char *file, unsigned initial_size) {
}



bool remove (const char *file) {
}



int open (const char *file) {
}



int filesize (int fd) {
}



int read (int fd, void *buffer, unsigned size) {
}



int write (int fd, const void *buffer, unsigned size) {
}



void seek (int fd, unsigned position) {
}



unsigned tell (int fd) {
}



void close (int fd) {
}
