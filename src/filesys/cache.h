#include "devices/disk.h"

void cache_destroy();
void cache_init();
struct cache *get_cache (disk_sector_t);
void cache_read (struct disk *, disk_sector_t, void *);
void cache_write (struct disk *, disk_sector_t, void *);
