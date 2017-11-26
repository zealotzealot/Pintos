#include "vm/frame.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/swap.h"

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
  struct list_elem *e;
  struct frame_table_entry *fte;
  struct hash *page_hash;
  struct page *page;

  for (e=list_begin(&LRU_list); e!=list_end(&LRU_list); e=list_next(e)){
    fte = list_entry (e, struct frame_table_entry, elem_list);

    page_hash = &(((fte->thread)->process_sema)->page_hash);
    ASSERT(page_hash != NULL)

    page = get_page (page_hash, fte->upage);
    if(page->type == PAGE_LOADED && page->pin == false)
      return fte;
  }
  ASSERT (false);
}

uint8_t *frame_allocate (void *upage, bool writable, enum palloc_flags flags){
#ifdef DEBUG
  printf("frame_allocate in upage %p %s\n",upage,thread_current()->name);
#endif
  lock_acquire(&lock_frame);
  
  uint8_t *kpage = palloc_get_page(flags);
  
  if (kpage == NULL) {
    swap_out();
    kpage = palloc_get_page(flags);
    ASSERT(kpage != NULL);
  }
  
  bool install_success = install_page(upage, kpage, writable);
  ASSERT(install_success);
  
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *) malloc (sizeof(struct frame_table_entry));
  fte->upage = (void *) ((uintptr_t) upage & ~PGMASK);
  fte->kpage = (void *) ((uintptr_t) kpage & ~PGMASK);
  fte->writable = writable;
  fte->thread = thread_current();
  
  list_push_back (&LRU_list, &fte->elem_list);
  struct hash_elem *old_hash = hash_replace (&frame_table, &fte->elem_hash);
  ASSERT(old_hash == NULL);

#ifdef DEBUG
  printf("frame_allocate kpage %p\n",fte->kpage);
#endif
  lock_release(&lock_frame);
#ifdef DEBUG
  printf("frame_allocate out upage %p %s\n",upage,thread_current()->name);
#endif
  return kpage;
}



void frame_free (void *kpage, bool locked){
#ifdef DEBUG
  printf("frame_free in kpage %p %s\n",kpage,thread_current()->name);
#endif
  if (kpage == NULL)
    return;

  if (!locked)
    lock_acquire(&lock_frame);

  palloc_free_page(kpage);

  struct frame_table_entry *fte = get_frame(kpage);

  ASSERT(fte != NULL)

  list_remove (&fte->elem_list);
  hash_delete (&frame_table, &fte->elem_hash);

  pagedir_clear_page(fte->thread->pagedir, fte->upage);

  free(fte);

  if (!locked)
    lock_release(&lock_frame);
#ifdef DEBUG
  printf("frame_free out kpage %p %s\n",kpage,thread_current()->name);
#endif
}
