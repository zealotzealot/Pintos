#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/kernel/hash.h"
#include <list.h>

void syscall_init (void);
void exit (int);

typedef int pid_t;

struct file_desc {
  struct file *file;
  char name[32];
  int fd;
  struct list_elem elem;
};

struct mte {
  int map_id;
  struct file *file;
  void *base;
  int length;
  struct list_elem elem;
};

#endif /* userprog/syscall.h */
