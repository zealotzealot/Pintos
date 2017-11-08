#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/hash.h"
#include "threads/palloc.h"

struct frame_table_entry {
  void *upage;
  void *kpage;
  bool writable;
  struct thread *thread;
  struct list_elem elem_list;
  struct hash_elem elem_hash;
};

void frame_init (void);
struct frame_table_entry *choose_frame_evict(void);
uint8_t *frame_allocate (void *, bool, enum palloc_flags);
void frame_free (void *);

#endif
