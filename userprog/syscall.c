#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "process.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock;

void syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void check_user_vaddr(const void *vaddr) {
  if (!is_user_vaddr(vaddr)) Exit(-1);
}

bool is_valid_file_descrpitor(int fd){
  if(fd < 3 || fd >= 128) return false;
  if(thread_current()->fd_table[fd] == NULL) return false;
  return true;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // f->esp points to # of syscall
  // printf("syscall_number: %d\n", *(uint32_t *)(f->esp));
  switch (*(uint32_t *)(f->esp)) {
    case SYS_HALT:
      Halt();
      break;
    case SYS_EXIT:
      check_user_vaddr(f->esp + 4);
      Exit(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_EXEC:
      check_user_vaddr(f->esp + 4);
      f->eax = Exec((const char *)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_WAIT:
      check_user_vaddr(f->esp + 4);
      f->eax = Wait(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_CREATE:
      check_user_vaddr(f->esp + 16);
      check_user_vaddr(f->esp + 20);
      f->eax = Create((const char *)*(uint32_t *)(f->esp + 16), (unsigned)*(uint32_t *)(f->esp + 20));
      break;
    case SYS_REMOVE:
      check_user_vaddr(f->esp + 4);
      f->eax = Remove((const char *)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_OPEN:
      check_user_vaddr(f->esp + 4);
      f->eax = Open((const char *)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_FILESIZE:
      check_user_vaddr(f->esp + 4);
      f->eax = Filesize((int)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_READ:
      check_user_vaddr(f->esp + 4);
      check_user_vaddr(f->esp + 8);
      check_user_vaddr(f->esp + 12);
      f->eax = Read((int)*(uint32_t*)(f->esp + 4), (void*)*(uint32_t*)(f->esp + 8), (unsigned)*(uint32_t*)(f->esp + 12));
      break;
    case SYS_WRITE:
      check_user_vaddr(f->esp + 4);
      check_user_vaddr(f->esp + 8);
      check_user_vaddr(f->esp + 12);
      f->eax = Write((int)*(uint32_t *)(f->esp + 4),(const void *)*(uint32_t *)(f->esp + 8), (unsigned)*(uint32_t *)(f->esp + 12));
      break;
    case SYS_SEEK:
      check_user_vaddr(f->esp + 16);
      check_user_vaddr(f->esp + 20);
      Seek(*(uint32_t *)(f->esp + 16), *(uint32_t *)(f->esp + 20));
      break;
    case SYS_TELL:
      check_user_vaddr(f->esp + 4);
      f->eax = Tell(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_CLOSE:
      check_user_vaddr(f->esp + 4);
      Close(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_FIBONACCI:
      f->eax = Fibonacci(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_MAX_OF_FOUR_INT:
      f->eax = Max_of_four_int(*(uint32_t *)(f->esp + 4), *(uint32_t *)(f->esp + 8), *(uint32_t *)(f->esp + 12), *(uint32_t *)(f->esp + 16));
      break;
    default:
      break;
  }
  // printf ("system call!\n");
  // thread_exit ();
}

/// 3.3.4 System Calls (p.29)

/// 1) halt: terminates pintos by calling shutdown_power_off()
void Halt(){
  shutdown_power_off();
}

/// 2) exit: terminates the current user program, returning status to the kernel
/// success: 0, error: nonzero
/// Refer to 3.3.2,  print exit message "process_name: exit(status)"
void Exit(int status){
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_current()->exit_status = status;
  for(int i = 3; i < 128; i++) if(is_valid_file_descrpitor(i)) Close(i);

  thread_exit();
}

/// 3) exec: runs the executable whose name is given in cmd_line
/// return pid or -1 if the program cannot load or run for any reason
int Exec(const char *cmd_line){
  return process_execute(cmd_line);
}

/// 4) wait: waits for a child process pid and retrieves the child's exit status
/// return the status that was passed to exit
int Wait (int pid){
  return process_wait(pid);
}

// 5) create: creates a new file called file with initial_size bytes
// return true if successful, false otherwise
bool Create (const char *file, unsigned size) {
  if(file == NULL) Exit(-1);
  return filesys_create(file, size);
}

// 6) remove: deletes the file called file
// return true if successful, false otherwise
bool Remove (const char *file){
  if(file == NULL) Exit(-1);
  return filesys_remove(file);
} 

// 7) open: opens the file called file
// return a nonnegative integer handle called a "file descriptor" (fd)
int Open (const char *file) {
  if(file == NULL) Exit(-1);
  lock_acquire(&filesys_lock);
  struct file *f = filesys_open(file);
  if(f == NULL){
    lock_release(&filesys_lock);
    return TID_ERROR;
  }
  else {
    for(int i = 3; i < 128; i++){
      if(thread_current()->fd_table[i] == NULL) {
        if(strcmp(thread_current()->name, file) == NULL) file_deny_write(f);
        thread_current()->fd_table[i] = f;
        lock_release(&filesys_lock);
        return i;
      }
    }
  }
  lock_release(&filesys_lock);
  return TID_ERROR;
}

// 8) filesize: returns the size, in bytes, of the file open as fd
int Filesize (int fd) {
  if(!is_valid_file_descrpitor(fd)) return TID_ERROR;
  if (fd == 0) return TID_ERROR;
  return file_length(thread_current()->fd_table[fd]);
}

/// 9) read: reads size bytes from the file open as fd into buffer
/// return: # of bytes actually read (STDIN = 0), -1 if error
/// if fd > 2, read from file
int Read (int fd, void *buffer, unsigned size){
  lock_acquire(&filesys_lock);
  check_user_vaddr(buffer);
  unsigned i;
  //fd = 0: reads from keyboard using input_getc()
  if (fd == 0) {
    for (i = 0; i < size; i++){
      ((uint8_t *)buffer)[i] = input_getc();
      if (((char *)buffer)[i] == '\0') break;
    }
    lock_release(&filesys_lock);
    return i;
  }

  else if (fd > 2 && fd < 128){
    if(!is_valid_file_descrpitor(fd)){
      lock_release(&filesys_lock);
      Exit(-1);
    }
    int r = file_read(thread_current()->fd_table[fd], buffer, size);
    lock_release(&filesys_lock);
    return r;
  }

  lock_release(&filesys_lock);
  Exit(-1);
}

/// 10) write: writes size bytes from buffer to the open file fd
/// return # of bytes actually written, -1 if error
/// if fd > 3, write to file
int Write (int fd, const void *buffer, unsigned size){

  lock_acquire(&filesys_lock);
  check_user_vaddr(buffer);

  //fd = 1: writes to the console
  if (fd == 1) {
    putbuf(buffer, size);
    lock_release(&filesys_lock);
    return size;
  }
  else if (fd > 2 && fd < 128){
    if(!is_valid_file_descrpitor(fd)){
      lock_release(&filesys_lock);
      Exit(-1);
    }
    int r = file_write(thread_current()->fd_table[fd], buffer, size);
    lock_release(&filesys_lock);
    return r;
  }
  else {
    lock_release(&filesys_lock);
    Exit(-1);
  }
}
// 11) seek: changes the next byte to be read or written in open file fd to position
void Seek (int fd, unsigned pos) {
  if (!is_valid_file_descrpitor(fd)) Exit(-1);
  file_seek(thread_current()->fd_table[fd], pos);
}

// 12) tell: returns the position of the next byte to be read or written in open file fd
unsigned Tell (int fd) {
  if (!is_valid_file_descrpitor(fd)) Exit(-1);
  return file_tell(thread_current()->fd_table[fd]);
}

// 13) close: closes file descriptor fd
void Close (int fd){

  if(!is_valid_file_descrpitor(fd)) Exit(-1);

  file_close(thread_current()->fd_table[fd]);
  thread_current()->fd_table[fd] = NULL;
}

int Fibonacci(int n) {
  if (n == 0) return 0;
  if (n == 1) return 1;
  int f1, f2, f3, i;

  f1 = 0; 
  f2 = 1;
  for (i = 1; i < n; i++) {
    f3 = f1 + f2;
    f1 = f2;
    f2 = f3;
  }
  return f3;
} 

int Max_of_four_int(int a, int b, int c, int d) {
  int M = a;
  if (b > M) M = b;
  if (c > M) M = c;
  if (d > M) M = d;
  return M;
}
