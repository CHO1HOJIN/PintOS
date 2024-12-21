#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/malloc.h"
#include "lib/string.h"

#define PGSIZE 4096
// frame table list
struct list frame_table;
// for iterating through the frame table
struct list_elem *frame_ptr;
// lock for frame table for synch
struct lock frame_lock;

struct frame *alloc_page_to_frame(enum palloc_flags fg);
struct frame *find_frame(void *pfn);
static void delete_frame(struct frame *f);
void free_frame(void *pfn);
bool load_to_frame(void *pfn, struct PTE *pte);
void evict_frame(void);
static struct list_elem *clock(void);


void frame_table_init(void){
    frame_ptr = NULL;
    list_init(&frame_table);
    lock_init(&frame_lock);
}

struct frame *alloc_page_to_frame(enum palloc_flags fg){
    struct frame *f = (struct frame *)malloc(sizeof(struct frame));
    if(f == NULL) return NULL;

    // initialize frame
    memset(f, 0, sizeof(struct frame));
    f->t = thread_current();
    f->pfn = palloc_get_page(fg);

    // add to frame table
    while (f->pfn == NULL){
        lock_acquire(&frame_lock);
        evict_frame();
        lock_release(&frame_lock);
        f->pfn = palloc_get_page(fg);
    }

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &f->elem);
    lock_release(&frame_lock);

    return f;
}

struct frame *find_frame(void *pfn){
    struct list_elem *e;
    struct frame *f;
    for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
        f = list_entry(e, struct frame, elem);
        if(f->pfn == pfn) return f;
    }

    return NULL;
}
static void delete_frame (struct frame *f){
    struct list_elem *e = &(f->elem);
    struct list_elem *rm = list_remove(e);
    if (e == frame_ptr) frame_ptr = rm;
}

void free_frame(void *pfn){
    lock_acquire(&frame_lock);
    struct frame *f = find_frame(pfn);
    if(f == NULL){
        lock_release(&frame_lock);
        return;
    }
    delete_frame(f);
    pagedir_clear_page(f->t->pagedir, f->pte->vpn);
    palloc_free_page(f->pfn);
    free(f);
    lock_release(&frame_lock);
}

bool load_to_frame(void *pfn, struct PTE *pte){
    size_t read_bytes = pte->read_bytes;
    if((size_t)file_read_at(pte->file, pfn, read_bytes, pte->offset) != read_bytes){
        return false;
    }

    memset(pfn + read_bytes, 0, PGSIZE - read_bytes);
    return true;
}

// clock algorithm
// Cycle the frame table and find the frame to evict
static struct list_elem *clock(void){
    // if the frame pointer is NULL and the frame table is not empty, set the frame pointer to the beginning
    if(frame_ptr == NULL && !list_empty(&frame_table)){
        frame_ptr = list_begin(&frame_table);
        return frame_ptr;
    }
    // if the frame pointer is at the end of the frame table, set it to the beginning
    if(frame_ptr == list_end(&frame_table) && !list_empty(&frame_table)){
        frame_ptr = list_begin(&frame_table);
        return frame_ptr;
    }
    // if the frame pointer is at the end of the frame table, set it to the beginning
    frame_ptr = list_next(frame_ptr);
    if(frame_ptr == list_end(&frame_table)) 
        frame_ptr = clock();
    
    return frame_ptr;
}

// evict frame using clock algorithm
void evict_frame (void){
    struct frame *f;
    // get victim frame
    while (true){
        f = list_entry(clock(), struct frame, elem);
        // if the frame is accessed, set the accessed bit to false
        if(pagedir_is_accessed(f->t->pagedir, f->pte->vpn)){
            pagedir_set_accessed(f->t->pagedir, f->pte->vpn, 0);
        }
        // if the frame is not accessed, evict the frame
        else {
            bool dirty = pagedir_is_dirty(f->t->pagedir, f->pte->vpn);
            // if the frame is dirty
            if(f->pte->type == MEMMAP && dirty){
                // if the frame is memmaped and dirty, then write 
                // data to the file, and evict
                file_write_at (f->pte->file, f->pfn, f->pte->read_bytes, \
                    f->pte->offset);
            }
            // if the frame is from the swap slot, swap out.
            else if(f->pte->type == SWAP){
                f->pte->swap_slot = swap_out(f->pfn);
            }
            // swap out and change the type to SWAP
            else if(f->pte->type == LOAD && dirty){
                f->pte->swap_slot = swap_out(f->pfn);
                f->pte->type = SWAP;
            }
            
            // free the frame
            f->pte->mem_flag = false;
            delete_frame(f);
            pagedir_clear_page(f->t->pagedir, f->pte->vpn);
            palloc_free_page(f->pfn);
            free(f);

            return;
        }
    }

}