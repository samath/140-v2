#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/file-map.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/syscall-nr.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "pagedir.h"

static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
static void syscall_exit (int status);
static pid_t syscall_exec (const char *cmd_line);
static int syscall_wait (pid_t pid);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, const void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);

static bool uaddr_valid (void *uptr);

struct file_map *fm;
struct lock filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fm = init_file_map ();
  lock_init (&filesys_lock);
}

#define argval(INTR_FRAME, TYPE, ARG)       \
  (*(( TYPE * ) INTR_FRAME->esp + ARG * sizeof(int)))

static void
syscall_handler (struct intr_frame *f) 
{
  enum SYSCALL_NUMBER call_number = *(enum SYSCALL_NUMBER *) f->esp;
  int retval = 0;
  switch (call_number) {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      syscall_exit (argval(f, int, 1));
      break;
    case SYS_EXEC:
      retval = syscall_exec (argval(f, char *, 1));
      break;
    case SYS_WAIT:
      retval = syscall_wait (argval(f, int, 1));
      break;
    case SYS_CREATE:
      retval = (int) syscall_create (argval(f, char *, 1), 
                                     argval(f, unsigned, 2));
      break;
    case SYS_REMOVE:
      retval = (int) syscall_remove (argval(f, char *, 1));
      break;
    case SYS_OPEN:
      retval = syscall_open (argval(f, char *, 1));
      break;
    case SYS_FILESIZE:
      retval = syscall_filesize (argval(f, int, 1));
      break;
    case SYS_READ:
      retval = syscall_read (argval(f, int, 1),
                             argval(f, void *, 2),
                             argval(f, unsigned, 3));
      break;
    case SYS_WRITE:
      retval = syscall_write (argval(f, int, 1),
                              argval(f, void *, 2),
                              argval(f, unsigned, 3));
      break;
    case SYS_SEEK:
      syscall_seek (argval(f, int, 1), argval(f, unsigned, 2));
      break;
    case SYS_TELL:
      retval = (int) syscall_tell (argval(f, int, 1));
      break;
    case SYS_CLOSE:
      syscall_close (argval(f, int, 1));
      break;
    default:
      printf("unhandled system call!\n");
      thread_exit();
  }

  *(int *)f->eax = retval;
}

static void syscall_halt ()
{
  shutdown_power_off ();
}

static void syscall_exit (int status)
{
  //NOT YET IMPLEMENTED
}

static pid_t syscall_exec (const char *cmd_line)
{
  //NOT YET IMPLEMENTED
}

static int syscall_wait (pid_t pid)
{
  //NOT YET IMPLEMENTED
}

static bool syscall_create (const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool retval = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return retval;
}

static bool syscall_remove (const char *file)
{
  lock_acquire (&filesys_lock);
  bool retval = filesys_remove (file);
  lock_release (&filesys_lock);
  return retval;
}

static int syscall_open (const char *file)
{
  lock_acquire (&filesys_lock);
  struct file* fp = filesys_open (file);
  int retval = get_new_fd (fm, fp);
  lock_release (&filesys_lock);
  return retval;
}

static int syscall_filesize (int fd)
{ 
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  lock_acquire (fwl.lock);
  int retval = file_length (fwl.fp);
  lock_release (fwl.lock);
  return retval;
}

static int syscall_read (int fd, void *buffer, unsigned size)
{
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  lock_acquire (fwl.lock);
  int retval = file_read (fwl.fp, buffer, size);
  lock_release (fwl.lock);
  return retval;
}

static int syscall_write (int fd, const void *buffer, unsigned size)
{
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  lock_acquire (fwl.lock);
  int retval = file_write (fwl.fp, buffer, size);
  lock_release (fwl.lock);
  return retval;
}

static void syscall_seek (int fd, unsigned position)
{
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  lock_acquire (fwl.lock);
  file_seek (fwl.fp, position);
  lock_release (fwl.lock);
}

static unsigned syscall_tell (int fd) 
{
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  lock_acquire (fwl.lock);
  unsigned retval = file_tell (fwl.fp);
  lock_release (fwl.lock);
  return retval;
}

static void syscall_close (int fd)
{
  close_fd(fm, fd);
}


static bool uaddr_valid (void *uptr) {
  return pagedir_get_page (thread_current ()->pagedir, uptr) != NULL;
}
