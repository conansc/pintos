#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"


/* Initializes the frame table and the locks */
void frame_table_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_table_lock);
  lock_init(&evict_lock);
}


/* Allocates a frame and adds the frame to the frame list */
void * frame_allocate(enum palloc_flags flag, struct sup_page * sp)
{

  /* Make sure that we want to allocate a frame within the user space. */
  if ( (PAL_USER | PAL_ZERO) != flag && PAL_USER != flag )
    return NULL;

  /* Try to allocate frame */
  void * new_frame = palloc_get_page(flag);

  /* Check if a frame could be allocated. If not,
     we first need to evict one and check then if we can
     allocate it now. It could happen that in the meantime
     another thread allocated the just evicted frame and we have to
     evict another one. We do this routine until we could allocate
     a frame. */
  while(new_frame == NULL)
  {
    evict_frame();
    new_frame = palloc_get_page(flag);
  }

  /* Add frame to frame table and return it */
  frame_add(new_frame, sp);
  return new_frame;

}

/* Add a newly created frame to the frame table */
void frame_add(void* frame_addr, struct sup_page * sp)
{

  /* Create frame struct*/
  struct frame * new_frame = malloc(sizeof(struct frame));
  ASSERT(new_frame != NULL);

  /* Set variables in frame struct */
  new_frame->frame_addr = frame_addr;
  new_frame->sp = sp;

  /* Add struct to frame table */
  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &new_frame->elem);
  lock_release(&frame_table_lock);

}

/* Gets the corresponding frame for a given frame address by iterating over
   the frame table and compare the frame addresses. */
struct frame * get_frame(void * frame_addr)
{

  struct list_elem * next;
  struct list_elem * e;

  lock_acquire(&frame_table_lock);

  struct frame * curr_frame = NULL;
  e = list_begin(&frame_table);
  while(e != list_end(&frame_table))
  {
    next = list_next(e);
    curr_frame = list_entry(e, struct frame, elem);
    if(frame_addr == curr_frame->frame_addr)
      break;
    e = next;
  }

  lock_release(&frame_table_lock);

  return curr_frame;

}

/* Frees the allocated frame by iterating over the frame table
   and removes the frame that corresponds to the given frame addres. */
void frame_deallocate(void* frame_addr)
{
  struct list_elem * next;
  struct list_elem * e;
  bool found = false;

  lock_acquire(&frame_table_lock);

  e = list_begin(&frame_table);
  while(e != list_end(&frame_table))
  {
    next = list_next(e);
    struct frame * curr_frame = list_entry(e, struct frame, elem);
    if(frame_addr == curr_frame->frame_addr)
    {
      list_remove(e);
      free(curr_frame);
      found = true;
      break;
    }
    e = next;
  }

  ASSERT(found);

  lock_release(&frame_table_lock);

  palloc_free_page(frame_addr);

}

/* Iterates two times over the frame list and tries to evict one of the
   frames. The exact conditions for an eviction can be seen in the function
   try_evict. We iterate two times since it could happen that in the first
   iteration over the list frames could have set the accessed bit. So, we have
   to unset it in the first iteration and iterate again to be able to evict
   now the unset frames. Overall we use the clock algorithm for this procedure. */
void evict_frame(void)
{

  unsigned frame_table_size;
  struct frame * curr_frame;
  struct list_elem * next;
  struct list_elem * e;
  bool evicted = false;

  lock_acquire(&evict_lock);

  lock_acquire(&frame_table_lock);
  frame_table_size = list_size(&frame_table);
  e = list_begin(&frame_table);
  lock_release(&frame_table_lock);

  /* Iterate twice over the frame table while trying to evict a frame. */
  unsigned i;
  for(i=0;i<2*frame_table_size;i++)
  {

    lock_acquire(&frame_table_lock);

    next = list_next(e);
    if(next == list_end(&frame_table))
      next = list_begin(&frame_table);

    /* Get next frame from the list */
    curr_frame = list_entry(e, struct frame, elem);

    lock_release(&frame_table_lock);

    /* Try to evict the next frame from the list.
       If the eviction was successfully we can stop here. */
    evicted = try_evict(curr_frame);
    if(evicted)
      break;

    e = next;

  }

  lock_release(&evict_lock);

}

/* Tries to evict a frame for a given frame address */
bool try_evict(struct frame * curr_frame)
{

  /* If the supplemental page for the frame is pinned, do not evict it. */
  if(sup_page_is_pinned(curr_frame->sp))
    return false;

  /* If the frame was accessed since the last time that the hand passed it,
     do not evict it. In this case we set it as unaccessed, as the hand just
     passed it. */
  bool accessed = pagedir_is_accessed(curr_frame->sp->allocating_thread->pagedir, curr_frame->sp->virt_addr);
  if(accessed)
  {
    pagedir_set_accessed(curr_frame->sp->allocating_thread->pagedir, curr_frame->sp->virt_addr, false);
    return false;
  }

  /* Remove frame from our frame table.
     Tell the OS that the frame is free. Must be done before transferring
     the frame, so we ensure that a write access to it causes a page fault,
     so we cannot modify it after transferring it.
     Must also be done before frame_deallocate,
     so if a thread tries to access this frame it will page fault and has
     to wait until this eviction is done(due to the holded evict_lock).
     We ensure with it, that no other thread gets the frame(since not freed),
     writes something in it and another thread tries to read
     its old data, that was just overwritten. */
  pagedir_clear_page(curr_frame->sp->allocating_thread->pagedir, curr_frame->sp->virt_addr);

  /* Evict frame depending on its type */
  switch(curr_frame->sp->type)
  {
    case FILE:
    /* For a file we need to check if it is was changed(dirty). If so, we
       do a copy-on-write and copy the changed content to the swap.
       Then we set the type to MEM since now the source is not the file
       anymore(instead it is the changed content). */
      if(pagedir_is_dirty(curr_frame->sp->allocating_thread->pagedir, curr_frame->sp->virt_addr))
      {
        curr_frame->sp->swap_idx = page_to_swap(curr_frame->frame_addr);
        curr_frame->sp->type = MEM;
      }
      break;
    case MEM:
    /* If the type is MEM we just simply copy the content to the swap. */
      curr_frame->sp->swap_idx = page_to_swap(curr_frame->frame_addr);
      break;
    case MMAP:
    /* If the type is MMAP, we need to check if is was changed(dirty). If, so
       we write the changes to the source file of the memory mapping. Note that
       this is different for MMAP than for FILE as type. */
      if(pagedir_is_dirty(curr_frame->sp->allocating_thread->pagedir, curr_frame->sp->virt_addr))
      {

        file_write_at(curr_frame->sp->file, curr_frame->frame_addr, curr_frame->sp->read_bytes, curr_frame->sp->ofs);
        
      }
      break;
    case STACK:
      PANIC("[%s,%i] Want to evict page with stack as type.", __FILE__, __LINE__);
      break;
    default:
      PANIC("[%s,%i] Could not find type of dirty page.", __FILE__, __LINE__);
      return NULL;
  }


  /* Set the supplemental page to unavailable.
     It is important to do it here, at the end of the function,
     because we want to the process, whose page we are evicting right now
     should not try to load the page back until we wrote the page
     somewhere, i.e. swap or file. */
  sup_page_set_unavailable(curr_frame->sp);

  /* Free frame */
  frame_deallocate(curr_frame->frame_addr);

  /* We have successfully evicted a frame */
  return true;

}
