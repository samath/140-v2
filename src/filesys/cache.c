#include <list.h>
#include <stdio.h>
#include <string.h>
#include "devices/block.h"
#include "devices/timer.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#define FLUSH_TIMER_MSECS 30000

/* Use an array to represent fixed-size cache */
static struct cache_entry cache[CACHE_SIZE];
static struct lock cache_lock;

static struct lock readahead_lock;
static struct condition readahead_wait;
static struct list readahead_list;

static int clock_hand;

/* Initialize all cache entries in the global filesys gache */
void
cache_init (void)
{
  lock_init(&cache_lock);
  clock_hand = 0;
  
  int i;
  for (i = 0; i<CACHE_SIZE; i++)
  {
    struct cache_entry *ce = &cache[i];
    ce->block_num = -1;
    ce->readahead = -1;
    ce->handlers = 0;
    ce->metadata = false;
    ce->priority = 0;
    ce->dirty = false;
    ce->fs = NULL;
    cond_init(&ce->cond);
  }

  lock_init(&readahead_lock);
  cond_init(&readahead_wait);
  list_init(&readahead_list);

  tid_t tid = thread_create("flush all thread", PRI_DEFAULT,
                            cache_timed_flush, NULL);
  if(tid == TID_ERROR)
    PANIC("Failed to start cache flushing thread in file system startup\n");
  
  tid = thread_create("readahead thread", PRI_DEFAULT,
                            cache_readahead, NULL);
  if(tid == TID_ERROR)
    PANIC("Failed to start readahead thread in file system startup\n");
}

/* Read a block of data utilizing caching and the read-ahead policy */
void
block_read_cache(struct block *fs, block_sector_t block_num, void * data,
                 uint32_t offset, uint32_t size, bool metadata,
                 block_sector_t next_block_num)
{
  lock_acquire(&cache_lock);
  struct cache_entry *ce;
  int cache_idx = search_cache(block_num);
  //If block is not in cache, evict one if necessary and load new block
  if(cache_idx == -1)
  {
    cache_idx = cache_eviction();
    cache_flush_block(cache_idx);
    ce = &cache[cache_idx];
    ce->block_num = block_num;
    ce->readahead = next_block_num;
    ce->metadata = metadata;
    ce->priority = (ce->metadata) ? 3 : 1;
    ce->dirty = false;
    ce->fs = fs;

    lock_release(&cache_lock);

    block_read(ce->fs, ce->block_num, ce->data);

    lock_acquire(&cache_lock);

  }
  else
  {
    ce = &cache[cache_idx];
    ce->priority = (ce->metadata) ? 3 : 1;
    ce->readahead = next_block_num;
  }

  memcpy(data, ce->data + offset, size);
  ce->handlers--;
  cond_broadcast(&ce->cond, &cache_lock);

  if((int)ce->readahead != -1)
  {
    add_readahead_order(ce->fs, next_block_num);
    ce->readahead = -1;
  }

  lock_release(&cache_lock);
}

/* Write a block of data utilizing caching and the read-ahead and write-behind
 * policy */
void
block_write_cache (struct block *fs, block_sector_t block_num,
                   const void * data, uint32_t offset, uint32_t size,
                   bool metadata, block_sector_t next_block_num)
{
  lock_acquire(&cache_lock);
  struct cache_entry *ce;
  int cache_idx = search_cache(block_num);
  //If block is not in cache, evict one if necessary and load new block
  if(cache_idx == -1)
  {
    cache_idx = cache_eviction();
    cache_flush_block(cache_idx);
    ce = &cache[cache_idx];
    ce->block_num = block_num;
    ce->readahead = next_block_num;
    ce->metadata = metadata;
    ce->priority = (ce->metadata) ? 3 : 1;
    ce->dirty = false;
    ce->fs = fs;

    lock_release(&cache_lock);

    block_read(ce->fs, ce->block_num, ce->data);

    lock_acquire(&cache_lock);

  }
  else
  {
    ce = &cache[cache_idx];
    ce->priority = (ce->metadata) ? 3 : 1;
    ce->readahead = next_block_num;
  }

  memcpy(ce->data + offset, data, size);
  ce->dirty = true;
  ce->handlers--;
  cond_broadcast(&ce->cond, &cache_lock);

  if((int)ce->readahead != -1)
  {
    add_readahead_order(ce->fs, next_block_num);
    ce->readahead = -1;
  }

  lock_release(&cache_lock);
}

