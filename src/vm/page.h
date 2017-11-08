#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "userprog/process.h"

enum page_type {
  PAGE_FILE,
  PAGE_STACK,
  PAGE_SWAP,
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

  // No fields for stack

  // Fields for swap
  int slot;

  struct hash_elem elem_hash;
};

void page_init(struct hash *);
void page_destroy(struct hash *);

void page_add_file(struct file *, off_t, uint8_t *, size_t, size_t, bool);
void page_add_stack(void *);
void page_add_swap(void *, int, bool, pid_t);
bool page_load(void *);

#endif
