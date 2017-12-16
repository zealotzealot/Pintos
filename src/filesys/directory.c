#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "userprog/process.h"

/* A single directory entry. */
struct dir_entry 
    {
    disk_sector_t inode_sector;         /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

bool dir_chdir (char *path){
  struct dir *dir = dir_open_path (path);

  if (dir == NULL)
    return false;

  struct dir *process_dir = (thread_current()->process_sema)->dir;
  dir_close (process_dir);
  (thread_current()->process_sema)->dir = dir;

  return true;
}

bool split_path_name (char *path_, char *path, char *name){
  if (!strlen (path_) || strlen (path_) > PATH_MAX)
    return false;

  char *last_slash = strrchr (path_, '/');

  if (last_slash == NULL){
    if (strlen (path_) > NAME_MAX)
      return false;

    path [0] = '\0';
    strlcpy (name, path_, strlen (path_) + 1);
  }

  else{
    int last_slash_index = last_slash - path_ + 1;

    if (strlen(path_) - last_slash_index + 1 > NAME_MAX)
      return false;

    strlcpy (path, path_, last_slash_index + 1);
    strlcpy (name, path_ + last_slash_index, strlen(path_) - last_slash_index + 1);

    path [last_slash_index] = '\0';
    name [strlen (path_) - last_slash_index + 1] = '\0';
  }

  return true;
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) 
{
  bool success = inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
  struct dir *dir = dir_open_root ();
  ASSERT (dir_add (dir, ".", ROOT_DIR_SECTOR));
  dir_close(dir);
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

struct dir *dir_open_path (char *path_){
  if (strlen(path_) > PATH_MAX)
    return NULL;

  if (!strlen (path_))
    return dir_open_current ();

  char path [strlen (path_) + 1];
  strlcpy (path, path_, strlen(path_) + 1);

  int i, cnt = 0;
  for (i = 0; i < strlen (path); i ++){
    if (path[i] == '/')
      ++cnt;
  }

  struct dir *dir, *dir_next;
  if (path[0] == '/') //absolute
    dir = dir_open_root ();
  else{
    dir = dir_open_current ();
    ++cnt;
  }

  struct inode *inode;
  char *token, *save_ptr;

  for (token = strtok_r (path, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr)){
    if (--cnt < 0)
      break;

    if (strlen (token) > NAME_MAX)
      return NULL;

    if (!strlen (token))
      continue;

    if (!dir_lookup (dir, token, &inode)){
      dir_close (dir);
      return NULL;
    }

    if (inode->removed){
      return NULL;
    }

    dir_next = dir_open (inode);

    if(dir_next == NULL){
      dir_close (dir);
      return NULL;
    }

    dir_close (dir);
    dir = dir_next;
  }
  return dir;
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

struct dir *
dir_open_current (void){
  if (thread_current() -> tid == 1)
    return dir_open_root();

  struct dir *dir = (thread_current()->process_sema)->dir;
  return dir_open (inode_reopen (dir->inode));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e){
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
       }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (!strlen(name))
    *inode = dir->inode;

  else if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);

  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) 
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  int size = inode_write_at (dir->inode, &e, sizeof e, ofs);
  success = size == sizeof e;
 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if ((inode->data).is_dir){
    if (inode->open_cnt > 1)
      goto done;

    char name [NAME_MAX + 1];
    struct dir *dir2 = dir_open (inode);
    if (dir_readdir (dir2, name)){
      dir_close (dir2);
      goto done;
    }
    dir_close (dir2);
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          if (!strcmp (e.name, ".") || !strcmp (e.name, ".."))
            continue;

          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
