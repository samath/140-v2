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
static void *uptr_valid (void *uptr);
static void *str_valid (void *str);
static void *buffer_valid (void *buffer, unsigned size);

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


static enum SYSCALL_NUMBER syscall_first_call = SYS_HALT;
static enum SYSCALL_NUMBER syscall_last_call = SYS_INUMBER;
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

#define PTR_SIZE (sizeof(uint32_t *))

static void
syscall_handler (struct intr_frame *f) 
{
  /* Validate the first addr of the stack frame */
  void *esp = uptr_valid (f->esp);
  if (esp == NULL) {
    syscall_exit (-1);
    return;
  }
  
  enum SYSCALL_NUMBER call_number = *(enum SYSCALL_NUMBER *) esp;
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
    void *vaddr = uptr_valid((uint32_t *) f->esp + 1 + i);
    if (vaddr == NULL) {
      syscall_exit (-1);
      return;
    }
    /* Translate the argument to kernel virtual (== physical) memory */
    argbuf[i] = *(uint32_t *) vaddr;
  }
  
  int retval = 0;

  /* Switch based on call_number to delegate to corresponding syscall.
     Have not implemented several syscalls as of this project. 
     Use validation methods to check user-provided arguments.
  */
  switch (call_number) {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      syscall_exit ((int) argbuf[0]);
      break;
    case SYS_EXEC:
      if (str_valid ((void *) argbuf[0]) == NULL) {
        syscall_exit (-1);
        return;
      }
      retval = syscall_exec ((char *) argbuf[0]);
      break;
    case SYS_WAIT:
      retval = syscall_wait ((int) argbuf[0]);
      break;
    case SYS_CREATE:
      if (str_valid ((char *) argbuf[0]) == NULL) {
        syscall_exit (-1);
        return;
      }
      retval = (int) syscall_create ((char *) argbuf[0],
                                     (unsigned) argbuf[1]);
      break;
    case SYS_REMOVE:
      if (!uptr_valid ((char *) argbuf[0])) {
        syscall_exit (-1);
        return;
      }
      retval = (int) syscall_remove ((char *) argbuf[0]);
      break;
    case SYS_OPEN:
      if (str_valid ((char *) argbuf[0]) == NULL) {
        syscall_exit (-1);
        return;
      }
      retval = (int) syscall_open ((char *) argbuf[0]);
      break;
    case SYS_FILESIZE:
      retval = syscall_filesize ((int) argbuf[0]);
      break;
    case SYS_READ:
      if (buffer_valid ((void *) argbuf[1], (unsigned) argbuf[2]) == NULL) {
        syscall_exit (-1);
        return;
      }
      retval = syscall_read ((int) argbuf[0],
                             (void *) argbuf[1],
                             (unsigned) argbuf[2]);
      break;
    case SYS_WRITE:
      if (buffer_valid ((void *) argbuf[1], (unsigned) argbuf[2]) == NULL) {
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
  syscall_release_files ();
  process_cleanup (status);
  thread_exit ();
}

// Accessible in syscall.h for access to the syscall-handler's file map.
void syscall_release_files ()
{
  close_fd_for_thread (fm);
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


/* Checks to see if a user pointer is valid by (a)
   checking all four bytes are below PHYS_BASE and (b) 
   an entry exists in the page table. */
static void *uptr_valid (void *uptr) {
  return buffer_valid (uptr, sizeof (uint32_t *));
}

/* Iterates through a string virtual address char by char to
   check that all of its memory addresses are valid. 
   Only updates page conversion for bytes in the argument str that
   have crossed a page boundary, to limit total calls to pagedir_get_page.
*/
static void *str_valid (void *str) {
  char *c;
  void *retval = NULL;
  while (true) {
    /* Translate the user virtual addr into a kernel virtual addr */
    c = (retval == NULL || (unsigned) str % PGSIZE == 0) ?
        ((char *) utok_addr(str)) : c + 1;
    if (c == NULL) return NULL;
    if (retval == NULL) retval = c;
    if (*c == '\0') return retval;
    str = ((char *) str) + 1;
  }
}

/* Iterates through a buffer virtual address page by page to check
   all of its memory addresses are valid. size is in bytes. 
   Calculates the maximum user virtual page address, then all pages 
   in between maximum address and starting address.
*/
static void *buffer_valid (void *buffer, unsigned size) {
  void *retval = utok_addr (buffer);
  if (retval == NULL) return NULL;

  // Starting address of maximum page read by buffer.
  void * max_addr = (void *)
    ((((unsigned) buffer + size - 1) / PGSIZE) * PGSIZE);
  
  /* Step through page-by-page */
  for(; max_addr > buffer; max_addr = (char *)max_addr - PGSIZE) {
    if (utok_addr (max_addr) == NULL)
      return NULL;
  }
  return retval;
}


