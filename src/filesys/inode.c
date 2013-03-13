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
#include "userprog/file-map.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Breakdown of multilevel index in inode_disk */
#define DIRECT_BLOCKS 64
#define INDIRECT_BLOCKS 60 
#define DOUBLY_INDIRECT 1
#define TOTAL_BLOCKS (DIRECT_BLOCKS + INDIRECT_BLOCKS + DOUBLY_INDIRECT)
#define INDIRECT_SIZE (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))
#define MAX_SIZE (1 << 23)

#define NO_BLOCK ((block_sector_t) -1)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned isdir;                     /* Nonzero for directories */
    unsigned magic;                     /* Magic number. */
    block_sector_t blocks[TOTAL_BLOCKS];         /* Block table. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };


static block_sector_t byte_to_sector (struct inode *inode, 
                                      unsigned offset, block_sector_t *next) 
{
  unsigned idx = offset / BLOCK_SECTOR_SIZE;
  struct inode_disk *id = &inode->data;
  if (next) *next = NO_BLOCK;

  if (idx < DIRECT_BLOCKS) 
  {
    if (next && idx + 1 < DIRECT_BLOCKS) *next = id->blocks[idx];
    return id->blocks[idx];
  } else if (idx < DIRECT_BLOCKS + INDIRECT_SIZE * INDIRECT_BLOCKS) 
  {
    idx = idx - DIRECT_BLOCKS;
    block_sector_t block = id->blocks[DIRECT_BLOCKS + idx / INDIRECT_SIZE];
    if (block == NO_BLOCK) return NO_BLOCK; 
    block_sector_t blocks[INDIRECT_SIZE];
    block_read_cache (fs_device, block, &blocks,
                      0, BLOCK_SECTOR_SIZE, true, -1);
    if (next && (idx + 1) % INDIRECT_SIZE != 0)
      *next = blocks[(idx + 1) % INDIRECT_SIZE];
    return blocks[idx % INDIRECT_SIZE];
  } else if (idx < MAX_SIZE / BLOCK_SECTOR_SIZE)
  { 
    idx = idx - DIRECT_BLOCKS - INDIRECT_SIZE * INDIRECT_BLOCKS;
    block_sector_t block = id->blocks[DIRECT_BLOCKS + INDIRECT_BLOCKS];
    if (block == NO_BLOCK) return NO_BLOCK;
    block_sector_t blocks[INDIRECT_SIZE];
    block_read_cache (fs_device, block, &blocks,
                      0, BLOCK_SECTOR_SIZE, true, -1);

    block = blocks[idx / INDIRECT_SIZE];
    if (block == NO_BLOCK) return NO_BLOCK;
    block_read_cache (fs_device, block, &blocks, 
                      0, BLOCK_SECTOR_SIZE, true, -1);

    if (next && (idx + 1) % INDIRECT_SIZE != 0)
      *next = blocks[(idx + 1) % INDIRECT_SIZE];
    return blocks[idx % INDIRECT_SIZE];
  } else return NO_BLOCK;
}

/*
  Allocate a new disk sector to be filled in to the block table for
    a given inode at index idx.  Allocates additional blocks if necessary
    for indirect blocks.  Returns the allocated block_sector_t, or NO_BLOCK
    if allocation fails.
    TODO synchronization
*/

