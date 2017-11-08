#include "vm/frame.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "vm/swap.h"

struct lock lock_frame;
struct list LRU_list;
struct hash frame_table;

unsigned frame_hash_func (const struct hash_elem *, void *);
bool frame_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);
struct frame_table_entry *get_frame(void *);


unsigned
frame_hash_func (const struct hash_elem *p_, void *aux UNUSED){
  const struct frame_table_entry *p =
          hash_entry (p_, struct frame_table_entry, elem_hash);
  
  return hash_bytes (&p->kpage, sizeof p->kpage);
}

bool
frame_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  return hash_entry(a, struct frame_table_entry, elem_hash)->kpage
          < hash_entry(b, struct frame_table_entry, elem_hash)->kpage;
}



struct frame_table_entry *get_frame(void *kpage) {
  struct frame_table_entry dummy_frame;
  dummy_frame.kpage = kpage;

  struct hash_elem *hash_elem = hash_find(&frame_table, &dummy_frame.elem_hash);
  if (hash_elem == NULL)
    return NULL;

  return hash_entry(hash_elem,
                    struct frame_table_entry,
                    elem_hash);
}



void frame_init(){
  lock_init (&lock_frame);
  list_init (&LRU_list);
  hash_init (&frame_table, frame_hash_func, frame_less_func, NULL);
}

struct frame_table_entry *choose_frame_evict() {
  struct list_elem *elem = list_begin(&LRU_list);

  if (elem == NULL)
    return NULL;

  return list_entry(elem, struct frame_table_entry, elem_list);
}

uint8_t *frame_allocate (void *upage, bool writable, enum palloc_flags flags){
  uint8_t *kpage = palloc_get_page(flags);
  
  if (kpage == NULL) {
    swap_out();
    kpage = palloc_get_page(flags);
    if (kpage == NULL) {
      return NULL;
    }
  }

  if (!install_page(upage, kpage, writable)) {
    palloc_free_page(kpage);
    return NULL;
  }

  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *) malloc (sizeof(struct frame_table_entry));
  fte->upage = (void *) ((uintptr_t) upage & ~PGMASK);
  fte->kpage = (void *) ((uintptr_t) kpage & ~PGMASK);
  fte->writable = writable;
  fte->pid = thread_current()->tid;

  lock_acquire (&lock_frame);
  list_push_back (&LRU_list, &fte->elem_list);
  hash_insert (&frame_table, &fte->elem_hash);
  lock_release (&lock_frame);

  return kpage;
}



void frame_free (void *kpage){
  if (kpage == NULL)
    return;

  palloc_free_page(kpage);

  struct frame_table_entry *fte = get_frame(kpage);

  lock_acquire (&lock_frame);
  list_remove (&fte->elem_list);
  hash_delete (&frame_table, &fte->elem_hash);
  lock_release (&lock_frame);

  pagedir_clear_page(thread_current()->pagedir, fte->upage);

  free(fte);
}
