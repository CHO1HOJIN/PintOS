#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>
#include <bitmap.h>


void swap_init(void);
void swap_in(size_t used_index, void *pfn);
size_t swap_out(void *pfn);
void swap_free(size_t used_index);

#endif /* vm/swap.h */