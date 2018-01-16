#include "vm/swap.h"
#include "threads/synch.h"
#include "devices/block.h"

/* Block memory where the swapped out pages are stored in */
static struct block * swap_memory;
/* Stores how many pages can be stored in the block memory.
   A page needs multiple blocks, so we can store less
   pages in the block memory than there are blocks in it.
   We will referr to this bunchs of blocks as "spots"
   in the following */
static int swap_size;
/* Stores how many block sectors we need per page.
   Needed since a block is smaller than a page
   (in our case 4096 bytes <> 512 bytes)*/
static int page_sectors_count = PGSIZE / BLOCK_SECTOR_SIZE;
/* Bitmap to store which spots in the block are free  */
static struct bitmap * swap_free;
/* Lock that is used when accessing the bitmap */
struct lock swap_lock;

/* Initializes the swap block memory, bitmap and
   all other needed variables (see explanation above). */
void swap_init(void)
{
  swap_memory = block_get_role(BLOCK_SWAP);
  ASSERT(swap_memory != NULL);
  ASSERT(page_sectors_count != 0);
  swap_size = block_size(swap_memory) / page_sectors_count;
  swap_free = bitmap_create(swap_size);
  ASSERT(swap_free != NULL);
  /* Set all values in the bitmap to true, because they are for now all free */
  bitmap_set_all(swap_free, true);
  lock_init(&swap_lock);
}

/* Copies a frame to the swap block memory */
swap_idx_t page_to_swap(void * page)
{

  ASSERT(page >= PHYS_BASE);
  lock_acquire(&swap_lock);
  /* Get the next free spot in the swap block memory */
  unsigned idx = bitmap_scan(swap_free, 0, 1, true);
  lock_release(&swap_lock);

  /* If no free spot could be found, swap is full. */
  if(idx == BITMAP_ERROR)
    PANIC("Swap is full.");

  /* Copy the page from the frame to the swap block memory block-wise */
  int start = idx * page_sectors_count;
  int i;
  for(i=0; i<page_sectors_count; i++)
    block_write(swap_memory, start+i, page + (BLOCK_SECTOR_SIZE * i));

  /* Set the just used spot in the swap block memory to occupied */
  lock_acquire(&swap_lock);
  bitmap_set(swap_free, idx, false);
  lock_release(&swap_lock);

  /* Return the index of the used spot in the swap block memory */
  return idx;

}

/* Copies a page from the swap block memory to a frame */
void swap_to_page(swap_idx_t idx, void * page)
{

  /* Check that the spot is really occupied with data */
  lock_acquire(&swap_lock);
  ASSERT(bitmap_test(swap_free, idx) == false);
  lock_release(&swap_lock);

  /* Copy the page from the swap block memory to the frame block-wise */
  int start = idx * page_sectors_count;
  int i;
  for(i=0; i<page_sectors_count; i++)
    block_read(swap_memory, start+i, page + (BLOCK_SECTOR_SIZE * i));

  /* Set the copied spot to free */
  lock_acquire(&swap_lock);
  bitmap_set(swap_free, idx, true);
  lock_release(&swap_lock);

}
