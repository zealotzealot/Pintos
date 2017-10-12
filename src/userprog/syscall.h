#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>

void syscall_init (void);
void exit (int);

typedef int pid_t;

struct file_desc {
  struct file *file;
  int fd;
  struct list_elem elem;
};

#endif /* userprog/syscall.h */
