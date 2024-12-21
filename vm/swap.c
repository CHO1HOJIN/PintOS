#include "vm/swap.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

/* Swap table */
struct bitmap *swap_bitmap;
/* Swap block */
struct block *swap_block;
/* Swap lock */
struct lock swap_lock;

void swap_init(void){
    swap_bitmap = bitmap_create(PGSIZE);
    lock_init(&swap_lock);
}

void swap_in(size_t used_index, void *pfn){

    if (used_index == 0) {
      NOT_REACHED();
    }
    else {
        used_index -= 1;
        swap_block = block_get_role(BLOCK_SWAP);
        lock_acquire(&swap_lock);
        // read from swap block (start: used_index * 8, 8 sectors)
        for(size_t i = 0; i < 8; i++){
            block_read(swap_block, used_index * 8 + i, pfn + \
                i * BLOCK_SECTOR_SIZE);
        }
        /* Unset the read sector */
        bitmap_set_multiple(swap_bitmap, used_index, 1, false);
        lock_release(&swap_lock);
    }
    return;
}

// 1. select victim page
// 2. if necessary, write to disk
// 3. nullify the page table entry
size_t swap_out(void *pfn){
    
    // find empty slot in swap table, and set the bit to 1
    swap_block = block_get_role(BLOCK_SWAP);

    lock_acquire(&swap_lock);

    size_t free_index = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
    for(size_t i = 0; i < 8; i++){
        block_write(swap_block, free_index * 8 + i, pfn + \
            i * BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
    free_index += 1;
    return free_index;
}

void swap_free(size_t used_index){
    if(used_index == 0) return;
    used_index -= 1;
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_bitmap, used_index, 1, false);
    lock_release(&swap_lock);
    return;
}