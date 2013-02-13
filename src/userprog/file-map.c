#include "userprog/file-map.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Struct uniquely represents an open file. */
struct fpm_info {
  struct file *fp;
  int num_active;       /* Number of open file descriptors for fp. */
  struct fpm_info* next;
  struct lock file_lock;
};

/* Struct uniquely represents an open file descriptor. */
struct fdm_info {
  struct file *fp;
  tid_t thread_id;      /* Id of owning thread. */
  int fd;
  struct fdm_info *next;
};

struct file_map {
  struct fpm_info ** fp_map;
  struct fdm_info ** fd_map;
  int next_fd;
  struct lock file_map_lock;
};

static struct file_with_lock get_file_with_lock (struct fpm_info *fpm);
static int hash (void *addr);
static struct fdm_info* fdm_from_fd (struct file_map *fm, int fd);
static struct fpm_info* fpm_from_fp (struct file_map *fm, struct file *fp);
static struct file* fp_from_fd (struct file_map *fm, int fd);
static void free_fdm (struct file_map *fm, struct fdm_info *fdm);


/* Convert an fpm into a struct containing a file and corresponding lock.
   If fpm == NULL (which represents a number of error conditions), 
   initialize all fields to NULL. */
static struct file_with_lock get_file_with_lock (struct fpm_info *fpm) 
{
  struct file_with_lock fwl;
  if (fpm) {
    fwl.fp = fpm->fp;
    fwl.lock = &(fpm->file_lock);
  } else {
    fwl.fp = NULL; 
    fwl.lock = NULL;
  }
  return fwl;
}

#define FD_TABLE_SIZE 32
#define FP_TABLE_SIZE 32
#define BASE_FD 2

/* Called in syscall_init to set up empty hash maps. */
struct file_map *init_file_map () 
{
  struct file_map *fm = malloc(sizeof(struct file_map));
  if (fm == NULL) return NULL;
  fm->fp_map = malloc(FP_TABLE_SIZE * sizeof(struct fpm_info *));
  fm->fd_map = malloc(FD_TABLE_SIZE * sizeof(struct fdm_info *));
  if (fm->fp_map == NULL || fm->fd_map == NULL) {
    free(fm->fd_map);
    free(fm->fp_map);
    free(fm);
    return NULL;
  }

  int i = 0, j = 0;
  for(; i < FP_TABLE_SIZE; i++) fm->fp_map[i] = NULL;
  for(; j < FD_TABLE_SIZE; j++) fm->fd_map[j] = NULL;
  fm->next_fd = BASE_FD;
  lock_init (&(fm->file_map_lock));
  return fm;
}

void destroy_file_map (struct file_map *fm) 
{
  int i = 0, j = 0;
  for(; i < FP_TABLE_SIZE; i++) {
    struct fpm_info *fpm = fm->fp_map[i];
    while(fpm) {
      struct fpm_info *next = fpm->next;
      free(fpm);
      fpm = next;
    }
  }
  free(fm->fp_map);
  for(; j < FD_TABLE_SIZE; j++) {
    struct fdm_info *fdm = fm->fd_map[j];
    while(fdm) {
      struct fdm_info *next = fdm->next;
      free(fdm);
      fdm = next;
    }
  }
  free(fm->fd_map);
  free(fm);
}


#define PRIME 37

static int hash (void * addr) 
{
  char* as_bytes = (char *)&addr;
  int result = 0, i = 3;
  for(; i >= 0; i--) 
    result = result * PRIME + (int) as_bytes[i];
  return result % FP_TABLE_SIZE;
}

/* Walk the FD_TABLE to find the fdm_info corresponding to fd.
   Return NULL if none are found. */
static struct fdm_info* fdm_from_fd (struct file_map *fm, int fd) 
{
  struct fdm_info * start = fm->fd_map[fd % FD_TABLE_SIZE];
  while(start) {
    if(start->fd == fd) {
      if(start->thread_id == thread_current ()->tid)
        return start;
      else return NULL;
    }
    start = start->next;
  }
  return NULL;
}

/* Walk the FP_TABLE to find the fpm_info corresponding to fp.
   Return NULL if none are found. */
static struct fpm_info* fpm_from_fp (struct file_map *fm, struct file *fp) 
{
  if (fp == NULL) return NULL;
  struct fpm_info * start = fm->fp_map[hash(fp)];
  while(start) {
    if(start->fp == fp) return start;
    start = start->next;
  }
  return NULL;
}
  
/*  Use the FD_TABLE to check if fd is valid. If so, return the file. */
static struct file* fp_from_fd (struct file_map *fm, int fd) 
{
  struct fdm_info* fdm = fdm_from_fd(fm, fd);
  if(fdm) return fdm->fp;
  else return NULL;
}

/* Return file and lock for a given file descriptor. */
struct file_with_lock fwl_from_fd (struct file_map *fm, int fd) 
{
  lock_acquire (&(fm->file_map_lock));
  struct fpm_info *fpm  = fpm_from_fp(fm, fp_from_fd(fm, fd));
  lock_release (&(fm->file_map_lock));
  return get_file_with_lock (fpm);
}

