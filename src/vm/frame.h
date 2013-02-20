#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdint.h>

enum frame_flags
  {
    FRAME_TODO
  };

/* Frame table entry */
struct frame
  {
    uint32_t *paddr;            // Physical address of this frame table entry
    struct supp_page *ksp;      // Pointer to the kernel virtual page table entry
    struct supp_page *usp;      // Pointer to the user virtual page table entry
  };


void frame_init (void);

#endif
