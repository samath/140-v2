#ifndef USERPROG_FILE_MAP
#define USERPROG_FILE_MAP

#include <stdio.h>
#include "threads/synch.h"
#include "filesys/inode.h"

struct file_map;

struct file_synch_status {
  struct lock lock;
  unsigned writers_waiting;
  unsigned readers_running;
  struct condition read_cond;
  struct condition write_cond;
};

void init_file_map (void);
void destroy_file_map (void);

/* Get a lock for an inode. */
struct file_synch_status* status_for_inode (struct inode *inode);
/* Return the file to which a file descriptor refers. */
struct file *fp_from_fd (int fd);
/* Return an unused file descriptor for f. */
int get_new_fd (struct file *f);
/* Close a given file descriptor. 
   If the file has no remaining descriptors, close the file. */
void close_fd (int fd);

/* Close all file descriptors belonging to current thread. */
void close_fd_for_thread (void);

#endif
