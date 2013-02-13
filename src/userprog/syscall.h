#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int pid_t;

void syscall_init (void);
void syscall_release_files (void);

#endif /* userprog/syscall.h */
