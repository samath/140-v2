#ifndef USERPROG_FILE_MAP
#define USERPROG_FILE_MAP

#include <stdio.h>
#include "threads/synch.h"
#include "filesys/inode.h"

struct file_map;
struct file_with_lock {
  struct file *fp;
  struct lock *lock;
};

void init_file_map (void);
void destroy_file_map (void);

/* Get a lock for an inode. */
struct lock* lock_for_inode (struct inode *inode);
/* Return the file to which a file descriptor refers. */
struct file_with_lock fwl_from_fd (int fd);
/* Return an unused file descriptor for f. */
int get_new_fd (struct file *f);
/* Close a given file descriptor. 
   If the file has no remaining descriptors, close the file. */
void close_fd (int fd);

/* Close all file descriptors belonging to current thread. */
void close_fd_for_thread (void);

#endif
