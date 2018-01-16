#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include <string.h>


/* Initialize cache */
void cache_init(void)
{

  lock_init(&cache_lock);
  lock_init(&read_ahead_queue_lock);
  lock_init(&caching_running_lock);
  sema_init(&read_ahead_sema, 0);
  list_init(&read_ahead_queue);

  /* Initialize every cache entry */
  unsigned i;
  for(i=0;i<MAX_CACHE_SIZE;i++)
  {
    lock_init(&cache_blocks[i].lock);
    cache_blocks[i].free = true;
    cache_blocks[i].dirty = false;
    cache_blocks[i].opened_by = 0;
    cache_blocks[i].accessed = false;
    cache_blocks[i].disk_sector = 0;
  }

  /* Initialize zeroes array */
  for(i=0;i<BLOCK_SECTOR_SIZE;i++)
    zeroes[i] = 0;

  /* Start write-behind and read-ahead threads */
  caching_running = true;
  thread_create("cache_write_behind", 0, thread_func_write_behind, NULL);
  thread_create("cache_read_ahead", 0, thread_func_read_ahead, NULL);

}

/* Gets disk_sector from disk into cache and writes given amount of bytes into the given buffer */
void read_cache_into_buffer(block_sector_t disk_sector, int sector_ofs, void * buffer, int buffer_ofs, int chunk_size)
{

  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  /*  */
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  uint8_t * disk_block = cache_blocks[cache_block_idx].disk_block;
  lock_release(&cache_blocks[cache_block_idx].lock);

  /* Write to buffer */
  memcpy(buffer + buffer_ofs, disk_block + sector_ofs, chunk_size);

  lock_acquire(&cache_blocks[cache_block_idx].lock);
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Gets disk_sector from disk into cache and writes the given amoutn of data from buffer into it */
void write_buffer_into_cache(block_sector_t disk_sector, int sector_ofs, const void * buffer, int buffer_ofs, int chunk_size)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  /* Write data to disk sector */
  memcpy(cache_blocks[cache_block_idx].disk_block + sector_ofs, buffer + buffer_ofs, chunk_size);
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  /* Set dirty bit, since written */
  cache_blocks[cache_block_idx].dirty = true;
  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Gets disk_sector from disk into cache and writes zeroes to it */
void write_zeroes_into_cache(block_sector_t disk_sector)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  lock_acquire(&cache_blocks[cache_block_idx].lock);
  uint8_t * disk_block = cache_blocks[cache_block_idx].disk_block;
  /* Writes zeroes to disk sector*/
  memcpy (disk_block, zeroes, BLOCK_SECTOR_SIZE);
  /* Set dirty bit, since written */
  cache_blocks[cache_block_idx].dirty = true;
  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Gets disk_sector from disk into cache and writes its data into the given inode */
void read_cache_into_inode(block_sector_t disk_sector, struct inode * inode)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  /* Writes meta data */
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  struct inode_disk * disk_inode = (struct inode_disk *) cache_blocks[cache_block_idx].disk_block;
  inode->parent = disk_inode->parent;
  inode->length = disk_inode->length;
  inode->read_length = disk_inode->length;
  inode->is_dir = disk_inode->is_dir;

  /* Write direct/indirect/doubly-indirect block structures */
  unsigned i;
  for(i=0;i<DIRECT_BLOCKS_COUNT;i++)
    inode->blocks.direct_block_sectors[i] = disk_inode->blocks.direct_block_sectors[i];
  inode->blocks.indirect_block_sector = disk_inode->blocks.indirect_block_sector;
  inode->blocks.doubly_indirect_block_sector = disk_inode->blocks.doubly_indirect_block_sector;

  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Gets disk_sector from disk into cache and writes content of inode into it */
void write_inode_into_cache(block_sector_t disk_sector, struct inode * inode)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  /* Write meta data */
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  struct inode_disk * disk_inode = (struct inode_disk *) cache_blocks[cache_block_idx].disk_block;
  disk_inode->parent = inode->parent;
  disk_inode->is_dir = inode->is_dir;
  disk_inode->length = inode->length;
  disk_inode->magic = INODE_MAGIC;

  /* Write direct/indirect/doubly-indirect block structures */
  unsigned i;
  for(i=0;i<DIRECT_BLOCKS_COUNT;i++)
    disk_inode->blocks.direct_block_sectors[i] = inode->blocks.direct_block_sectors[i];
  disk_inode->blocks.indirect_block_sector = inode->blocks.indirect_block_sector;
  disk_inode->blocks.doubly_indirect_block_sector = inode->blocks.doubly_indirect_block_sector;

  /* Set dirty bit, since written */
  cache_blocks[cache_block_idx].dirty = true;
  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Creates an inode with the given meta data for disk sector */
void create_inode_in_cache(block_sector_t disk_sector, struct inode_blocks * blocks, off_t length, bool is_dir, block_sector_t parent, unsigned magic)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  /* Set meta data */
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  struct inode_disk * disk_inode = (struct inode_disk *) cache_blocks[cache_block_idx].disk_block;
  disk_inode->parent = parent;
  disk_inode->is_dir = is_dir;
  disk_inode->length = length;
  disk_inode->magic = magic;

  /* Write direct/indirect/doubly-indirect block structures */
  unsigned i;
  for(i=0;i<DIRECT_BLOCKS_COUNT;i++)
    disk_inode->blocks.direct_block_sectors[i] = blocks->direct_block_sectors[i];
  disk_inode->blocks.indirect_block_sector = blocks->indirect_block_sector;
  disk_inode->blocks.doubly_indirect_block_sector = blocks->doubly_indirect_block_sector;

  /* Set dirty bit, since written */
  cache_blocks[cache_block_idx].dirty = true;
  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Set the value of an indirect bock on disk sektor at offset */
void set_indirect_block_value(block_sector_t disk_sector, off_t offset, block_sector_t value)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);

  block_sector_t temp_array[128];
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  memcpy(temp_array, cache_blocks[cache_block_idx].disk_block, 512);
  temp_array[offset] = value;
  memcpy(cache_blocks[cache_block_idx].disk_block, temp_array, 512);
  cache_blocks[cache_block_idx].dirty = true;
  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

}

