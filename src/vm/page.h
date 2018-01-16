#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/swap.h"
#include "filesys/off_t.h"
#include "threads/synch.h"

#define STACK_SIZE_LIMIT 0x800000 /* Stack limit (8 MB) */
#define STACK_HEURISTIC 0x000020 /* Heuristic to approximate if access if stack access */


/* Enum that defines the source of the supplemental page.
   It is needed to decide later what to do on eviction of the page. */
enum page_type
{
  FILE,
  MEM,
  MMAP,
  STACK
};

struct sup_page
{
  /* A referrence to the thread that allocated the supplemental page */
  struct thread * allocating_thread;
  /* Source of the current table entry */
  enum page_type type;
  /* Virtual address which accesses this entry. Set once. */
  void * virt_addr;
  /* Index of the page in swap memory. Set if SWAP. No locking needed. */
  swap_idx_t swap_idx;
  /* Indicates if the page is in the memory. Lock needed. */
  bool available;
  /* File to read from. Set if FILE/MMAP. Set once. */
  struct file * file;
  /* Offset within file (dividable by page size). Set if FILE/MMAP. Set once. */
  off_t ofs;
  /* Indicates if file is writable. Set if FILE/MMAP. Set once. */
  bool writable;
  /* Tells if the page must not be evicted. Lock needed. */
  bool pinned;
  /* Indicates the ratio of real read bytes and zeroed bytes. Set if FILE/MMAP. Set once. */
  uint32_t read_bytes;
  uint32_t zero_bytes;
  /* Lock for the access to the pinned status. Set once. */
  struct lock lock;
  /* Element that is added to the hash list. Set once. */
  struct hash_elem elem;
};


struct sup_page * get_sup_page(struct hash *, void *);
bool sup_page_free(struct hash *, void *);
bool load_page(struct hash *, void *, bool);
bool load_file(struct sup_page *);
bool load_swap(struct sup_page *);
bool load_empty(struct sup_page *);
bool create_file_sup_page(struct hash *, void *, struct file *, off_t, uint32_t, uint32_t, bool);
bool create_mmap_sup_page(struct hash *, void *, struct file *, off_t, uint32_t, uint32_t);
bool remove_mmap_sup_page(struct hash *, void *);
bool try_extend_stack(void *, void *, bool);
void sup_page_set_pinning(struct sup_page *, bool);
void sup_page_pin_vaddr(void * );
void sup_page_unpin_vaddr(void *);
void sup_page_pin(struct sup_page *);
void sup_page_unpin(struct sup_page *);
bool sup_page_is_pinned(struct sup_page *);
void sup_page_set_unavailable(struct sup_page *);
void sup_page_set_available(struct sup_page *);
void sup_page_set_availability(struct sup_page *, bool);
bool sup_page_is_available(struct sup_page *);
void sup_page_table_init(struct hash *);
void sup_page_table_init(struct hash *);
void sup_page_table_free(struct hash *);
unsigned sup_page_hash (const struct hash_elem *, void *);
bool sup_page_less (const struct hash_elem *, const struct hash_elem *, void *);
void sup_page_action_func(struct hash_elem *, void *);


#endif /* vm/page.h */
