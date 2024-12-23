#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/syscall.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
extern struct lock filesys_lock;
/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  
  // Split file_name and cmd_name
  char cmd[128];
  strlcpy(cmd, file_name, strlen(file_name) + 1);
  char *save_ptr;
  char* temp = strtok_r(cmd, " ", &save_ptr);
  if(filesys_open(cmd) == NULL) return TID_ERROR;

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (cmd, PRI_DEFAULT, start_process, fn_copy);
  struct thread *t;
  struct list_elem *e;
  struct thread *child_t = NULL;
  
  for(e = list_begin(&thread_current()->children); e != list_end(&thread_current()->children); e = list_next(e)) {
    t = list_entry(e, struct thread, child);
    if(t->tid == tid) {
      child_t = t;
      break;
    }
  }

  sema_down(&child_t->load_lock);

  if (child_t->flag == 0) return -1;
  if (tid == TID_ERROR) palloc_free_page (fn_copy);

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize the set of vm_entries*/
  page_table_init(&(thread_current()->page_table));

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  //Wait for finish loading
  sema_up(&(thread_current()->load_lock));

  /* If load failed, quit. */
  palloc_free_page (file_name);

  // If load failed, exit
  if (!success) {
    thread_current()->flag = 0;
    Exit(-1);
  }
  // If load success, set flag to 1
  else thread_current()->flag = 1;

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *cur = thread_current();
  // struct thread *child_ptr = NULL;
  struct list_elem *e;
  int exit_status = -1;
  
  for (e = list_begin(&(cur->children)); e != list_end(&(cur->children)); e = list_next(e)) {
    
    struct thread *child_ptr = list_entry(e, struct thread, child);
    // If child_tid is found, wait for the child to exit
    if (child_ptr->tid == child_tid) {
      sema_down(&(child_ptr->child_lock));
      exit_status = child_ptr->exit_status;
      list_remove(&(child_ptr->child));
      sema_up(&(child_ptr->mem_lock));
      break;
    }
  }
  
  //DEBUG
  // for (int i = 0; i < 2000000000; i++);

  return exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  
  uint32_t *pd;
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  // palloc_free_page (thread_current()->pagedir);
  for(unsigned i = 0; i <= thread_current()->max_mapid; i++) Munmap(i);
  /*pte delete*/
  file_close(thread_current()->file);
  page_table_destroy(&(thread_current()->page_table));

  pd = thread_current()->pagedir;

  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      thread_current()->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  sema_up(&(thread_current()->child_lock));
  sema_down(&(thread_current()->mem_lock));
}


/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

void stack_constructor(int argc, char* argv[], void **esp);
static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */

/*construct stack*/
void stack_constructor(int argc, char* argv[], void **esp){
  int argv_addr[128];
  int word_align = 0;
  int i;
  int len;

  // push argv[i]
  for (i = argc - 1; i >=0; --i){
    len = strlen(argv[i]) + 1; // +1 for NULL
    word_align += len;
    *esp -= len;

    // DEBUG
    // printf("argv[%d]: %s, esp: %p\n", i, argv[i], *esp);

    memcpy(*esp, argv[i], len);
    argv_addr[i] = (int)*esp;
  }

  // padding
  word_align = (4 - word_align % 4) % 4;

  //DEBUG
  // printf("word_align: %d\n", word_align);

  if (word_align != 0){
    *esp -= word_align;
    memset(*esp, 0, word_align); 
  }
  
  // argv[argc] = NULL
  *esp -= 4;
  memset(*esp, 0, 4); 

  // push argv[i] address
  for (i = argc - 1; i >= 0; --i){
    *esp -= 4;
    memcpy(*esp, &argv_addr[i], 4); 
  }
  
  // push &argv[0]
  memcpy(*esp - 4, esp, 4);
  // push argc
  memcpy(*esp - 8, &argc, 4);
  // push return address 
  memset(*esp - 12, 0, 4);

  *esp -= 12;

  // DEBUG
  // hex_dump((uintptr_t)*esp, *esp, 100, 1);
}

/// TODO
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  /// 1) Parse File Name

  char* token;
  char *save_ptr;
  char *argv[128];
  int argc = 0;
  char file_name_copied[128];

  snprintf(file_name_copied, sizeof(file_name_copied), "%s", file_name);

  i = 0;
  token = strtok_r(file_name_copied, " ", &save_ptr);

  do{
    argv[i++] = token;
    token = strtok_r(NULL, " ", &save_ptr);
  }while(token != NULL);
  

  argc = i;
  lock_acquire(&filesys_lock);
  file = filesys_open (argv[0]);
  if (file) t->file = file;
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }


  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  //TODO
  if (!setup_stack (esp))
    goto done;
  
  /// construct stack with arguments
  stack_constructor(argc, argv, esp);
  
  // printf ("load: %s: done\n", file_name);

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  lock_release(&filesys_lock);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct PTE* pte = create_pte(upage, LOAD, writable, file, ofs, page_read_bytes, false);
      if(pte == NULL) return false;
      page_insert_entry(&(thread_current()->page_table), pte);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  struct frame *f = alloc_page_to_frame(PAL_USER | PAL_ZERO);
  if(f == NULL) return false;

  bool success = false;
  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, f->pfn, true);

  if(success){
    *esp = PHYS_BASE;

    // create a page table entry for the stack
    f->pte = create_pte(((uint8_t *) PHYS_BASE) - PGSIZE, SWAP, true, NULL, 0, 0, true);
    if(f->pte == NULL) return false;
    page_insert_entry(&(thread_current()->page_table), f->pte);
  }
  else
    free_frame(f->pfn);
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  // printf("install_page, upage: %p, kpage: %p\n", upage, kpage);
  
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool
handle_mm_fault (struct PTE *pte) {
  struct frame *f = alloc_page_to_frame(PAL_USER);
  // if(f == NULL) return false;
  f->pte = pte;
  bool success = false;

  // simply load from the same file in the disk
  if (pte->type == LOAD || pte->type == MEMMAP) {
    if (load_to_frame(f->pfn, pte)) {
      success = install_page(pte->vpn, f->pfn, pte->writable);
    }
  }
  // if from swap space, but not in the memory yet, swap in
  else if (pte->type == SWAP){
    swap_in(pte->swap_slot, f->pfn);
    success = install_page(pte->vpn, f->pfn, pte->writable);
  }

  // if load fails, free the frame
  if (success) pte->mem_flag = true;
  else free_frame(f->pfn);

  return success;
}

bool
stack_growth (void *addr, void *esp) {
  // check if the address is valid
  // and check if the address is below PHYS_BASE - 0x800000
  // if address is below PHYS_BASE - 0x800000, then it cannot be a stack growth
  if (!is_user_vaddr(addr) || addr < PHYS_BASE - 0x800000) return false;
  // check if the address is below PHYS_BASE - 32 (qualify the PUSHA instruction)
  if (addr < esp -32) return false;

  // get boundary
  void *upage = pg_round_down(addr);

  struct frame *f = alloc_page_to_frame(PAL_USER | PAL_ZERO);
  if(f == NULL) return false;

  bool success = install_page(upage, f->pfn, true);
  if(success){
    f->pte = create_pte(upage, SWAP, true, NULL, 0, 0, true);
    if(f->pte == NULL) return false;
    page_insert_entry(&(thread_current()->page_table), f->pte);
  }
  else
    free_frame(f->pfn);

  return success;
}