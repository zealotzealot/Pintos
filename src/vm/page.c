#include "vm/page.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

bool lock_set;

struct hash *get_current_hash();
unsigned page_hash_func (const struct hash_elem *, void *);
bool page_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);

bool page_load_file(struct page *);
bool page_load_stack(struct page *);
bool page_load_swap(struct page *);



// Get page hash of current process
struct hash *current_page_hash() {
  return &((thread_current()->process_sema)->page_hash);
}

struct page *get_page(struct hash *h, void *addr) {
  struct page dummy_page;
  dummy_page.upage = pg_round_down(addr);

  if (h==NULL){
    h = current_page_hash();
    if (h==NULL)
      ASSERT(0);
  }
  
  struct hash_elem *hash_elem = hash_find(h, &dummy_page.elem_hash);
  if (hash_elem == NULL)
    return NULL;

  return hash_entry(hash_elem,
                    struct page,
                    elem_hash);
}

unsigned
page_hash_func (const struct hash_elem *p_, void *aux UNUSED){
  const struct page *p = hash_entry (p_, struct page, elem_hash);
  return hash_bytes (&p->upage, sizeof p->upage);
}

bool
page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  return hash_entry(a, struct page, elem_hash)->upage
       < hash_entry(b, struct page, elem_hash)->upage;
}

void page_init(struct hash *h) {
  hash_init(h, page_hash_func, page_less_func, NULL);
  if (!lock_set) {
    lock_set = true;
  }
}

//delete and free each fte, swafte, swap slot, pte
hash_action_func *page_free (struct hash_elem *h, void *aux UNUSED){
    struct page *page = hash_entry (h, struct page, elem_hash);
    
    if (page->kpage != NULL)
      frame_free (page->kpage, false);
    
    if(page->type == PAGE_SWAP)
      swap_free (page->slot);

    free(page);
}

//destroy page table
void page_destroy(struct hash *h) {
  if (h==NULL)
    return;
  hash_destroy (h, page_free);
}

void page_add_file(struct file *file, off_t ofs, uint8_t *upage, size_t page_read_bytes, size_t page_zero_bytes, bool writable) {
#ifdef DEBUG
  printf("page add file in %p %s\n",upage,thread_current()->name);
#endif
  struct page *page = malloc(sizeof(struct page));

  page->type = PAGE_FILE;
  page->file = file;
  page->ofs = ofs;
  page->upage = upage;
  page->kpage = NULL;
  page->page_read_bytes = page_read_bytes;
  page->page_zero_bytes = page_zero_bytes;
  page->writable = writable;

  hash_insert(current_page_hash(), &page->elem_hash);
#ifdef DEBUG
  printf("page add file out %p %s\n",upage,thread_current()->name);
#endif

}



void page_add_stack(void *addr) {
  if (get_page(NULL, pg_round_down(addr)) != NULL)
    return;

#ifdef DEBUG
  printf("page add stack in %p %s\n",addr,thread_current()->name);
#endif
  struct page *page = malloc(sizeof(struct page));

  page->type = PAGE_STACK;
  page->upage = pg_round_down(addr);
  page->kpage = NULL;
  page->writable = true;

  hash_insert(current_page_hash(), &page->elem_hash);
#ifdef DEBUG
  printf("page add stack out %p %s\n",addr,thread_current()->name);
#endif
}



void page_change_swap(struct hash *h, void *upage, int slot, bool writable, pid_t pid) {
#ifdef DEBUG
  printf("page add swap in %p %s\n",upage,thread_current()->name);
#endif
  struct page *page = get_page (h, upage);
  page->type = PAGE_SWAP;
  page->kpage = NULL;
  page->writable = writable;
  page->slot = slot;

#ifdef DEBUG
  printf("page add swap out %p %s\n",upage,thread_current()->name);
#endif
}



bool page_load(void * addr) {
  struct page *page = get_page (NULL, addr);
  if (page == NULL)
    return false;
  switch (page->type) {
    case PAGE_FILE:
      return page_load_file(page);
    case PAGE_STACK:
      return page_load_stack(page);
    case PAGE_SWAP:
      return page_load_swap(page);
    default:
      return false;
  }
}



bool page_load_file(struct page *page) {
#ifdef DEBUG
  printf("page load file in %p %s\n",page->upage,thread_current()->name);
#endif
  
  uint8_t *kpage = frame_allocate(page->upage, page->writable, PAL_USER);
  page->kpage = kpage;

  /* Load this page. */
  if (file_read_at (page->file, kpage, page->page_read_bytes, page->ofs) != (int) page->page_read_bytes) {
    frame_free(kpage, false);
    return false;
  }
  memset (kpage + page->page_read_bytes, 0, page->page_zero_bytes);

  page->type = PAGE_LOADED;

#ifdef DEBUG
  printf("page load file out %p %s\n",page->upage,thread_current()->name);
#endif

  return true;
}



bool page_load_stack(struct page *page) {
#ifdef DEBUG
  printf("page load stack in %p %s\n",page->upage,thread_current()->name);
#endif

  uint8_t *kpage = frame_allocate(page->upage, page->writable, PAL_USER | PAL_ZERO);
  page->kpage = kpage;

  page->type = PAGE_LOADED;

#ifdef DEBUG
  printf("page load stack out %p %s\n",page->upage,thread_current()->name);
#endif
  return true;
}



bool page_load_swap(struct page *page) {
#ifdef DEBUG
  printf("page load swap in %p %s\n",page->upage,thread_current()->name);
#endif

  uint8_t *kpage = frame_allocate(page->upage, page->writable, PAL_USER);
  page->kpage = kpage;

  swap_in(kpage, page->slot);

  page->type = PAGE_LOADED;
#ifdef DEBUG
  printf("page load swap out %p %s\n",page->upage,thread_current()->name);
#endif
  return true;
}
