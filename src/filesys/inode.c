#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (struct inode *inode, off_t pos)
 {
   ASSERT (inode != NULL);
   if (pos >= inode_length(inode))
     return -1;

   unsigned idx = pos / BLOCK_SECTOR_SIZE;
   lock_acquire(&inode->lock);
   block_sector_t bt = get_block_for_index(&(inode->blocks), idx);
   lock_release(&inode->lock);

   if(bt == 0)
    return -1;
   return bt;
}

/* Traverses the block structure of an inode an get the block device sector
   for the given index. */
block_sector_t get_block_for_index(struct inode_blocks * inode_blocks, unsigned idx)
{
    /* Index is within direct blocks */
    if(idx < DIRECT_BLOCKS_COUNT)
    {
      block_sector_t direct_block_sector = inode_blocks->direct_block_sectors[idx];
      return (direct_block_sector == 0 ? 0 : direct_block_sector);
    }

    /* Index is within indirect blocks */
    if(idx < (DIRECT_BLOCKS_COUNT + BLOCKS_PER_INDIRECT_BLOCK))
    {
      if(inode_blocks->indirect_block_sector == 0)
        return 0;
      block_sector_t indirect_block_idx = idx - DIRECT_BLOCKS_COUNT;
      /* Get block for index from indirect block */
      block_sector_t block_sector = get_indirect_block_value(inode_blocks->indirect_block_sector, indirect_block_idx);
      return (block_sector == 0 ? 0 : block_sector);
    }

    /* Index is within doubly indirect blocks */
    if(idx < (DIRECT_BLOCKS_COUNT + BLOCKS_PER_INDIRECT_BLOCK + BLOCKS_PER_INDIRECT_BLOCK * BLOCKS_PER_INDIRECT_BLOCK))
    {
      if(inode_blocks->doubly_indirect_block_sector == 0)
        return 0;
      int doubly_indirect_block_idx = (idx - DIRECT_BLOCKS_COUNT - BLOCKS_PER_INDIRECT_BLOCK) / BLOCKS_PER_INDIRECT_BLOCK;
      /* Get indirect block where index lies in */
      block_sector_t indirect_block_sector = get_indirect_block_value(inode_blocks->doubly_indirect_block_sector, doubly_indirect_block_idx);
      if(indirect_block_sector == 0)
        return 0;
      block_sector_t indirect_block_idx = idx - DIRECT_BLOCKS_COUNT - BLOCKS_PER_INDIRECT_BLOCK - BLOCKS_PER_INDIRECT_BLOCK * doubly_indirect_block_idx;
      /* Get block for index from indirect block */
      block_sector_t block_sector = get_indirect_block_value(indirect_block_sector, indirect_block_idx);
      return (block_sector == 0 ? 0 : block_sector);
    }

    return 0;

}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
/* Lock for access to open_inodes */
struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init(&open_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{

  bool success;
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);

  /* Initialize blocks and extends its block struture to the given size */
  struct inode_blocks blocks;
  unsigned i;
  for(i=0;i<DIRECT_BLOCKS_COUNT;i++)
    blocks.direct_block_sectors[i] = 0;
  blocks.indirect_block_sector = 0;
  blocks.doubly_indirect_block_sector = 0;
  success = inode_blocks_extend(&blocks, 0, length);

  /* Put just created inode into cache for writing it later onto the disk */
  if(success)
    create_inode_in_cache(sector, &blocks, length, is_dir, ROOT_DIR_SECTOR, INODE_MAGIC);

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode * inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes); e = list_next (e))
  {
    inode = list_entry (e, struct inode, elem);
    if (inode_get_inumber(inode) == sector)
      {
        inode_reopen (inode);
        lock_release(&open_inodes_lock);
        return inode;
      }
  }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);

  lock_init(&inode->extend_lock);
  lock_init(&inode->lock);

  lock_acquire(&inode->lock);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  /* Read meta data of inode from disk into the inode */
  read_cache_into_inode(sector, inode);

  lock_release(&inode->lock);
  lock_release(&open_inodes_lock);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
  {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}


