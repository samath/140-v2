#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
// Needed for call in exception.c. Wraps access to file map.
void syscall_release_files (void);

#endif /* userprog/syscall.h */
