#include "vm/page.h"
#include <string.h>
#include <stdio.h>
#include "vm/frame.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"


/* Looks up a the supplemental page for a given hash table and virtual address */
struct sup_page * get_sup_page(struct hash * spt, void * virt_addr)
{

  struct sup_page p;
  struct hash_elem * e;

  p.virt_addr = pg_round_down(virt_addr);
  e = hash_find (spt, &p.elem);

  if(e != NULL)
    return hash_entry(e, struct sup_page, elem);

  return NULL;

}

/* Checks if the supplemental page corresponding to a given virtual
   address is free. Needed for the check if enough continuous
   pages for a memory mapping are available. */
bool sup_page_free(struct hash * spt, void * check_addr)
{
  struct sup_page * sp = get_sup_page(spt, check_addr);
  return sp==NULL;
}

/* Loads a page into a frame if it is not already present */
bool load_page(struct hash * spt, void * virt_addr, bool pin)
{

  /* Get and check supplemental page for given virtual address */
  struct sup_page * sp = get_sup_page(spt, virt_addr);
  if(sp == NULL)
  {
    PANIC("[page.c|load_page] Could not find virtual address in supplemental page table.");
    return false;
  }

  /* Pin page while loading it */
  sup_page_pin(sp);

  /*  Do not load if it is already available */
  if(sup_page_is_available(sp))
    return true;

  /* Load the page depending on the type */
  bool loaded = false;
  switch(sp->type)
  {
    case FILE:
      loaded = load_file(sp);
      break;
    case MEM:
      loaded = load_swap(sp);
      break;
    case MMAP:
      loaded = load_file(sp);
      break;
    case STACK:
      loaded = load_empty(sp);
      break;
    default:
      PANIC("[page.c|load_page] No matching enum found.");
      break;
  }

  /* Check the given pin variable if the page should stay pinned after
     this function. If not, unpin it. */
  if(!pin)
    sup_page_unpin(sp);

  return loaded;

}

/* Load from file into a frame */
bool load_file(struct sup_page * sp)
{

  /* Set correct flags */
  enum palloc_flags flag;
  if (sp->read_bytes == 0)
      flag = PAL_USER | PAL_ZERO;
  else
      flag = PAL_USER;

  /* Allocate a frame for loading the data into it */
  void * frame_addr = frame_allocate(flag, sp);
  if (frame_addr == NULL)
    return false;

  /* Check that the read_bytes and zero_bytes adds up to a page size */
  ASSERT( (sp->read_bytes + sp->zero_bytes) == PGSIZE);

  /* Check that we want to read more than zero bytes. If so, read
     the read_bytes bytes stating at ofs from the file and copy them into
     the frame. Fill up rest with zeroes. */
  if(sp->read_bytes > 0)
  {
    acquire_harddrive_access();
    int bytes_read = file_read_at (sp->file, frame_addr, sp->read_bytes, sp->ofs);
    if (bytes_read != (int) sp->read_bytes)
    {
      release_harddrive_access();
      frame_deallocate(frame_addr);
      return false;
    }

    memset((char*)frame_addr + bytes_read, 0, sp->zero_bytes);
    release_harddrive_access();
  }

  /* Tell the OS about the mapping from virtual address to frame address */
  bool installed = install_page(sp->virt_addr, frame_addr, sp->writable);
  if (!installed)
  {
    frame_deallocate(frame_addr);
    return false;
  }

  /* Set the just loaded page as available */
  sup_page_set_available(sp);

  /* Clean the dirty status as it is just loaded and cannot be dirty */
  struct thread * curr_thread = thread_current();
  pagedir_set_dirty(curr_thread->pagedir, sp->virt_addr, false);

  return true;

}

/* Load data from swap memory into a frame */
bool load_swap(struct sup_page * sp)
{
  /* Allocate frame to copy data into */
  void * new_frame_addr = frame_allocate(PAL_USER, sp);
  if(new_frame_addr == NULL)
    return false;

  /* Tell the OS about the mapping from virtual address to frame address */
  bool installed = install_page(sp->virt_addr, new_frame_addr, sp->writable);
  if(!installed)
  {
    frame_deallocate(new_frame_addr);
    return false;
  }

  /* Copy data into frame and set the page as available */
  swap_to_page(sp->swap_idx, new_frame_addr);
  sup_page_set_available(sp);

  return true;

}

/* Loads a frame without data*/
bool load_empty(struct sup_page * sp)
{
  /* Allocate frame to copy data into */
  void * new_frame_addr = frame_allocate(PAL_USER, sp);
  if(new_frame_addr == NULL)
    return false;

  /* Tell the OS about the mapping from virtual address to frame address */
  bool installed = install_page(sp->virt_addr, new_frame_addr, sp->writable);
  if(!installed)
  {
    frame_deallocate(new_frame_addr);
    return false;
  }

  /* Set the type and set the page as available */
  sp->type = MEM;
  sup_page_set_available(sp);

  return true;
}


