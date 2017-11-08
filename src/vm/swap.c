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
}



void swap_in(void *kpage, int slot){
  int i;
  for (i=0; i<8; i++){
    disk_read (swap_disk, slot+i, kpage+i*DISK_SECTOR_SIZE);
  bitmap_flip (swap_bitmap, slot+i);
  }
}



void swap_out() {
  struct frame_table_entry *fte_evicted = choose_frame_evict();
  if(fte_evicted == NULL)
    ASSERT(0);

  void *kpage = fte_evicted->kpage;

  int slot_start,i;
  slot_start = bitmap_scan_and_flip(swap_bitmap, 0, 8, false);
  for(i=0; i<8; i++){
    disk_write(swap_disk, slot_start+i, kpage+i*DISK_SECTOR_SIZE);
  }

  page_add_swap(fte_evicted->upage, slot_start, fte_evicted->writable);
 
  frame_free(kpage);
}
