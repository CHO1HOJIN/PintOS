#include <string.h>
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

/* Returns a hash value for page p. */
static unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
  const struct PTE *p = hash_entry(p_, struct PTE, elem);
  return hash_bytes(&p->vpn, sizeof p->vpn);
}

/* Returns true if page a precedes page b. */
static bool page_cmp(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct PTE *a = hash_entry(a_, struct PTE, elem);
  const struct PTE *b = hash_entry(b_, struct PTE, elem);

  return a->vpn < b->vpn;
}

/* destroy function */
static void page_destroy(struct hash_elem *e, void *aux UNUSED) {
    struct PTE *p = hash_entry(e, struct PTE, elem);
    free_frame(pagedir_get_page (thread_current()->pagedir, p->vpn));
    swap_free(p->swap_slot);   
    free(p);
}

void page_table_init(struct hash *pt) {
  hash_init(pt, page_hash, page_cmp, NULL);
}

void page_table_destroy(struct hash *pt) {
  hash_destroy(pt, page_destroy);
}

struct PTE *create_pte(void *vpn, pte_typ type, bool writable, struct file *file, \
    size_t offset, size_t read_bytes, bool mem_flag) {
  struct PTE *pte = (struct PTE *)malloc(sizeof(struct PTE));
  if(pte == NULL) return NULL;
  memset(pte, 0, sizeof(struct PTE));

  pte->vpn = vpn;
  pte->type = type;
  pte->writable = writable;
  pte->file = file;
  pte->offset = offset;
  pte->read_bytes = read_bytes;
  pte->mem_flag = mem_flag;

  return pte;
}

struct PTE *page_lookup(void *vpn) {
  struct PTE p;
  struct hash_elem *e;

  p.vpn = pg_round_down(vpn);
  e = hash_find(&thread_current()->page_table, &p.elem);
  if (e == NULL) return NULL;
  else return hash_entry(e, struct PTE, elem);
}

bool page_insert_entry(struct hash *pt, struct PTE *pte) {
  struct hash_elem *e = &pte->elem;
  return hash_insert(pt, e);
}

bool page_delete_entry(struct hash *pt, struct PTE *pte) {
  struct hash_elem *e = &pte->elem;
  bool success = hash_delete(pt, e);
  if(!success) return false;
  free_frame(pagedir_get_page(thread_current()->pagedir, pte->vpn));
  swap_free(pte->swap_slot);
  free(pte);
  return success;

}
