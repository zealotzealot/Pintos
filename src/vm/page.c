#include "vm/page.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

struct lock page_lock;

struct hash *get_current_hash();
unsigned page_hash_func (const struct hash_elem *, void *);
bool page_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);

// Get page hash of current process
struct hash *current_page_hash() {
  return &pid_to_process_sema(thread_current()->tid)->page_hash;
}

struct page *get_page(void *addr) {
  struct page dummy_page;
  dummy_page.upage = pg_round_down(addr);

  return hash_entry(hash_find(current_page_hash(), &dummy_page.elem_hash),
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
}

void page_destroy(struct hash *h) {
  hash_destroy(h, NULL);
}

void page_add_file(struct file *file, off_t ofs, uint8_t *upage, size_t page_read_bytes, size_t page_zero_bytes, bool writable) {
  struct page *page = malloc(sizeof(struct page));

  page->file = file;
  page->ofs = ofs;
  page->upage = upage;
  page->page_read_bytes = page_read_bytes;
  page->page_zero_bytes = page_zero_bytes;
  page->writable = writable;

  hash_insert(current_page_hash(), &page->elem_hash);
}

bool page_load_file(void * addr) {
  struct page *page = get_page(addr);

  uint8_t *kpage = push_frame_table(page->upage, page->writable, PAL_USER);

  /* Load this page. */
  if (file_read_at (page->file, kpage, page->page_read_bytes, page->ofs) != (int) page->page_read_bytes) {
    palloc_free_page (kpage);
    return false;
  }
  memset (kpage + page->page_read_bytes, 0, page->page_zero_bytes);

  /* Add the page to the process's address space. */
  if (!install_page (page->upage, kpage, page->writable)) {
    palloc_free_page (kpage);
    return false;
  }

  return true;
}