static block_sector_t allocate_block (struct inode *inode, 
                                      unsigned idx) 
{
  block_sector_t new_block;
  if (!free_map_allocate (1, &new_block)) return NO_BLOCK;
  block_sector_t data[INDIRECT_SIZE];
  memset (data, 0, BLOCK_SECTOR_SIZE);
  block_write_cache (fs_device, new_block, data, 0, 
                     BLOCK_SECTOR_SIZE, false, inode->sector);
  unsigned i = 0;
  for(; i < INDIRECT_SIZE; i++) data[i] = NO_BLOCK;

  if (idx < DIRECT_BLOCKS) 
  {
    inode->data.blocks[idx] = new_block;
    block_write_cache (fs_device, inode->sector, &inode->data, 0,
                       BLOCK_SECTOR_SIZE, true, -1);
  } else if (idx < DIRECT_BLOCKS + INDIRECT_SIZE * INDIRECT_BLOCKS) 
  {
    idx = idx - DIRECT_BLOCKS;
    block_sector_t block = inode->data.blocks
                                  [DIRECT_BLOCKS + idx / INDIRECT_SIZE];
    if (block == NO_BLOCK) {
      if (free_map_allocate (1, &block)) {
        inode->data.blocks[DIRECT_BLOCKS + idx / INDIRECT_SIZE] = block;
        block_write_cache (fs_device, inode->sector, &inode->data, 0,
                           BLOCK_SECTOR_SIZE, true, block);
        block_write_cache (fs_device, block, data, 0,
                           BLOCK_SECTOR_SIZE, true, -1);
      } else {
        free_map_release (new_block, 1);
        return NO_BLOCK;
      }
    }

    block_write_cache (fs_device, block, &new_block,
                      sizeof(block_sector_t) * (idx % INDIRECT_SIZE),
                      sizeof(block_sector_t), true, -1);
  } else if (idx < MAX_SIZE / BLOCK_SECTOR_SIZE)
  { 
    idx = idx - DIRECT_BLOCKS - INDIRECT_BLOCKS * INDIRECT_SIZE;
    block_sector_t block = inode->data.blocks[DIRECT_BLOCKS + INDIRECT_BLOCKS];
    if (block == NO_BLOCK) {
      if (free_map_allocate (1, &block)) {
        inode->data.blocks[DIRECT_BLOCKS + INDIRECT_BLOCKS] = block;
        block_write_cache (fs_device, inode->sector, &inode->data, 0,
                           BLOCK_SECTOR_SIZE, true, block);
        block_write_cache (fs_device, block, data, 0,
                           BLOCK_SECTOR_SIZE, true, -1); 
      } else {
        free_map_release (new_block, 1);
        return NO_BLOCK;
      }
    }

    block_sector_t second_block;
    block_read_cache (fs_device, block, &second_block,
                      sizeof(block_sector_t) * (idx / INDIRECT_SIZE),
                      sizeof(block_sector_t), true, -1);
                      
    if (second_block == NO_BLOCK) {
      if (free_map_allocate (1, &second_block)) {
        block_write_cache (fs_device, block, &second_block,
                           sizeof(block_sector_t) * (idx / INDIRECT_SIZE),
                           sizeof(block_sector_t), true, second_block);
        block_write_cache (fs_device, second_block, data, 0,
                           BLOCK_SECTOR_SIZE, true, -1);
      } else {
        free_map_release (new_block, 1);
        return NO_BLOCK;
      }
    }

    block_write_cache (fs_device, second_block, &new_block, 
                       sizeof(block_sector_t) * (idx % INDIRECT_SIZE),
                       sizeof(block_sector_t), true, -1); 
  } else {
    free_map_release (new_block, 1);
    return NO_BLOCK;
  }

  return new_block;
}


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->isdir = isdir ? 1 : 0;

      // Initialize all block entries to -1 (NO_BLOCK).
      int i = 0;
      for(; i < TOTAL_BLOCKS; i++) disk_inode->blocks[i] = NO_BLOCK;

      // Write only the disk_inode, allocate pages lazily
      block_write_cache (fs_device, sector, disk_inode, 0,
                         BLOCK_SECTOR_SIZE, true, -1);
      success = true;

      struct inode inode;
      inode.data = *disk_inode;
      inode.sector = sector;
      int num_sectors = bytes_to_sectors (length);
      for(i = 0; i < num_sectors; i ++) 
        allocate_block (&inode, i);

      /*
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write_cache(fs_device, sector, disk_inode, 0,
                            BLOCK_SECTOR_SIZE, true, -1);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++)
              {
                block_write_cache(fs_device, disk_inode->start + i, zeros, 0,
                                  BLOCK_SECTOR_SIZE, false, -1);
              }
            }
          success = true; 
        }
      */

      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
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
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read_cache(fs_device, inode->sector, &inode->data, 0,
                   BLOCK_SECTOR_SIZE, true, -1);
                   
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns whether or not INODE is a directory. */
bool
inode_isdir (const struct inode *inode)
{
  return inode->data.isdir != 0;
}

/* Returns INODE's inode number. */
block_sector_t
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
          int i = 0;
          for (; i < inode->data.length; i += BLOCK_SECTOR_SIZE) {
            // TODO optimize, free indirect blocks, etc.           
            block_sector_t block = byte_to_sector (inode, i, NULL);
            if (block != NO_BLOCK) {
              free_map_release (block, 1);
            }
          }
          free_map_release (inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t next;
      block_sector_t sector_idx = byte_to_sector (inode, offset, &next);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
   
      //printf("%d, %d, %d, %d, %d %d\n", size, offset,
      //    inode_left, sector_left, min_left, chunk_size);
   
      if (chunk_size <= 0)
        break;

      if (sector_idx != NO_BLOCK) { 
        block_read_cache(fs_device, sector_idx, 
                         buffer + bytes_read, sector_ofs,
                         chunk_size, false, next);
      } else {
        memset (buffer + bytes_read, 0, chunk_size);
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

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
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  bool locked = false;
  struct lock *lock = lock_for_inode (inode);

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t next;
      block_sector_t sector_idx = byte_to_sector (inode, offset, &next);
      if (sector_idx == NO_BLOCK) {
        if (lock != NULL && !locked) {
          lock_acquire (lock);
          locked = true;
        }
        sector_idx = allocate_block (inode, offset / BLOCK_SECTOR_SIZE);

        if (sector_idx == NO_BLOCK)
          PANIC ("could not allocate file sector");
      }
      
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      block_write_cache(fs_device, sector_idx, buffer + bytes_written,
                        sector_ofs, chunk_size, false, next);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);
  if (inode_length (inode) < offset - 1) {
    if (lock != NULL && !locked) {
      lock_acquire (lock);
      locked = true;
    }
    inode->data.length = offset;
    block_write_cache (fs_device, inode->sector, &inode->data,
                       0, BLOCK_SECTOR_SIZE, true, -1);
 }

  if (lock != NULL && locked) lock_release (lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
