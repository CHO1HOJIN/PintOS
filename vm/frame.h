#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

struct frame{
    void *pfn; // Physical Frame Number
    struct thread *t; // Thread that owns the frame
    struct PTE *pte; // Page Table Entry
    struct list_elem elem; // List element for frame list
};



void frame_table_init(void);
struct frame *alloc_page_to_frame(enum palloc_flags fg);
struct frame *find_frame(void *pfn);
void free_frame(void *pfn);
bool load_to_frame(void *pfn, struct PTE *pte);
#endif/* vm/frame.h */