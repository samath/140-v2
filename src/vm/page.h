#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include "lib/kernel/hash.h"

enum supp_page_flags
  {
    SUPP_PAGE_TODO
  };

/* Supplemental page table entry */
struct supp_page
  {
    struct frame *frame;        // Pointer to the frame table entry
    uint32_t *pte;              // Pointer to the main page table entry
    struct hash_elem hash_elem; // For page table -> supp page table access
  };

#endif
