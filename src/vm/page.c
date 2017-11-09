#include "vm/page.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

bool lock_set;
struct lock page_lock;

struct hash *get_current_hash();
unsigned page_hash_func (const struct hash_elem *, void *);
bool page_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);

bool page_load_file(struct page *);
bool page_load_stack(struct page *);
bool page_load_swap(struct page *);



// Get page hash of current process
struct hash *current_page_hash() {
  return &current_process_sema()->page_hash;
}

struct page *get_page(void *addr) {
  struct page dummy_page;
  dummy_page.upage = pg_round_down(addr);

  struct hash_elem *hash_elem = hash_find(current_page_hash(), &dummy_page.elem_hash);
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
    lock_init(&page_lock);
  }
}

void page_destroy(struct hash *h) {
  hash_destroy(h, NULL);
}

void page_add_file(struct file *file, off_t ofs, uint8_t *upage, size_t page_read_bytes, size_t page_zero_bytes, bool writable) {
  struct page *page = malloc(sizeof(struct page));

  page->type = PAGE_FILE;
  page->file = file;
  page->ofs = ofs;
  page->upage = upage;
  page->page_read_bytes = page_read_bytes;
  page->page_zero_bytes = page_zero_bytes;
  page->writable = writable;

  hash_insert(current_page_hash(), &page->elem_hash);
}



void page_add_stack(void *addr) {
  struct page *page = malloc(sizeof(struct page));

  page->type = PAGE_STACK;
  page->upage = pg_round_down(addr);
  page->writable = true;

  hash_insert(current_page_hash(), &page->elem_hash);
}



void page_add_swap(void *upage, int slot, bool writable, pid_t pid) {
  if (!lock_held_by_current_thread(&page_lock))
    lock_acquire(&page_lock);

  struct page *page = malloc(sizeof(struct page));

  page->type = PAGE_SWAP;
  page->upage = upage;
  page->writable = writable;
  page->slot = slot;

  hash_insert(&pid_to_process_sema(pid)->page_hash, &page->elem_hash);

  if (!lock_held_by_current_thread(&page_lock))
    lock_release(&page_lock);
}



bool page_load(void * addr) {
  struct page *page = get_page(addr);
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
  lock_acquire(&page_lock);

  uint8_t *kpage = frame_allocate(page->upage, page->writable, PAL_USER);

  /* Load this page. */
  if (file_read_at (page->file, kpage, page->page_read_bytes, page->ofs) != (int) page->page_read_bytes) {
    frame_free(kpage, false);
    lock_release(&page_lock);
    return false;
  }
  memset (kpage + page->page_read_bytes, 0, page->page_zero_bytes);

  hash_delete(current_page_hash(), &page->elem_hash);
  free(page);

  lock_release(&page_lock);
  return true;
}



bool page_load_stack(struct page *page) {
  lock_acquire(&page_lock);

  uint8_t *kpage = frame_allocate(page->upage, page->writable, PAL_USER | PAL_ZERO);

  hash_delete(current_page_hash(), &page->elem_hash);
  free(page);

  lock_release(&page_lock);
  return true;
}



bool page_load_swap(struct page *page) {
  lock_acquire(&page_lock);

  uint8_t *kpage = frame_allocate(page->upage, page->writable, PAL_USER);

  swap_in(kpage, page->slot);

  hash_delete(current_page_hash(), &page->elem_hash);
  free(page);

  lock_release(&page_lock);
  return true;
}
