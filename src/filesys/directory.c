#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/file-map.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

static void
dir_lock (const struct dir *dir)
{
  if (dir->inode == NULL) return;
  struct file_synch_status *fss = status_for_inode (dir->inode);
  if (fss == NULL) return;
  lock_acquire (&fss->dir_lock);
}

static void
dir_unlock (const struct dir *dir)
{
  if (dir->inode == NULL) return;
  struct file_synch_status *fss = status_for_inode (dir->inode);
  if (fss == NULL) return;
  lock_release (&fss->dir_lock);
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
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

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
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
       ofs += sizeof e) 
  {
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

/* Returns true iff DIR contains only the entries
   '.' and '..' */
static bool
dir_empty (struct inode *dir)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);

  for (ofs = 0; inode_read_at (dir, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    if (!e.in_use)
      continue;
    
    if (strcmp (e.name, ".") != 0 && strcmp (e.name, "..") != 0)
      return false;
  }
  return true;
}


/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  dir_lock (dir);
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  dir_unlock (dir);
  return *inode != NULL;
}


/* Searches for a file with the given PATH and returns the inumber of the
   file, -1 otherwise.

   If PATH begins with a '/', initiates the search from the root directory.
   Otherwise, searches using the current thread's working directory */
block_sector_t
dir_lookup_recursive (const char *path)
{
  int len = strlen (path);
  if (len == 0)
    return -1;
  char buf[len + 1];    
  strlcpy (buf, path, len + 1); 

  /* Initialize directory entry */
  struct dir dir;
  struct dir_entry entry;
  block_sector_t sector; 

  if (buf[0] == '/') 
    sector = ROOT_DIR_SECTOR;
  else
    sector = thread_current ()->wd;

  /* Remove trailing '/' */
  char *c = &buf[len - 1];
  while (*c == '/') {
    *c = '\0';
    c--;
  }

  c = &buf[0];
  while (true) {
    /* Remove leading '/' */
    while (*c == '/')
      c++;

    if (*c == '\0')
      return sector;

    /* Advance to the next '/' and zero it */
    char *next = strchr (c, '/');
    if (next != NULL)
      *next = '\0';
    
    dir.inode = inode_open (sector);
    dir_lock (&dir);
    bool success = lookup (&dir, c, &entry, NULL);
    dir_unlock (&dir);

    if (!success)
      return DIR_LOOKUP_ERROR;

    sector = entry.inode_sector;
    inode_close (dir.inode);

    if (next == NULL)
      return sector;

    c = next + 1;
  }
}


/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  dir_lock (dir);
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
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  dir_unlock (dir);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  dir_lock (dir);
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Check if name is current working directory. */
  if (e.inode_sector == thread_current ()->wd)
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL || (inode_isdir (inode) && 
    (inode_open_cnt (inode) > 1 || !dir_empty (inode))))
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  dir_unlock (dir);
  inode_close (inode);
  return success;
}


/* Converts a file pointer from the file map into a dir
   pointer for dir_readdir. */
struct dir *
dir_shim_file (struct file *fp)
{
  return (struct dir *) fp;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  if (dir == NULL || !inode_isdir (dir->inode))
    return false;

  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use && strcmp (e.name, ".") && strcmp (e.name, ".."))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
