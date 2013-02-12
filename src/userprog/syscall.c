#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/file-map.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/syscall-nr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "pagedir.h"
#include "threads/vaddr.h"

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

static void *utok_addr (void *uptr);
static bool uaddr_valid (void *uptr);
static bool str_valid (void *str);
static bool buffer_valid (void *buffer, unsigned size);

struct file_map *fm;
struct lock filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fm = init_file_map ();
  if (fm == NULL) {
    syscall_exit (-1);
    return;
  }
  lock_init (&filesys_lock);
  lock_init (&cleanup_lock);
}

#define argval(INTR_FRAME, TYPE, ARG)       \
  (*(( TYPE * ) ((uint32_t *) INTR_FRAME->esp + ARG )))

static enum SYSCALL_NUMBER syscall_first_call = SYS_HALT;
static enum SYSCALL_NUMBER syscall_last_call = SYS_CLOSE;
static int syscall_argc[] =
  {
    0,  // Halt
    1,  // Exit
    1,  // Exec
    1,  // Wait
    2,  // Create
    1,  // Remove
    1,  // Open
    1,  // Filesz
    3,  // Read
    3,  // Write
    2,  // Seek
    1,  // Tell
    1   // Close
  };

static void
syscall_handler (struct intr_frame *f) 
{
  /* Validate the first addr of the stack frame */
  if (!uaddr_valid ((uint32_t *)(f->esp))) {
    syscall_exit (-1);
    return;
  }
  
  enum SYSCALL_NUMBER call_number = *(enum SYSCALL_NUMBER *) f->esp;
  if (call_number < syscall_first_call || call_number > syscall_last_call) {
    syscall_exit (-1);
    return;
  }

  /* Buffer the arguments for validation */
  int argc = syscall_argc[call_number];
  uint32_t argbuf[3];

  int i = 0;
  for (; i < argc; i++) {
    /* Validate each argument  */
    uint32_t *arg_addr = (uint32_t *)(f->esp) + 1 + i;
    if (!uaddr_valid (arg_addr)) {
      syscall_exit (-1);
      return;
    }
    argbuf[i] = *arg_addr;
  }
  
  int retval = 0;

  switch (call_number) {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      syscall_exit ((int) argbuf[0]);
      break;
    case SYS_EXEC:
      if (false && !str_valid (argval(f, char *, 0))) {
        syscall_exit (-1);
        return;
      }
      retval = syscall_exec (argval(f, char *, 1));
      break;
    case SYS_WAIT:
      retval = syscall_wait ((int) argbuf[0]);
      break;
    case SYS_CREATE:
      if (!uaddr_valid ((char *) argbuf[0])) {
        syscall_exit (-1);
        return;
      }
      retval = (int) syscall_create ((char *) argbuf[0],
                                     (unsigned) argbuf[1]);
      break;
    case SYS_REMOVE:
      if (!uaddr_valid ((char *) argbuf[0])) {
        syscall_exit (-1);
        return;
      }
      retval = (int) syscall_remove ((char *) argbuf[0]);
      break;
    case SYS_OPEN:
      if (!uaddr_valid ((char *) argbuf[0])) {
        syscall_exit (-1);
        return;
      }
      retval = (int) syscall_open ((char *) argbuf[0]);
      break;
    case SYS_FILESIZE:
      retval = syscall_filesize ((int) argbuf[0]);
      break;
    case SYS_READ:
      if (!buffer_valid ((void *) argbuf[1], (unsigned) argbuf[2])) {
        syscall_exit (-1);
        return;
      }
      retval = syscall_read ((int) argbuf[0],
                             (void *) argbuf[1],
                             (unsigned) argbuf[2]);
      break;
    case SYS_WRITE:
      if (!buffer_valid ((void *) argbuf[1], (unsigned) argbuf[2])) {
        syscall_exit (-1);
        return;
      }
      retval = syscall_write ((int) argbuf[0],
                             (void *) argbuf[1],
                             (unsigned) argbuf[2]);
      break;
    case SYS_SEEK:
      syscall_seek ((int) argbuf[0], (unsigned) argbuf[1]);
      break;
    case SYS_TELL:
      retval = (int) syscall_tell ((int) argbuf[0]);
      break;
    case SYS_CLOSE:
      syscall_close ((int) argbuf[0]);
      break;
    default:
      printf("unhandled system call!\n");
      thread_exit();
  }

  f->eax = retval;
}