/* Search the cache for the specificed block. If found, return the index.
 * Otherwise return -1. */
int
search_cache (block_sector_t block_num)
{
  int i;
  for (i = 0; i<CACHE_SIZE; i++)
  {
    if(cache[i].block_num == block_num)
    {
      while(cache[i].handlers > 0)
        cond_wait(&cache[i].cond, &cache_lock);
      if(cache[i].block_num == block_num)
      {
        cache[i].handlers++;
        return i;
      }
      else
        i = -1;
    }
    else if(cache[i].readahead == block_num)
    {
      /* The desired block is being read in by the readahead thread. Wait
       * patiently for it to be inserted into cache and restart search. */
      cond_wait(&cache[i].cond, &cache_lock);
      i = -1;
    }
  }
  /* Block not found in cache */
  return -1;
}

/* Evict an entry from the cache and return the index. Use the cache_entry
 * priority which prioritizes metadata over file data when checking whether
 * a file has been recently accessed. If the cache is not full, return the
 * index of an open slot. */
int
cache_eviction (void)
{
  int evict = -1;
  while(true)
  {
    struct cache_entry *ce = &cache[clock_hand];
    if(ce->handlers > 0)
      continue;
    else if(ce->priority > 0)
      ce->priority--;
    else
    {
      evict = clock_hand;
    }

  /* Move clock hand to next slot. Reset to 0 when necessary */
  clock_hand++;
  if(clock_hand == CACHE_SIZE)
    clock_hand = 0;
  
  if(evict != -1)
    break;
  }
  return evict;
}

/* Force a flush of all blocks in the cache */
void
cache_flush_all (void)
{
  lock_acquire(&cache_lock);
  int i = 0;
  for(i = 0; i<CACHE_SIZE; i++)
  {
    cache_flush_block(i);
    cache[i].handlers--;
    cond_broadcast(&cache[i].cond, &cache_lock);
  }
  lock_release(&cache_lock);
}

/* Force the flush of a specified block in the cache */
void
cache_flush_block (int cache_idx)
{
  struct cache_entry *ce = &cache[cache_idx];
  while(ce->handlers > 0)
    cond_wait(&ce->cond, &cache_lock);
  ce->handlers++;

  if(ce->dirty)
  {
    lock_release(&cache_lock);
    block_write(ce->fs, ce->block_num, ce->data);
    lock_acquire(&cache_lock);
    ce->dirty = false;
  }
}

/* Forces a flush of the entire cache every 30 seconds */
void
cache_timed_flush(void *arg UNUSED)
{
  while(true)
  {
    timer_msleep(FLUSH_TIMER_MSECS);
    cache_flush_all();
  }
}

/* Adds a block that needs to be read in to the pending read-ahead list */
void
add_readahead_order (struct block *fs, block_sector_t next_block_num)
{
  return;
  struct readahead_order *order = malloc(sizeof(struct readahead_order));
  if(order == NULL)
    PANIC("Failed to malloc readahead order in filesys\n");
  order->block_num = next_block_num;
  order->fs = fs;

  lock_acquire(&readahead_lock);
  list_push_back(&readahead_list, &order->elem);
  cond_signal(&readahead_wait, &readahead_lock);
  lock_release(&readahead_lock);
}

/* Background function to bring read-ahead blocks into cache */
void
cache_readahead (void *arg UNUSED)
{
  char data[1];
  struct readahead_order *order;
  lock_acquire(&readahead_lock);
  while(true)
  {
    while(list_empty(&readahead_list))
      cond_wait(&readahead_wait, &readahead_lock);

    while(!list_empty(&readahead_list))
    {
      order = (struct readahead_order *)list_pop_front(&readahead_list);
      block_read_cache(order->fs, order->block_num, data, 0, 0, false, -1);
      free(order);
    }

  }
  lock_release(&readahead_lock);
}
