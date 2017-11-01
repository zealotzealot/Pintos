#ifndef VM_H
#define VM_H

#include "lib/kernel/hash.h"

struct frame_table_entry {
  void *vaddr;
  void *paddr;
  int pid;
  struct list_elem elem_list;
  struct hash_elem elem_hash;
};

void frame_init (void);
bool evict_frame (void);
bool push_frame_table (void *, void *, bool);

#endif