/* Gets the value of an indirect bock on disk sektor at offset */
block_sector_t get_indirect_block_value(block_sector_t disk_sector, off_t offset)
{
  /* Get disk sector from disk into cache */
  cache_block_t cache_block_idx = get_from_cache(disk_sector, true);
  block_sector_t temp_array[128];

  lock_acquire(&cache_blocks[cache_block_idx].lock);
  memcpy(temp_array, cache_blocks[cache_block_idx].disk_block, 512);
  block_sector_t value = temp_array[offset];
  /* Decrement opene_by, now this cache block can be evicted again */
  cache_blocks[cache_block_idx].opened_by--;
  lock_release(&cache_blocks[cache_block_idx].lock);

  return value;
}

/* Gets disk sector from disk from cache. If not loaded, load it. */
cache_block_t get_from_cache(block_sector_t disk_sector, bool used_after)
{

  lock_acquire(&cache_lock);
  /* Try to get the cache block */
  cache_block_t cache_block_idx = try_get_from_cache(disk_sector);

  /* If no cache block for disk sector is in cache, get free cache block
     and read it into it. */
  if(cache_block_idx == -1)
  {
    cache_block_idx = get_free_cache_block();

    /* If no free cache block available, evict one. */
    while(cache_block_idx == -1)
    {
      evict_cache_sector();
      cache_block_idx = get_free_cache_block();
    }

    /* Set data for cache block */
    lock_acquire(&cache_blocks[cache_block_idx].lock);
    cache_blocks[cache_block_idx].disk_sector = disk_sector;
    block_read(fs_device, cache_blocks[cache_block_idx].disk_sector, cache_blocks[cache_block_idx].disk_block);
    lock_release(&cache_blocks[cache_block_idx].lock);

  }


  ASSERT(cache_block_idx != -1);
  lock_acquire(&cache_blocks[cache_block_idx].lock);
  ASSERT(!cache_blocks[cache_block_idx].free);

  cache_blocks[cache_block_idx].accessed = true;

  /* If cache block is used for reading/writing data from/to it used_after is
     set to true and we increment opene_by to be sure that it is not eviceted. */
  if(used_after)
    cache_blocks[cache_block_idx].opened_by++;


  lock_release(&cache_blocks[cache_block_idx].lock);
  lock_release(&cache_lock);
  return cache_block_idx;

}

