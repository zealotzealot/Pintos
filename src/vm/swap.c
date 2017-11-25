#include "vm/swap.h"
#include <stdio.h>
#include "lib/kernel/bitmap.h"
#include "lib/string.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/interrupt.h"
#include "vm/frame.h"
#include "vm/page.h"



struct disk *swap_disk;
struct bitmap *swap_bitmap;



void swap_init(){
  swap_disk = disk_get (1,1);
  swap_bitmap = bitmap_create (disk_size(swap_disk));
  bitmap_set_all (swap_bitmap, false);
  lock_init(&swap_lock);
}

void swap_free (int slot){
  ASSERT(lock_held_by_current_thread(&swap_lock));
  int i;
  for(i=0; i<8; i++){
    bitmap_flip (swap_bitmap, slot+i);
  }
}

void swap_in(void *kpage, int slot){
#ifdef DEBUG
  printf("swap in in %p, %d\n",kpage, slot);
#endif
  lock_acquire(&swap_lock);
  int i;
  for (i=0; i<8; i++){
    disk_read (swap_disk, slot+i, kpage+i*DISK_SECTOR_SIZE);
    bitmap_flip (swap_bitmap, slot+i);
  }
  lock_release(&swap_lock);
#ifdef DEBUG
  printf("swap in out %p, %d\n",kpage,slot);
#endif
}



void swap_out() {
#ifdef DEBUG
  printf("swap out in\n");
#endif
  lock_acquire(&swap_lock);
  
  struct frame_table_entry *fte_evicted = choose_frame_evict();
  ASSERT(fte_evicted != NULL)

  void *kpage = fte_evicted->kpage;
  
  int slot_start,i;
  slot_start = bitmap_scan_and_flip(swap_bitmap, 0, 8, false);
  for(i=0; i<8; i++){
    disk_write(swap_disk, slot_start+i, kpage+i*DISK_SECTOR_SIZE);
  }
#ifdef DEBUG
  printf("swap out %p, %d\n",kpage, slot_start);
#endif

  struct hash *page_hash = &(((fte_evicted->thread)->process_sema)->page_hash);
  ASSERT(page_hash != NULL)

  page_change_swap(page_hash,fte_evicted->upage, slot_start, fte_evicted->writable, fte_evicted->thread->tid);
 
  frame_free(kpage, true);

 lock_release(&swap_lock);
#ifdef DEBUG
 printf("swap out out\n");
#endif
}
