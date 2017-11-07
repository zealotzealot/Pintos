#ifndef VM_H
#define VM_H

#include "lib/kernel/hash.h"
#include "userprog/process.h"

struct page {
  struct file *file;
  off_t ofs;
  uint8_t *upage;
  size_t page_read_bytes;
  size_t page_zero_bytes;
  bool writable;

  struct hash_elem elem_hash;
};

void page_init(struct hash *);
void page_destroy(struct hash *);

void page_add_file(struct file *, off_t, uint8_t *, size_t, size_t, bool);
bool page_load(void *);

#endif
