#include "vm/frame.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"

struct lock lock_frame;
struct list LRU_list;
struct hash frame_table;

unsigned frame_hash_func (const struct hash_elem *, void *);
bool frame_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);


unsigned
frame_hash_func (const struct hash_elem *p_, void *aux UNUSED){
  const struct frame_table_entry *p =
          hash_entry (p_, struct frame_table_entry, elem_hash);
  
  return hash_bytes (&p->paddr, sizeof p->paddr);
}

bool
frame_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  return hash_entry(a, struct frame_table_entry, elem_hash)->paddr
          < hash_entry(b, struct frame_table_entry, elem_hash)->paddr;
}

void frame_init(){
  lock_init (&lock_frame);
  list_init (&LRU_list);
  hash_init (&frame_table, frame_hash_func, frame_less_func, NULL);
}

bool evict_frame (){
  struct list_elem *fte_evicted = list_begin (&LRU_list);
  
  lock_acquire (&lock_frame);
  list_remove (fte_evicted);
  lock_release (&lock_frame);

  return true;
}

bool push_frame_table (void *upage, void *kpage, bool writable){
  
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *) malloc (sizeof(struct frame_table_entry));
  fte->vaddr = (void *) ((uintptr_t) upage & ~PGMASK);
  fte->paddr = (void *) ((uintptr_t) kpage & ~PGMASK);
  fte->pid = thread_current()->tid;

  lock_acquire (&lock_frame);
  list_push_back (&LRU_list, &fte->elem_list);
  hash_insert (&frame_table, &fte->elem_hash);
  lock_release (&lock_frame);

  return true;
}