static void syscall_halt ()
{
  shutdown_power_off ();
}


static void syscall_exit (int status)
{
  close_fd_for_thread (fm);
  process_cleanup (status);
  thread_exit ();
}

static pid_t syscall_exec (const char *cmd)
{
  tid_t tid = process_execute (cmd);
  return (tid == TID_ERROR) ? -1 : tid;
}

static int syscall_wait (pid_t pid)
{
  return process_wait (pid);
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
  int retval = (fp) ? get_new_fd (fm, fp) : -1;
  lock_release (&filesys_lock);
  return retval;
}

static int syscall_filesize (int fd)
{ 
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  if (fwl.lock == NULL) return -1;
  lock_acquire (fwl.lock);
  int retval = file_length (fwl.fp);
  lock_release (fwl.lock);
  return retval;
}

static int syscall_read (int fd, void *buffer, unsigned size)
{
  if (fd == STDIN_FILENO) {
    unsigned int i = 0;
    for (; i < size; i++)
      *((char *) buffer + size) = input_getc();
    return 0;
  }
    
  struct file_with_lock fwl = fwl_from_fd (fm, fd);

  int retval = -1;
  if (fwl.fp) {
    lock_acquire (fwl.lock);
    retval = file_read (fwl.fp, buffer, size);
    lock_release (fwl.lock);
  }

  return retval;
}

#define PUTBUF_MAX 512 // Max number of bytes to write at a time to console

static int syscall_write (int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO) { 
    unsigned offset = 0;
    for (; offset < size; offset += PUTBUF_MAX) {
      putbuf (buffer + offset, 
        (size - offset > PUTBUF_MAX ? PUTBUF_MAX : size - offset));
    }
    return 0;
  }

  struct file_with_lock fwl = fwl_from_fd (fm, fd);

  int retval = -1;
  if (fwl.fp) {
    lock_acquire (fwl.lock);
    retval = file_write (fwl.fp, buffer, size);
    lock_release (fwl.lock);
  }

  return retval;
}

static void syscall_seek (int fd, unsigned position)
{
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  if (fwl.lock == NULL) {
    syscall_exit (-1);
    return;
  }
  lock_acquire (fwl.lock);
  file_seek (fwl.fp, position);
  lock_release (fwl.lock);
}

static unsigned syscall_tell (int fd) 
{
  struct file_with_lock fwl = fwl_from_fd (fm, fd);
  if (fwl.lock == NULL) {
    syscall_exit (-1);
    return 1;
  }
  lock_acquire (fwl.lock);
  unsigned retval = file_tell (fwl.fp);
  lock_release (fwl.lock);
  return retval;
}

static void syscall_close (int fd)
{
  close_fd (fm, fd);
}


/* Convert a user virtual addr into a kernel virtual addr.
   Return NULL if the mapping is absent or uaddr is not a user addr. */
static void *utok_addr (void *uptr) {
  if (!is_user_vaddr (uptr)) return NULL;
  return pagedir_get_page (thread_current ()->pagedir, uptr);
}


/* Checks to see if a user virtual address is valid by (a)
   checking it's below PHYS_BASE and (b) an entry exists
   in the page table. */
static bool uaddr_valid (void *uptr) {
  return utok_addr (uptr) != NULL;
}

/* Iterates through a string character by character to
   check that all of its memory addresses are valid. */
static bool str_valid (void *str) {
  char *c;
  while (true) {
    /* Translate the user virtual addr into a kernel virtual addr */
    c = (char *) utok_addr(str);
    if (c == NULL) return false;
    if (*c == '\0') return true;
    str = ((char *) str) + 1;
  }
}

/* Iterates through a buffer page-by-page to check that
   all of its memory addresses are valid. */
static bool buffer_valid (void *buffer, unsigned size) {
  /* Check front and end */
  if (!uaddr_valid (buffer)) return false;
  if (size != 0 && !uaddr_valid ((char *) buffer + size - 1)) return false;

  /* Step through page-by-page */
  unsigned i = 0;
  for(; i * PGSIZE < size; i++) {
    if (!uaddr_valid ((char *) buffer + i * PGSIZE)) return false;
  }
  return true;
}


