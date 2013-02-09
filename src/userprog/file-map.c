#include "userprog/file-map.h"
#include "filesys/file.h"

struct fpm_info {
  struct file *fp;
  int num_active;
  struct fpm_info* next;
};

struct fdm_info {
  struct file *fp;
  int fd;
  struct fdm_info *next;
};

struct file_map {
  struct fpm_info ** fp_map;
  struct fdm_info ** fd_map;
  int next_fd;
};

#define FD_TABLE_SIZE 16
#define FP_TABLE_SIZE 16
#define BASE_FD 2

struct file_map *init_file_map () {
  struct file_map *fm = malloc(sizeof(struct file_map));
  fm->fp_map = malloc(FP_TABLE_SIZE * sizeof(struct fpm_info *));
  int i = 0, j = 0;
  for(; i < FP_TABLE_SIZE; i++) fm->fp_map[i] = NULL;
  fm->fd_map = malloc(FD_TABLE_SIZE * sizeof(struct fdm_info *));
  for(; j < FD_TABLE_SIZE; j++) fm->fd_map[j] = NULL;
  fm->next_fd = BASE_FD;
}

void destroy_file_map (struct file_map *fm) {
  // NOT YET IMPLEMENTED
}


#define PRIME 37

static int hash (void * addr) {
  char* as_bytes = (char *)&addr;
  int result = 0, i = 3;
  for(; i >= 0; i--) 
    result = result * PRIME + (int) as_bytes[i];
  return result;
}

static struct fdm_info* fdm_from_fd (struct file_map *fm, int fd) {
  struct fdm_info * start = fm->fd_map[fd % FD_TABLE_SIZE];
  while(start) {
    if(start->fd == fd) return start;
    start = start->next;
  }
  return NULL;
}

static struct fpm_info* fpm_from_fp (struct file_map *fm, struct file *fp) {
  struct fpm_info * start = fm->fp_map[hash(fp)];
  while(start) {
    if(start->fp == fp) return start;
    start = start->next;
  }
  return NULL;
}
  
struct file* fp_from_fd (struct file_map *fm, int fd) {
  struct fdm_info* fdm = fdm_from_fd(fm, fd);
  if(fdm) return fdm->fp;
  else return NULL;
}

/* Finds the corresponding entry for fp in the fp_map.
   Increments num_active, or creates a new entry if none exists.
   Adds a new fdm_info to the fd_map.
   Returns the new file descriptor.
*/
int get_new_fd (struct file_map *fm, struct file *fp) {
  struct fpm_info * result = fpm_from_fp(fm, fp);
  if(result == NULL) {
    result = malloc(sizeof(struct fpm_info));
    result->fp = fp;
    result->num_active = 0;
    result->next = fm->fp_map[hash(fp)];
    fm->fp_map[hash(fp)] = result;
  }

  result->num_active++;
  int fd = fm->next_fd;

  struct fdm_info * new_fdm = malloc(sizeof(struct fdm_info));
  new_fdm->fp = fp;
  new_fdm->fd = fd;
  new_fdm->next = fm->fd_map[fd % FD_TABLE_SIZE];
  fm->fd_map[fd % FD_TABLE_SIZE] = new_fdm;

  fm->next_fd++;
  return fd;
}

/* Close a given file descriptor.
   Iterates over the stored fd_map and frees related memory.
   Decrements the num_active field for the corresponding file pointer.
   If num_active is 0, calls file_close on the file pointer.
*/
void close_fd (struct file_map *fm, int fd) {
  struct fdm_info *prev = fm->fd_map[fd % FD_TABLE_SIZE], *fdm = NULL;
  if(prev == NULL) return;
  if(prev->fd == fd) {
    fdm = prev;
    fm->fd_map[fd % FD_TABLE_SIZE] = fdm->next;
  } else {    
    while(prev->next) {
      if(prev->next->fd == fd) {
        fdm = prev->next;
        prev->next = fdm->next;
        break;
      }
      prev = prev->next;
    }
  }
  if(fdm == NULL) return;

  struct file* fp = fdm->fp;
  free(fdm);

  struct fpm_info *fpm = fpm_from_fp(fm, fp);
  if(fpm == NULL) return;
  fpm->num_active--;
  if(fpm->num_active == 0) {
    struct fpm_info *prev_fpm = fm->fp_map[hash(fp)];
    if(prev_fpm == fpm) {
      fm->fp_map[hash(fp)] = fpm->next;
    } else { 
      while(prev_fpm->next) {
        if(prev_fpm->next == fpm) {
          fpm = prev_fpm->next;
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
