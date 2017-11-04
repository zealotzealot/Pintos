#include "vm/page.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "threads/malloc.h"

struct lock page_lock;

unsigned page_hash_func (const struct hash_elem *, void *);
bool page_less_func (const struct hash_elem *, const struct hash_elem *, void * UNUSED);


unsigned
page_hash_func (const struct hash_elem *p_, void *aux UNUSED){
  const struct page *p = hash_entry (p_, struct page, elem_hash);
  return hash_bytes (&p->some_value, sizeof p->some_value);
}

bool
page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  return hash_entry(a, struct page, elem_hash)->some_value
       < hash_entry(b, struct page, elem_hash)->some_value;
}

void page_init(struct hash *h) {
  hash_init(h, page_hash_func, page_less_func, NULL);
}

void page_destroy(struct hash *h) {
  hash_destroy(h, NULL);
}
