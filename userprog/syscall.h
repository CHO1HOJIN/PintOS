#include <stdbool.h>
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void check_user_vaddr(const void *vaddr);
void Halt (void);
void Exit (int status);
int Exec (const char *cmd_line);
int Wait (int pid);
bool Create (const char *file, unsigned initial_size);
bool Remove (const char *file);
int Open (const char *file);
int Filesize (int fd);
int Read (int fd, void *buffer, unsigned size);
int Write (int fd, const void *buffer, unsigned size);
void Seek (int fd, unsigned pos);
unsigned Tell (int fd);
void Close (int fd);
int Fibonacci (int n);
int Max_of_four_int (int a, int b, int c, int d);

#endif /* userprog/syscall.h */