/* Finds the corresponding entry for fp in the fp_map.
   Increments num_active, or creates a new entry if none exists.
   Adds a new fdm_info to the fd_map.
   Returns the new file descriptor.
*/
int get_new_fd (struct file_map *fm, struct file *fp) 
{ 
  struct fdm_info * new_fdm = malloc(sizeof(struct fdm_info));
  if (new_fdm == NULL) return -1;

  lock_acquire (&(fm->file_map_lock));
  struct fpm_info * result = fpm_from_fp(fm, fp);
  if(result == NULL) {  
    result = malloc(sizeof(struct fpm_info));
    if (result == NULL) {
      lock_release (&(fm->file_map_lock));
      free (new_fdm);
      return -1;
    }
    // No existing file descriptors, initialize new fpm_info
    result->fp = fp;
    result->num_active = 0;
    result->next = fm->fp_map[hash(fp)];
    lock_init (&(result->file_lock));
    fm->fp_map[hash(fp)] = result;
  }

  result->num_active++;
  int fd = fm->next_fd;
  
  // Create new fdm_info
  new_fdm->fp = fp;
  new_fdm->fd = fd;
  new_fdm->thread_id = thread_current ()->tid;
  new_fdm->next = fm->fd_map[fd % FD_TABLE_SIZE];
  fm->fd_map[fd % FD_TABLE_SIZE] = new_fdm;

  fm->next_fd++;
  lock_release (&(fm->file_map_lock));
  return fd;
}

/* Close a given file descriptor.
   Iterates over the stored fd_map and frees related memory.
   Decrements the num_active field for the corresponding file pointer.
   If num_active is 0, calls file_close on the file pointer.
*/
void close_fd (struct file_map *fm, int fd) 
{
  lock_acquire (&(fm->file_map_lock));
  struct fdm_info *prev = fm->fd_map[fd % FD_TABLE_SIZE], *fdm = NULL;
  if(prev == NULL) {
    lock_release (&(fm->file_map_lock));
    return;
  }
  // Check the first element in the bucket.
  if(prev->fd == fd) { 
    // File descriptor must belong to current thread.  
    // Otherwise return NULL.
    if(prev->thread_id != thread_current ()->tid) {
      lock_release (&(fm->file_map_lock));
      return;
    }
    fdm = prev;
    fm->fd_map[fd % FD_TABLE_SIZE] = fdm->next;
  } else {    
    while(prev->next) {  // Walk the linked list.
      if(prev->next->fd == fd) {
        if(prev->next->thread_id != thread_current ()->tid) {
          lock_release (&(fm->file_map_lock));
          return;
        }
        fdm = prev->next;
        prev->next = fdm->next;
        break;
      }
      prev = prev->next;
    }
  }
  if(fdm == NULL) {
    lock_release (&(fm->file_map_lock));
    return;
  }

  free_fdm (fm, fdm);
  lock_release (&(fm->file_map_lock));
}

/* Close all file descriptors belonging to current thread. */
void close_fd_for_thread (struct file_map *fm) 
{
  lock_acquire (&(fm->file_map_lock));

  tid_t tid = thread_current ()->tid;
  int i = 0;
  for(; i < FD_TABLE_SIZE; i++) {
    // Walk each bucket separately.
    struct fdm_info *prev = fm->fd_map[i], *next = NULL;
    // Remove all of the thread's fds from the front of the list
    while (prev && prev->thread_id == tid) {
      next = prev->next;
      fm->fd_map[i] = next;
      free_fdm (fm, prev);
      prev = next;
    }    
    // Remove all of the thread's fds from the interior of the list
    while(next) {
      if(next->thread_id == tid) {
        prev->next = next->next;
        free_fdm (fm, next);
      } else {
        prev = next;
      }
      next = prev->next;
    }
  }

  lock_release (&(fm->file_map_lock));
}

/* Free resources for a given file descriptor struct.
   Walk the FP_TABLE to decrement numActive for the given file.
   If numActive == 0, close the file and remove the fp_info.
*/
static void free_fdm (struct file_map *fm, struct fdm_info *fdm) 
{
  struct file* fp = fdm->fp;
  free(fdm);

  struct fpm_info *fpm = fpm_from_fp(fm, fp);
  if(fpm == NULL) {
    lock_release (&(fm->file_map_lock));
    return;
  }
  fpm->num_active--;
  if(fpm->num_active == 0) {
    // No remaining file descriptors for file; free and close.
    // Must rewalk the list to find previous element and patch.
    struct fpm_info *prev_fpm = fm->fp_map[hash(fp)];
    if(prev_fpm == fpm) {
      fm->fp_map[hash(fp)] = fpm->next;
    } else { 
      while(prev_fpm->next) {
        if(prev_fpm->next == fpm) {
          prev_fpm->next = fpm->next;
          break;
        }
        prev_fpm = prev_fpm->next;
      }
    }
    file_close (fpm->fp);
    free(fpm);
  }
}
