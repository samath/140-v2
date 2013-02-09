#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/file-map.h"

static void syscall_handler (struct intr_frame *);

static void halt (void);
static void exit (int status);
static pid_t exec (const char *cmd_line);
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);


struct file_map* fm;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fm = init_file_map ();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}

static void halt ()
{
  //NOT YET IMPLEMENTED
}

static void exit (int status)
{
  //NOT YET IMPLEMENTED
}

static pid_t exec (const char *cmd_line)
{
  //NOT YET IMPLEMENTED
}

static int wait (pid_t pid)
{
  //NOT YET IMPLEMENTED
}

static bool create (const char *file, unsigned initial_size)
{
  //NOT YET IMPLEMENTED
}

static bool remove (const char *file)
{
  //NOT YET IMPLEMENTED
}

static int open (const char *file)
{
  return get_new_fd (fm, file);
}

static int filesize (int fd)
{
  return file_length (fp_from_fd(fd));
}

static int read (int fd, void *buffer, unsigned size)
{
  return file_read (fp_from_fd(fm, fd), buffer, size);
}

static int write (int fd, void *buffer, unsigned size)
{
  return file_write (fp_from_fd(fm, fd), buffer, size);
}

static void seek (int fd, unsigned position)
{
  file_seek (fp_from_fd(fm, fd), position);
}

static unsigned tell (int fd) 
{
  return file_tell (fp_from_fd(fm, fd));
}

static void close (int fd)
{
  close_fd(fm, fd);
}

