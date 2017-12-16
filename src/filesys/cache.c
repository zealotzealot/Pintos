#include "filesys/cache.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/synch.h"

int CACHE_LIMIT = 64;

struct lock lock_cache;

struct list FIFO_list;
struct hash buffer_cache;

struct cache{
  struct disk *disk;
  disk_sector_t sec_no;
  struct list_elem elem_list;
  struct hash_elem elem_hash;
  char buffer[DISK_SECTOR_SIZE];
};

unsigned cache_hash_func (const struct hash_elem *, void *);
bool cache_less_func (const struct hash_elem *,
    const struct hash_elem *, void *aux UNUSED);
void cache_evict ();
struct cache *cache_allocate (struct disk *, disk_sector_t);

unsigned cache_hash_func (const struct hash_elem *p_, void *aux UNUSED){
  const struct cache *p = hash_entry (p_, struct cache, elem_hash);
  return hash_bytes (&p->sec_no, sizeof p->sec_no);
}

bool cache_less_func (const struct hash_elem *a,
    const struct hash_elem *b, void *aux UNUSED){
  return hash_entry(a, struct cache, elem_hash)->sec_no
          < hash_entry(b, struct cache, elem_hash)->sec_no;
}

void cache_read (struct disk *disk, disk_sector_t sec_no, void *buffer){
  lock_acquire (&lock_cache);

  struct cache *cache = get_cache (sec_no);
  if(cache == NULL){
    cache = cache_allocate (disk, sec_no);
    disk_read (disk, sec_no, cache->buffer);
  }
  memcpy (buffer, cache->buffer, DISK_SECTOR_SIZE);

  lock_release (&lock_cache);
}

void cache_write (struct disk *disk, disk_sector_t sec_no, void *buffer){
  lock_acquire (&lock_cache);

  struct cache *cache = get_cache (sec_no);
  if(cache == NULL){
    cache = cache_allocate (disk, sec_no);
    disk_read (disk, sec_no, cache->buffer);
  }
  memcpy (cache->buffer, buffer, DISK_SECTOR_SIZE);

  lock_release (&lock_cache);
}

struct cache *get_cache (disk_sector_t sector){
  struct cache dummy_sector;
  dummy_sector.sec_no = sector;

  struct hash_elem *hash_elem = hash_find (&buffer_cache, &dummy_sector.elem_hash);
  if(hash_elem == NULL)
    return NULL;

  return hash_entry(hash_elem, struct cache, elem_hash);
}

void cache_init (){
  lock_init (&lock_cache);
  list_init (&FIFO_list);
  hash_init (&buffer_cache, cache_hash_func, cache_less_func, NULL);
}

//evict cache from buffer and write to disk
void cache_evict (){
  ASSERT (hash_size(&buffer_cache)==CACHE_LIMIT);

  struct list_elem *e;
  struct cache *cache_evict;

  cache_evict = list_entry(list_pop_front (&FIFO_list), struct cache, elem_list);
  ASSERT (cache_evict != NULL);

  struct hash_elem *old_elem = hash_delete (&buffer_cache, &cache_evict->elem_hash);
  ASSERT (old_elem != NULL);

  disk_write (cache_evict->disk, cache_evict->sec_no, cache_evict->buffer);

  free (cache_evict);
}

struct cache *cache_allocate (struct disk *disk, disk_sector_t sec_no){
  if (hash_size(&buffer_cache) == CACHE_LIMIT){
    cache_evict();
  }

  struct cache *cache;
  cache = (struct cache *)malloc (sizeof(struct cache));
  cache->disk = disk;
  cache->sec_no = sec_no;

  list_push_back (&FIFO_list, &cache->elem_list);
  hash_insert (&buffer_cache, &cache->elem_hash);

  return cache;
}
