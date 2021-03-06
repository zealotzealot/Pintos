#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

struct lock inode_lock;

bool lock_on (){
  bool locked = false;

  if (lock_held_by_current_thread (&inode_lock))
    locked = true;
  else
    lock_acquire (&inode_lock);
}

void lock_off (bool locked){
  if (!locked)
    lock_release (&inode_lock);
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

bool disk_inode_build(struct inode_disk *, disk_sector_t);

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos >= inode->data.length)
    return -1;

  struct inode_disk *disk_inode = &inode->data;
  int idx = pos / DISK_SECTOR_SIZE;

  // Direct block
  if (idx < DIRECT_BLOCK_NUM) {
    return disk_inode->direct_blocks[idx];
  }
  // Indirect block
  else if (idx < DIRECT_BLOCK_NUM + 128) {
    idx -= DIRECT_BLOCK_NUM;
    struct indirect_disk *disk_indirect = calloc(1, sizeof *disk_indirect);
    cache_read(filesys_disk, disk_inode->indirect_block, disk_indirect);
    disk_sector_t result = disk_indirect->sectors[idx];
    free(disk_indirect);
    return result;
  }
  // Doubly indirect block
  else if (idx < DIRECT_BLOCK_NUM + 128 + 128*128) {
    idx -= DIRECT_BLOCK_NUM + 128;
    struct indirect_disk *disk_indirect = calloc(1, sizeof *disk_indirect);
    cache_read(filesys_disk, disk_inode->doubly_indirect_block, disk_indirect);
    struct indirect_disk *disk_doubly_indirect = calloc(1, sizeof *disk_indirect);
    cache_read(filesys_disk, disk_indirect->sectors[idx/128], disk_doubly_indirect);
    disk_sector_t result = disk_doubly_indirect->sectors[idx%128];
    free(disk_indirect);
    free(disk_doubly_indirect);
    return result;
  }
  // File size over upper bound
  else {
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  lock_init (&inode_lock);
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  bool locked = lock_on ();
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;

      success = true;

      size_t sectors = bytes_to_sectors (length);
      size_t i;
      for (i=0; i<sectors; i++){
        if (!disk_inode_build(disk_inode, i)){
          success = false;
          break;
        }
      }
      // Save and free inode
      if (success)
        cache_write(filesys_disk, sector, disk_inode);
      free (disk_inode);
    }
  
  lock_off (locked);
  return success;
}



bool disk_inode_build(struct inode_disk *disk_inode, disk_sector_t sector) {
  ASSERT(disk_inode != NULL);

  static char zeros[DISK_SECTOR_SIZE];

  // Build direct blocks
  if (sector < DIRECT_BLOCK_NUM) {
    int idx = sector;

    if (free_map_allocate (1, &(disk_inode->direct_blocks[idx])))
      cache_write (filesys_disk, disk_inode->direct_blocks[idx], zeros);

    return true;
  }

  // Build indirect blocks
  else if (sector < DIRECT_BLOCK_NUM + 128) {
    int idx = sector - DIRECT_BLOCK_NUM;

    struct indirect_disk *disk_indirect = calloc(1, sizeof *disk_indirect);
    if (disk_inode->indirect_block == 0) {
      free_map_allocate(1, &(disk_inode->indirect_block));
      memcpy(disk_indirect, zeros, DISK_SECTOR_SIZE);
    }
    else {
      cache_read(filesys_disk, disk_inode->indirect_block, disk_indirect);
    }

    if (free_map_allocate(1, &(disk_indirect->sectors[idx])))
      cache_write(filesys_disk, disk_indirect->sectors[idx], zeros);

    cache_write(filesys_disk, disk_inode->indirect_block, disk_indirect);
    free(disk_indirect);

    return true;
  }

  // Build doubly indirect blocks
  else if (sector < DIRECT_BLOCK_NUM + 128 + 128*128) {
    int idx = sector - DIRECT_BLOCK_NUM - 128;

    struct indirect_disk *disk_indirect = calloc(1, sizeof *disk_indirect);
    if (disk_inode->doubly_indirect_block == 0) {
      free_map_allocate(1, &(disk_inode->doubly_indirect_block));
      memcpy(disk_indirect, zeros, DISK_SECTOR_SIZE);
    }
    else {
      cache_read(filesys_disk, disk_inode->doubly_indirect_block, disk_indirect);
    }

    struct indirect_disk *disk_doubly_indirect = calloc(1, sizeof *disk_doubly_indirect);
    if (disk_indirect->sectors[idx/128] == 0) {
      free_map_allocate(1, &(disk_indirect->sectors[idx/128]));
      memcpy(disk_doubly_indirect, zeros, DISK_SECTOR_SIZE);
    }
    else {
      cache_read(filesys_disk, disk_indirect->sectors[idx/128], disk_doubly_indirect);
    }

    if (free_map_allocate(1, &(disk_doubly_indirect->sectors[idx%128])))
      cache_write(filesys_disk, disk_doubly_indirect->sectors[idx%128], zeros);

    cache_write(filesys_disk, disk_indirect->sectors[idx/128], disk_doubly_indirect);
    free(disk_doubly_indirect);

    cache_write(filesys_disk, disk_inode->doubly_indirect_block, disk_indirect);
    free(disk_indirect);

    return true;
  }

  // Sector out of upper boundary
  else
    return false;
}


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  bool locked = lock_on ();

  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_off (locked);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL){
    lock_off (locked);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read (filesys_disk, inode->sector, &inode->data);
  
  lock_off (locked);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  bool locked = lock_on ();
  if (inode != NULL)
    inode->open_cnt++;
  lock_off (locked);
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  bool locked = lock_on ();
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          size_t i;
          for (i=0; i<bytes_to_sectors(inode->data.length); i++)
            free_map_release(byte_to_sector(inode, i*DISK_SECTOR_SIZE), 1);
        }

      free (inode); 
    }
  lock_off (locked);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  bool locked = lock_on ();
  inode->removed = true;
  lock_off (locked);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  bool locked = lock_on ();
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (filesys_disk, sector_idx, buffer + bytes_read); 
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (filesys_disk, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  lock_off (locked);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  bool locked = lock_on ();
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if (inode->data.length < offset+size) {
    size_t i;
    for (i = bytes_to_sectors(inode->data.length);
         i < bytes_to_sectors(offset+size);
         i++) {
      disk_inode_build(&inode->data, i);
    }
    inode->data.length = offset+size;
    cache_write(filesys_disk, inode->sector, &inode->data);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Write full sector directly to disk. */
          cache_write (filesys_disk, sector_idx, buffer + bytes_written); 
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            cache_read (filesys_disk, sector_idx, bounce);
          else
            memset (bounce, 0, DISK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (filesys_disk, sector_idx, bounce); 
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  lock_off (locked);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  bool locked = lock_on();
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_off (locked);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  bool locked = lock_on();
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_off (locked);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
