#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_flush_all ();
  free_map_close ();
}

/* Creates a file or dir at PATH with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file/dir named NAME already exists,
   or if internal memory allocation fails, or if a dir is
   missing in the path. */
bool
filesys_create (const char *path, off_t initial_size, bool isdir) 
{
  int len = strlen(path);
  if (len == 0)
    return false;
  char buf[len + 1];
  strlcpy (buf, path, len + 1); 

  
  /* Identify the file and the directory components,
     as well as the sector of the containing dir. */
  char *f = &buf[len];
  while (f != &buf[0] && *f != '/')
    f--;

  block_sector_t dir_sector;
  block_sector_t file_sector;

  if (f != &buf[0]) {
    *f = '\0'; 
    if ((dir_sector = dir_lookup_recursive (buf)) == -1)
      return false;
    f++;
  } else
    dir_sector = thread_current ()->wd;


  /* Add the new file/dir. */
  struct dir *dir = dir_open (inode_open (dir_sector));
  bool success = (dir != NULL
                  && free_map_allocate (1, &file_sector)
                  && inode_create (file_sector, initial_size, isdir)
                  && dir_add (dir, f, file_sector));
  if (!success && file_sector != 0) 
    free_map_release (file_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given PATH.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  block_sector_t file_sector = dir_lookup_recursive (path);
  if (file_sector == -1)
    return NULL;

  struct inode *inode = inode_open (file_sector);
  if (inode == NULL)
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Changes the working directory to the directory
   specified by DIR, whether relative or absolute.
   Return success. */
bool
filesys_chdir (const char *dir)
{
  block_sector_t inumber = dir_lookup_recursive (dir);
  if (inumber == -1)
    return false;
  
  struct inode *inode = inode_open (inumber);
  if (inode == NULL || !inode_isdir (inode))
    return false;

  inode_close (inode);
  thread_current ()->wd = inumber;
  return true;
}

