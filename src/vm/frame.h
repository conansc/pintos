#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

/* A list to store all allocated frames. */
struct list frame_table;
/* Lock for the frametable_list */
struct lock frame_table_lock;
/* Lock for the eviction */
struct lock evict_lock;

/* Struct to store information about an allocated frame.
   All allocated frames are stored in the frame table list. */
struct frame
{
  /* The corresponding supplemental page */
  struct sup_page * sp;
  /* The physical address of the frame */
  void * frame_addr;
  /* The list element that is pushed to the frame_table_list */
  struct list_elem elem;
};


void frame_table_init(void);
struct frame * get_frame(void *);
void * frame_allocate(enum palloc_flags, struct sup_page *);
void frame_deallocate(void *);
void frame_add(void *, struct sup_page *);
void evict_frame(void);
bool try_evict(struct frame *);


#endif /* vm/frame.h */