/* Creates a supplemental page for a file.
   Despite lazy load the content is not load
   at this point into a frame. We wait until it
   is really accessed. */
bool create_file_sup_page(struct hash * spt, void * virt_addr, struct file * file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{

  struct sup_page * sp = malloc(sizeof(struct sup_page));

  if(sp == NULL)
    return false;

  sp->allocating_thread = thread_current();
  sp->type = FILE;
  sp->virt_addr = virt_addr;
  sp->available = false;
  sp->file = file;
  sp->ofs = ofs;
  sp->read_bytes = read_bytes;
  sp->zero_bytes = zero_bytes;
  sp->writable = writable;
  sp->pinned = false;
  lock_init(&sp->lock);

  struct hash_elem * e = hash_insert (spt, &sp->elem);

  if(e!=NULL)
  {
    free(sp);
    return false;
  }

  return true;

}

/* Creates a supplemental page for a memory mapped file.
   Despite lazy load the content is not load
   at this point into a frame. We wait until it
   is really accessed. */
bool create_mmap_sup_page(struct hash * spt, void * virt_addr, struct file * file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes)
{

  struct sup_page * sp = malloc(sizeof(struct sup_page));

  if(sp == NULL)
    return false;

  sp->allocating_thread = thread_current();
  sp->type = MMAP;
  sp->virt_addr = virt_addr;
  sp->available = false;
  sp->file = file;
  sp->ofs = ofs;
  sp->read_bytes = read_bytes;
  sp->zero_bytes = zero_bytes;
  sp->writable = true;
  sp->pinned = false;
  lock_init(&sp->lock);

  struct hash_elem * e = hash_insert(spt, &sp->elem);
  if(e!=NULL)
  {
    free(sp);
    return false;
  }

  return true;

}

/* Removes a supplemental page coming from a memory mapping  */
bool remove_mmap_sup_page(struct hash * spt, void * virt_addr)
{

  /* Get supplemental page corresponding to the given virtual address */
  struct thread * curr_thread = thread_current();
  struct sup_page * sp = get_sup_page(spt, virt_addr);
  sup_page_pin(sp);

  if(sp == NULL || virt_addr == NULL)
    return false;

  /* If page is available, thus loaded, and dirty, write changes back to
     the file that was mapped. Note that we write here only this page segment
     back to the file, not(!) all changes to the file. After, we deallocate
     the frame where the data was stored in. */
  if(sup_page_is_available(sp))
  {
    bool is_dirty = pagedir_is_dirty(curr_thread->pagedir, sp->virt_addr);
    if(is_dirty)
    {
      acquire_harddrive_access();
      file_write_at(sp->file, sp->virt_addr, sp->read_bytes, sp->ofs);
  		release_harddrive_access();
    }

    void * curr_frame = pagedir_get_page(curr_thread->pagedir, sp->virt_addr);
    frame_deallocate(curr_frame);
  	pagedir_clear_page(curr_thread->pagedir, sp->virt_addr);
  }

  /* Remove supplemental page */
  hash_delete(spt, &sp->elem);
  free(sp);

  return true;

}

/* Tries to extend stack by a further page. The success of the extension
   depends on the position of the virtual address we wanted to access
   relative to the current stack pointer. If it is too far below the current
   stack pointer we will refuse the extension, because we can assume
   that this is no stack access. Additionally, we can define if this heuristic
   should be used. This is needed for the allocation of the first stack page,
   where we do not want the extension to fail even if the address is too far
   below the stack pointer. */
bool try_extend_stack(void * virt_addr, void * curr_esp, bool use_heuristic)
{

  /* Check if virtual address is within user address space */
  void * rounded_virt_addr = pg_round_down(virt_addr);
  bool within_user_space = (rounded_virt_addr < PHYS_BASE) && (rounded_virt_addr >= (void*) 0x08048000);
  if(!within_user_space)
    return false;

  /* Check if page machting the virtual address is within stack */
  bool within_stack = rounded_virt_addr >= (PHYS_BASE - STACK_SIZE_LIMIT);
  if(use_heuristic)
    within_stack = within_stack && virt_addr >= (curr_esp - STACK_HEURISTIC);
  if(!within_stack)
    return false;

  /* Create supplemental page and initialize it */
  struct sup_page * sp = malloc(sizeof(struct sup_page));

  if(sp == NULL)
    return false;

  sp->allocating_thread = thread_current();
  sp->type = STACK;
  sp->virt_addr = rounded_virt_addr;
  sp->available = false;
  sp->writable = true;
  sp->pinned = false;
  lock_init(&sp->lock);

  /* Add supplemental page to hash list */
  struct thread * curr_thread = thread_current();
  struct hash_elem * e = hash_insert(&curr_thread->spt, &sp->elem);
  return (e==NULL);

}

/* Sets the pinning of a given supplemental page to value */
void sup_page_set_pinning(struct sup_page * sp, bool value)
{
  if(sp == NULL)
    return;
  lock_acquire(&sp->lock);
  sp->pinned = value;
  lock_release(&sp->lock);
}

/* Pins the page that corresponds to a given
   virtual address */
void sup_page_pin_vaddr(void * virt_addr)
{
  struct thread * curr_thread = thread_current();
  struct sup_page * sp = get_sup_page(&curr_thread->spt, virt_addr);
  sup_page_set_pinning(sp, true);
}

/* Unpins the page that corresponds to a given
   virtual address */
void sup_page_unpin_vaddr(void * virt_addr)
{
  struct thread * curr_thread = thread_current();
  struct sup_page * sp = get_sup_page(&curr_thread->spt, virt_addr);
  sup_page_set_pinning(sp, false);
}

/* Pins a given page */
void sup_page_pin(struct sup_page * sp)
{
  sup_page_set_pinning(sp, true);
}

/* Unpins a given page */
void sup_page_unpin(struct sup_page * sp)
{
  sup_page_set_pinning(sp, false);
}

/* Checks if a given page is pinned(which means that
   it cannot be evicted) */
bool sup_page_is_pinned(struct sup_page * sp)
{
  bool pinned = false;
  if(sp != NULL)
  {
    lock_acquire(&sp->lock);
    pinned = sp->pinned;
    lock_release(&sp->lock);
  }

  return pinned;
}

/* Sets the given page to unavailable */
void sup_page_set_unavailable(struct sup_page * sp)
{
  sup_page_set_availability(sp, false);
}

/* Sets a given page to available */
void sup_page_set_available(struct sup_page * sp)
{
  sup_page_set_availability(sp, true);
}

/* Sets the availability of the given page to value */
void sup_page_set_availability(struct sup_page * sp, bool value)
{
  if(sp != NULL)
  {
    lock_acquire(&sp->lock);
    sp->available = value;
    lock_release(&sp->lock);
  }
}

/* Checks if a supplemental page is available(which means it has an
   assigned frame) */
bool sup_page_is_available(struct sup_page * sp)
{
  bool available = false;
  if(sp != NULL)
  {
    lock_acquire(&sp->lock);
    available = sp->available;
    lock_release(&sp->lock);
  }

  return available;
}

/* Initializes the hash table */
void sup_page_table_init(struct hash * spt)
{
  ASSERT(spt != NULL);
  hash_init (spt, sup_page_hash, sup_page_less, NULL);
}

/* Destroys the given hash table */
void sup_page_table_free(struct hash * spt)
{
  ASSERT(spt != NULL);
  hash_destroy (spt, sup_page_action_func);
}

/* Returns a hash value for page p. */
unsigned sup_page_hash (const struct hash_elem * p_, void * aux UNUSED)
{
  const struct sup_page *p = hash_entry (p_, struct sup_page, elem);
  unsigned bytes = hash_bytes (&p->virt_addr, sizeof p->virt_addr);
  return bytes;
}

/* Returns true if page a precedes page b. */
bool sup_page_less (const struct hash_elem * a_, const struct hash_elem * b_, void * aux UNUSED)
{
  const struct sup_page *a = hash_entry (a_, struct sup_page, elem);
  const struct sup_page *b = hash_entry (b_, struct sup_page, elem);
  return a->virt_addr < b->virt_addr;
}

/* Frees the resources for a supplemental page struct. Used when a hash table is destroyed.
   We have to acquire here the evict_lock. If not, it could happen that the page
   is available(so it has an assigned frame) and we want to free the assigned frame.
   In the meantime another thread already has evicted the frame. In this case,
   the frame cannot be deallocated anymore and we get a failure. */
void sup_page_action_func(struct hash_elem * e, void * aux UNUSED)
{
  struct sup_page * sp = hash_entry(e, struct sup_page, elem);
  lock_acquire(&evict_lock);
  if(sup_page_is_available(sp))
  {
    struct thread * curr_thread = thread_current();
    void * curr_frame = pagedir_get_page(curr_thread->pagedir, sp->virt_addr);
    pagedir_clear_page(curr_thread->pagedir, sp->virt_addr);
    frame_deallocate(curr_frame);
  }
  lock_release(&evict_lock);
  free(sp);
}
