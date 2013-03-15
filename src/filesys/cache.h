#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_SIZE 64

/* An entry in the cache. Contains a block worth of data from disk */
struct cache_entry
{
  char data[BLOCK_SECTOR_SIZE];  //The block of data loaded into the cache
  block_sector_t block_num;      //The corresponding block number on disk
  block_sector_t readahead;      //The block being read in by read-ahead
  int handlers;                  //How many threads are using this entry
  bool metadata;                 //Does this cache entry contain metadata
  int priority;                  //Decides when to evict entry from the cache
  bool dirty;                    //Does the data in cache differ from the disk
  struct block *fs;              //The file system blocks

  struct condition cond;         //Signals threads waiting on this entry
};

/* A pending disk IO order to read in a subsequent block */
struct readahead_order
{
  block_sector_t block_num;  //The block of the data to be read in
  struct block *fs;          //The file system blocks
  struct list_elem elem;     //Used to add this order to any other orders
};

void cache_init (void);
void block_read_cache (struct block *fs, block_sector_t block_num,
                       void * data, uint32_t offset, uint32_t size,
                       bool metadata, block_sector_t next_block_num);
void block_write_cache (struct block *fs, block_sector_t block_num,
                        const void * data, uint32_t offset, uint32_t size,
                        bool metadata, block_sector_t next_block_num);

int search_cache (block_sector_t block_num);
void cache_flush_block (int cache_idx);
void cache_flush_all (void);
int cache_eviction (void);

void cache_timed_flush (void *arg UNUSED);
void cache_readahead (void *arg UNUSED);
void add_readahead_order (struct block *fs, block_sector_t next_block_num);
#endif
