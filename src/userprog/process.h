#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "lib/kernel/hash.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void set_exit_status (int);
struct process_sema *pid_to_process_sema(int);
bool install_page (void *upage, void *kpage, bool writable);

struct process_sema
{
  int pid;
  int alive; //0 means process die, 1 means not
  int parent_pid;
  int parent_alive; //0 means parent die, 1 means not
  int exit_status;
  int load_success; //0 means not load yet or success, -1 : fail
  struct semaphore sema;
  struct list_elem elem; 
  struct list file_desc_list;
  struct file *executable_file;

#ifdef VM
  struct hash page_hash;
#endif
};

#endif /* userprog/process.h */