/* Gets a cache block from cache for given disk sector. If not in cache,
   return -1. If in cache return cache block index. */
cache_block_t try_get_from_cache(block_sector_t disk_sector)
{
  cache_block_t i;

  for(i=0;i<MAX_CACHE_SIZE;i++)
  {
    lock_acquire(&cache_blocks[i].lock);
    if(!cache_blocks[i].free && cache_blocks[i].disk_sector == disk_sector)
    {
      lock_release(&cache_blocks[i].lock);
      return i;
    }
    lock_release(&cache_blocks[i].lock);
  }
  return -1;
}


/* Evicts a cache block. Use clock algoritm. */
void evict_cache_sector(void)
{

  cache_block_t i;
  cache_block_t curr_idx;

  /* Iterates twice over cache and tries to evict a cache block */
  for(i=0;i<2*MAX_CACHE_SIZE;i++)
  {

    curr_idx = i % MAX_CACHE_SIZE;
    lock_acquire(&cache_blocks[curr_idx].lock);

    /* If we already found a free block stop iteration */
    if(cache_blocks[curr_idx].free)
    {
      lock_release(&cache_blocks[curr_idx].lock);
      break;
    }

    /* If cache block in use, do not evict it */
    if(cache_blocks[curr_idx].opened_by > 0)
    {
      lock_release(&cache_blocks[curr_idx].lock);
      continue;
    }

    /* If cache block was accessed since last iteration, set it to not accessed. */
    if(cache_blocks[curr_idx].accessed)
    {
      cache_blocks[curr_idx].accessed = false;
    }
    else
    {
      /* If cache block is not accessed anymore since last iteration,
         evict block and stop iteration */
      if(cache_blocks[curr_idx].dirty)
        cache_sector_flush(curr_idx, false);
      cache_blocks[curr_idx].free = true;
      lock_release(&cache_blocks[curr_idx].lock);
      break;
    }
    lock_release(&cache_blocks[curr_idx].lock);
  }

}


/* Try to get a free block in cache. If there is space,
   returns cache block id, if not return -1. */
cache_block_t get_free_cache_block(void)
{
  cache_block_t i;

  for(i=0;i<MAX_CACHE_SIZE;i++)
  {
    lock_acquire(&cache_blocks[i].lock);
    if(cache_blocks[i].free)
    {
      cache_blocks[i].free = false;
      cache_blocks[i].dirty = false;
      lock_release(&cache_blocks[i].lock);
      return i;
    }
    lock_release(&cache_blocks[i].lock);
  }

  return -1;
}

/* Flushes the cache every FLUSH_INTERVALL as long as caching_running
   is true. Uses timer_sleep to prevent busy wait. */
void thread_func_write_behind(void * aux UNUSED)
{
  lock_acquire(&caching_running_lock);
  while(caching_running)
  {
    lock_release(&caching_running_lock);
    timer_sleep(FLUSH_INTERVALL);
    cache_complete_flush(false);
    lock_acquire(&caching_running_lock);
  }
}

