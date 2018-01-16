#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"

/* The size of the cache */
#define MAX_CACHE_SIZE 64
/* The intervall when the cache is flushed(in seconds). */
#define FLUSH_INTERVALL TIMER_FREQ*5

/* Type for the index of a cache block */
typedef int cache_block_t;

/* A cache sector, where a cached sector is put into. */
struct cache_block
{
  /* The disk sector which is currently cached. Only set if not free. */
  block_sector_t disk_sector;
  /* The data of the sector from the HDD. Only set if not free. */
  uint8_t disk_block[BLOCK_SECTOR_SIZE];
  /* Stores if this cache sector currently contains any sector. */
  bool free;
  /* Stores if this cache sector is dirty. */
  bool dirty;
  /* Stores how many processes have opened this cache sector. */
  unsigned opened_by;
  /* Stores if this cache sector was accessed. Used for eviction.  */
  bool accessed;
  /* Lock for the members of this struct */
  struct lock lock;
};

/* Struct for one read ahead job */
struct read_ahead_job
{
  /* Sector to be read ahead */
  block_sector_t disk_sector;
  /* List element to be added to the list */
  struct list_elem elem;
};

/* Array where the cached blocks are stored into. */
struct cache_block cache_blocks[MAX_CACHE_SIZE];
/* Lock for operations on cache. */
struct lock cache_lock;
/* Lock for the cache_running variable. */
struct lock caching_running_lock;
/* The queue for the read-ahead jobs. */
struct list read_ahead_queue;
/* Lock for the read-ahead queue */
struct lock read_ahead_queue_lock;
/* Semaphore used by the read-ahead thread to see if there are jobs(used to prevent busy-wait) */
struct semaphore read_ahead_sema;
/* Tells the read-ahead/write-behind threads if they should still run. */
bool caching_running;
/* An array with zeroes(used when we want to zero a block) */
char zeroes[BLOCK_SECTOR_SIZE];


void cache_init(void);
cache_block_t get_from_cache(block_sector_t, bool);
void read_cache_into_buffer(block_sector_t, int, void *, int, int);
void write_buffer_into_cache(block_sector_t, int, const void *, int, int);
void write_zeroes_into_cache(block_sector_t);
void read_cache_into_inode(block_sector_t, struct inode *);
void write_inode_into_cache(block_sector_t, struct inode *);
void create_inode_in_cache(block_sector_t sector, struct inode_blocks *, off_t, bool, block_sector_t, unsigned);
block_sector_t get_indirect_block_value(block_sector_t, off_t);
void set_indirect_block_value(block_sector_t, off_t, block_sector_t);
cache_block_t try_get_from_cache(block_sector_t);
void evict_cache_sector(void);
cache_block_t get_free_cache_block(void);
void thread_func_write_behind(void *);
void thread_func_read_ahead(void *);
void cache_complete_flush(bool);
void cache_sector_flush(unsigned, bool);
void add_read_ahead(block_sector_t );
void clear_read_ahead(void);


#endif /* filesys/cache.h */
