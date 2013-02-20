#include "vm/frame.h"
#include "lib/kernel/list.h"

static struct list frame_table;

void
frame_init (void)
{
  list_init (&frame_table); 
}


