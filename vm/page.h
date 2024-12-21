#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

/* Type of the page. */
typedef enum { LOAD, SWAP, MEMMAP } pte_typ;

struct PTE{
   void *vpn; /* Virtual page number */
   pte_typ type; /* Type of the page */
   bool writable; /* True if writable */

   struct file *file; /* Reference to the file */
   size_t offset; /* Offset of the file */
   size_t read_bytes; /* Amount of the data written in the page */

   size_t swap_slot; /* Location of the swap slot */
   bool mem_flag; /* True if the page is in the memory */

   struct hash_elem elem; /* Hash element for page table */
   struct list_elem mmap_elem; /* List element for mmap list */
};

/* These six functions are interfaces of this header. The main user
   of this header is 'process.c', and 'syscall.c' uses this as well,
   especially in the subroutines of lazy loading implementation.  */

struct mmap_file {
   unsigned mapid; /*mapping id*/
   struct file *file; /*mapping file object*/
   struct list_elem elem; /*list element for mmap list*/
   struct list pte_list; /*list of page table entries*/
};

void page_table_init (struct hash *pt);
void page_table_destroy (struct hash *pt);
struct PTE *create_pte (void *vpn, pte_typ type, bool writable, struct file *file, \
   size_t offset, size_t read_bytes, bool mem_flag);
struct PTE *page_lookup (void *vpn);
bool page_insert_entry (struct hash *pt, struct PTE *pte);
bool page_delete_entry (struct hash *pt, struct PTE *pte);

#endif /* vm/page.h */