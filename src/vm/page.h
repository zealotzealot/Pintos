#ifndef VM_H
#define VM_H

#include "lib/kernel/hash.h"
#include "userprog/process.h"

enum page_type {
  PAGE_FILE,
};

struct page {
  // Common fields
  enum page_type type;
  uint8_t *upage;
  bool writable;

  // Fields for file
  struct file *file;
  off_t ofs;
  size_t page_read_bytes;
  size_t page_zero_bytes;

  struct hash_elem elem_hash;
};

void page_init(struct hash *);
void page_destroy(struct hash *);

void page_add_file(struct file *, off_t, uint8_t *, size_t, size_t, bool);
bool page_load(void *);

#endif