/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, write the meta data
   of the inode to the cache for writing it later on the disk.
   If INODE was also a removed inode, remove cache entry for it
   and also free its resources(including its allocated blocks). */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire(&open_inodes_lock);

  if (inode_decrement_open_cnt(inode) == 0)
  {

    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);
    /* Deallocate blocks if removed, else
       write meta data into cache for writing it
       later onto the disk. */
    if (inode->removed)
      {
        cache_remove_sector(inode_get_inumber(inode));
        free_map_release (inode_get_inumber(inode), 1);
        lock_acquire(&inode->lock);
        inode_blocks_deallocate(&(inode->blocks), inode->length);
        lock_release(&inode->lock);
      }
      else
      {
        write_inode_into_cache(inode_get_inumber(inode), inode);
      }

    free (inode);
  }

  lock_release(&open_inodes_lock);

}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  off_t read_length;
  block_sector_t sector_idx;
  block_sector_t next_sector_idx;

  while (size > 0)
    {

      /* Check that the bytes we want to read are
         available in the file. Here, we use the read_length
         variable instead of the length variable to be sure that
         the bytes we want to read are really written. read_length is
         updates at the end of the write function, so we are sure the
         bytes were written. */
      read_length = inode_read_length(inode);
      if(offset >= read_length)
        return bytes_read;

      /* Get disk sector to read. */
      sector_idx = byte_to_sector (inode, offset);

      ASSERT(sector_idx != (block_sector_t)-1);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = read_length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read the bytes we want to read from the cache into the buffer */
      read_cache_into_buffer(sector_idx, sector_ofs, buffer, bytes_read, chunk_size);

      /* If there is a next block in the file start read ahead for it  */
      next_sector_idx = byte_to_sector (inode, offset+BLOCK_SECTOR_SIZE);
      if(next_sector_idx != (block_sector_t)-1)
        add_read_ahead(next_sector_idx);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   A write at end of file extends the inode. */
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  /* Check if writing is allowed for inode */
  lock_acquire(&inode->lock);
  if (inode->deny_write_cnt)
  {
    lock_release(&inode->lock);
    return 0;
  }
  lock_release(&inode->lock);

  /* Check if we want to write to a not allocated part of the file.
     If so, we extend the file. */
  lock_acquire(&inode->extend_lock);
  if( (offset + size) >= inode_length(inode))
  {
      off_t curr_len = inode_length(inode);
      lock_acquire(&inode->lock);
      bool success = inode_blocks_extend(&(inode->blocks), curr_len, offset + size);
      lock_release(&inode->lock);
      if(!success)
      {
        lock_release(&inode->extend_lock);
        return -1;
      }
      lock_acquire(&inode->lock);
      inode->length = offset + size;
      lock_release(&inode->lock);
  }
  lock_release(&inode->extend_lock);

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      ASSERT(sector_idx != (block_sector_t)-1);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length(inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Writes the bytes that should be written from the buffer into the
         respective block in the cache. */
      write_buffer_into_cache(sector_idx, sector_ofs, buffer, bytes_written, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  /* Update read_length that is used by the inode_read_at function
     to check how many bytes can be read from the inode. This is needed
     since we do not want to be able to read bytes that are actually not written. */
  lock_acquire(&inode->lock);
  inode->read_length = inode->length;
  lock_release(&inode->lock);

  return bytes_written;
}

/* Extends the inode so that it has enough blocks to hold target_bytes_count bytes. */
bool inode_blocks_extend(struct inode_blocks * inode_blocks, unsigned current_bytes_count, unsigned target_bytes_count)
{

  ASSERT(target_bytes_count >= current_bytes_count);

  /* Compute how many block we need to allocate */
  int current_block = bytes_to_sectors(current_bytes_count);
  int target_block = bytes_to_sectors(target_bytes_count);
  int blocks_to_extend = target_block - current_block;
  block_sector_t temp_integer;

  /* Check if inode is already big enough */
  ASSERT(blocks_to_extend >= 0);
  if(blocks_to_extend==0)
    return true;

  /* Write the next blocks to the direct blocks */
  unsigned i;
  for(i=0;i<DIRECT_BLOCKS_COUNT;i++)
  {
    /* Check that we need further blocks */
    if(blocks_to_extend==0)
      return true;

    /* Check that the block in direct block structure is free */
    if(inode_blocks->direct_block_sectors[i] != 0)
      continue;

    /* Allocate new block and write it into the direct block structure */
    if(!free_map_allocate(1, &temp_integer))
      return false;
    write_zeroes_into_cache(temp_integer);
    inode_blocks->direct_block_sectors[i] = temp_integer;
    --blocks_to_extend;
  }

  /* Check that we need further blocks */
  if(blocks_to_extend==0)
    return true;

  /* No indirect block allocated, so get one. */
  if(inode_blocks->indirect_block_sector == 0)
  {
    if(!free_map_allocate(1, &temp_integer))
      return false;
    /* Fill indirect block with zeroes, so we know where the
       next free place in indirect blocks structure is. */
    write_zeroes_into_cache(temp_integer);
    inode_blocks->indirect_block_sector = temp_integer;
  }

  /* Fill indirect block structure with blocks */
  if(!indirect_block_extend(inode_blocks->indirect_block_sector, &blocks_to_extend))
    return false;

  /* Check that we need further blocks */
  if(blocks_to_extend==0)
    return true;

  /* No doubly indirect block allocated, so get one.  */
  if(inode_blocks->doubly_indirect_block_sector == 0)
  {
    if(!free_map_allocate(1, &temp_integer))
      return false;
    /* Fill indirect block with zeroes, so we know where the
       next free place in indirect blocks structure is. */
    write_zeroes_into_cache(temp_integer);
    inode_blocks->doubly_indirect_block_sector = temp_integer;
  }

  /* Fill doubly indirect block structure with blocks */
  if(!doubly_indirect_block_extend(inode_blocks->doubly_indirect_block_sector, &blocks_to_extend))
    return false;

  /* Check that we need further blocks */
  if(blocks_to_extend==0)
    return true;

  /* Could not extend the inode because the file we want to write is too big */
  PANIC("[%s,%i] Could not extend inode because target size exceeds the maximum file size.", __FILE__, __LINE__);
  return false;

}

/* Extend indirect block structure with blocks until there are no
   blocks to extend left or indirect block is full. */
bool indirect_block_extend(block_sector_t indirect_block_idx, int * blocks_to_extend)
{

  block_sector_t i;
  for(i=0;i<BLOCKS_PER_INDIRECT_BLOCK;i++)
  {
    /* Check that we need further blocks */
    if( (*blocks_to_extend) == 0)
      break;

    block_sector_t block_idx = get_indirect_block_value(indirect_block_idx, i);

    /* Check that place in indirect block structure is free */
    if(block_idx != 0)
      continue;

    if(!free_map_allocate(1, &block_idx))
      return false;
    /* Initialize block with zeroes */
    write_zeroes_into_cache(block_idx);
    set_indirect_block_value(indirect_block_idx, i, block_idx);
    --(*blocks_to_extend);

  }

  return true;

}

/* Extend doubly indirect block structure with blocks until there are no
   blocks to extend left or indirect block is full. */
bool doubly_indirect_block_extend(block_sector_t doubly_indirect_block_idx, int * blocks_to_extend)
{

  block_sector_t i;
  for(i=0;i<BLOCKS_PER_INDIRECT_BLOCK;i++)
  {

    /* Check that we need further blocks */
    if( (*blocks_to_extend) == 0)
      break;

    /* Read indirect block value for current i */
    block_sector_t indirect_block_idx = get_indirect_block_value(doubly_indirect_block_idx, i);

    if(indirect_block_idx == 0)
    {
      if(!free_map_allocate(1, &indirect_block_idx))
        return false;
      /* Write zeroes into indirect block structure to know where next free space is. */
      write_zeroes_into_cache(indirect_block_idx);
      set_indirect_block_value(doubly_indirect_block_idx, i, indirect_block_idx);
    }

    /* Fill indirect block structure with blocks */
    if(!indirect_block_extend(indirect_block_idx, blocks_to_extend))
      return false;

  }

  return true;

}

/* Frees the allocated blocks for the given inode_blocks structure */
bool inode_blocks_deallocate(struct inode_blocks * inode_blocks, int length)
{
  /* Compute how many blocks we need to free */
  int blocks_to_deallocate = bytes_to_sectors(length);

  /* Free all direct blocks */
  unsigned i;
  for(i=0;i<DIRECT_BLOCKS_COUNT;i++)
  {
    /* Check that there are further blocks to deallocate */
    if(blocks_to_deallocate == 0)
      return true;

    free_map_release(inode_blocks->direct_block_sectors[i], 1);

    inode_blocks->direct_block_sectors[i] = 0;
    --blocks_to_deallocate;
  }

  /* Check that there are further blocks to deallocate */
  if(blocks_to_deallocate==0)
    return true;

  /* Free blocks in indirect block structure */
  indirect_block_deallocate(inode_blocks->indirect_block_sector, &blocks_to_deallocate);
  inode_blocks->indirect_block_sector = 0;

  /* Check that there are further blocks to deallocate */
  if(blocks_to_deallocate==0)
    return true;

  /* Free blocks in doubly indirect block structure */
  doubly_indirect_block_deallocate(inode_blocks->doubly_indirect_block_sector, &blocks_to_deallocate);
  inode_blocks->doubly_indirect_block_sector = 0;

  /* Check that there are further blocks to deallocate */
  if(blocks_to_deallocate==0)
    return true;

  /* Could not deallocate the inode because the file we want to write is too big */
  PANIC("[%s,%i] Could not deallocate all blocks.", __FILE__, __LINE__);
  return false;

}

/* Free all allocated blocks within given indirect block */
void indirect_block_deallocate(block_sector_t indirect_block_idx, int * blocks_to_deallocate)
{
  unsigned i;
  for(i=0;i<BLOCKS_PER_INDIRECT_BLOCK;i++)
  {
    /* Check that there are further blocks to deallocate */
    if( (*blocks_to_deallocate) == 0)
      return;
    free_map_release(get_indirect_block_value(indirect_block_idx, i), 1);
    --(*blocks_to_deallocate);
  }
}

/* Free all allocated blocks within given doubly indirect block */
void doubly_indirect_block_deallocate(block_sector_t doubly_indirect_block_idx, int * blocks_to_deallocate)
{
  unsigned i;
  for(i=0;i<BLOCKS_PER_INDIRECT_BLOCK;i++)
  {
    /* Check that there are further blocks to deallocate */
    if( (*blocks_to_deallocate) == 0)
      return;
    /* Free indirect block within doubly indirect block */
    indirect_block_deallocate(get_indirect_block_value(doubly_indirect_block_idx, i), blocks_to_deallocate);
  }
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  lock_acquire(&inode->lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length (struct inode *inode)
{
  off_t ret;
  lock_acquire(&inode->lock);
  ret = inode->length;
  lock_release(&inode->lock);
  return ret;
}

/* Return the truly written length of the INODE's data.
   Can differ from length since a process may be */
off_t inode_read_length (struct inode *inode)
{
  off_t ret;
  lock_acquire(&inode->lock);
  ret = inode->read_length;
  lock_release(&inode->lock);
  return ret;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber (struct inode *inode)
{
  block_sector_t ret;
  lock_acquire(&inode->lock);
  ret = inode->sector;
  lock_release(&inode->lock);
  return ret;
}

/* Decrements the open count of given inode an return the new value */
int inode_decrement_open_cnt(struct inode * inode)
{
  off_t ret;
  lock_acquire(&inode->lock);
  ret = --inode->open_cnt;
  lock_release(&inode->lock);
  return ret;
}

/* Return the current open count of given inode */
int inode_open_cnt(struct inode * inode)
{
  off_t ret;
  lock_acquire(&inode->lock);
  ret = inode->open_cnt;
  lock_release(&inode->lock);
  return ret;
}

/* Returns, if the inode is a directory */
bool
inode_is_directory(struct inode * inode)
{
  bool ret;
  lock_acquire(&inode->lock);
  ret = inode->is_dir;
  lock_release(&inode->lock);
  return ret;
}

/* Return parent of given inode */
block_sector_t inode_get_parent(struct inode *inode)
{
  off_t parent;
  lock_acquire(&inode->lock);
  parent = inode->parent;
  lock_release(&inode->lock);
  return parent;
}

/* Sets the parent of the given inode */
bool inode_add_parent(block_sector_t parent, block_sector_t child)
{
  struct inode *inode = inode_open(child);

  if (!inode)
    return false;

  lock_acquire(&inode->lock);
  inode->parent = parent;
  lock_release(&inode->lock);

  inode_close(inode);
  return true;
}

/* Sets the given inode as removed */
bool inode_is_removed(struct inode * inode)
{
  bool ret;
  lock_acquire(&inode->lock);
  ret = inode->removed;
  lock_release(&inode->lock);
  return ret;
}
