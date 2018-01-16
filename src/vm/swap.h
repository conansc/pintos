#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>
#include "threads/vaddr.h"

/* Index that is used to access the swap bitmap and block memory. */
typedef int swap_idx_t;

void swap_init(void);
swap_idx_t page_to_swap(void *);
void swap_to_page(swap_idx_t idx, void *);
void clear_swap_at(swap_idx_t);

#endif /* vm/swap.h */
