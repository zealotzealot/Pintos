#ifndef VM_H
#define VM_H

#include "lib/kernel/hash.h"
#include "userprog/process.h"

struct page {
  int some_value;
  struct hash_elem elem_hash;
};

void page_init (struct process_sema *);

#endif
