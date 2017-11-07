#ifndef VM_H
#define VM_H

#include "lib/kernel/hash.h"
#include "threads/palloc.h"

struct frame_table_entry {
  void *vaddr;
  void *paddr;
  int pid;
  struct list_elem elem_list;
  struct hash_elem elem_hash;
};

void frame_init (void);
bool evict_frame (void);
uint8_t *frame_allocate (void *, bool, enum palloc_flags);

#endif
