#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

void signal_parent (struct pinfo *pinfo);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_cleanup (int exit_code);
void process_exit (void);
void process_activate (void);

struct lock cleanup_lock;

#endif /* userprog/process.h */