/* Flushes the complete cache */
void cache_complete_flush(bool clear)
{
  cache_block_t i;
  lock_acquire(&cache_lock);
  for(i=0;i<MAX_CACHE_SIZE;i++)
  {
    lock_acquire(&cache_blocks[i].lock);
    if(!cache_blocks[i].free && cache_blocks[i].dirty)
      cache_sector_flush(i, clear);
    lock_release(&cache_blocks[i].lock);
  }
  lock_release(&cache_lock);
}

/* Removes a cache block for disk sector without writing it to disk. */
void cache_remove_sector(block_sector_t disk_sector)
{
  cache_block_t i;
  lock_acquire(&cache_lock);
  for(i=0;i<MAX_CACHE_SIZE;i++)
  {
    lock_acquire(&cache_blocks[i].lock);
    if(cache_blocks[i].disk_sector == disk_sector)
    {
      cache_blocks[i].free = true;
      cache_blocks[i].opened_by = 0;
      cache_blocks[i].accessed = false;
      cache_blocks[i].dirty = false;
    }
    lock_release(&cache_blocks[i].lock);
  }
  lock_release(&cache_lock);
}

/* Flushes the given cache block at index. If clear is set,
   cache block is also resetted. */
void cache_sector_flush(unsigned cache_block_idx, bool clear)
{
  ASSERT(lock_held_by_current_thread(&cache_blocks[cache_block_idx].lock));
  block_write(fs_device, cache_blocks[cache_block_idx].disk_sector, cache_blocks[cache_block_idx].disk_block);
  cache_blocks[cache_block_idx].dirty = false;
  if(clear)
  {
    cache_blocks[cache_block_idx].free = true;
    cache_blocks[cache_block_idx].opened_by = 0;
    cache_blocks[cache_block_idx].accessed = false;
  }
}

/* As long as caching_running is set to true,
   tries to get a new read-ahead job from the queue and
   reads it into the cache. Uses semaphore that is counted
   up when a new job is added, to prevent busy wait. */
void thread_func_read_ahead(void * aux UNUSED)
{

  block_sector_t next_sector_idx;
  struct read_ahead_job * job;

  lock_acquire(&caching_running_lock);
  while(caching_running)
  {
    lock_release(&caching_running_lock);

    /* Wait for a new job */
    sema_down(&read_ahead_sema);

    /* Check that there is a new job */
    lock_acquire(&read_ahead_queue_lock);
    if(list_empty(&read_ahead_queue))
    {
        lock_release(&read_ahead_queue_lock);
        continue;
    }

    /* Get new job and read it into cache */
    job = list_entry(list_pop_front(&read_ahead_queue), struct read_ahead_job, elem);
    lock_release(&read_ahead_queue_lock);
    next_sector_idx = job->disk_sector;
    free(job);
    get_from_cache(next_sector_idx, false);

    lock_acquire(&caching_running_lock);
  }

}

/* Adds a read-ahead job into the queue.
   Counts semaphore up to signalize that there is a new job. */
void add_read_ahead(block_sector_t disk_sector)
{

  ASSERT(disk_sector != 0);
  struct read_ahead_job * job = malloc(sizeof(struct read_ahead_job));
  ASSERT(job);
  job->disk_sector = disk_sector;

  lock_acquire(&read_ahead_queue_lock);
  list_push_back(&read_ahead_queue, &job->elem);
  lock_release(&read_ahead_queue_lock);

  sema_up(&read_ahead_sema);
}

/* Free all resources allocated for the read-ahead queue
   including all jobs. */
void clear_read_ahead(void)
{
  lock_acquire(&caching_running_lock);
  caching_running = false;
  lock_release(&caching_running_lock);

  struct list_elem *e;
  lock_acquire(&read_ahead_queue_lock);
  while (!list_empty (&read_ahead_queue))
  {
    e = list_pop_front (&read_ahead_queue);
    struct read_ahead_job * job = list_entry (e, struct read_ahead_job, elem);
    ASSERT(job != NULL);
    free(job);
  }
  lock_release(&read_ahead_queue_lock);

}
