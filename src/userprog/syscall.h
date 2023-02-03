#include <stdbool.h>
#include <stdint.h>
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

bool valid_user_pointer (void* user_pointer, unsigned size);
void sysexit (int status);
int syswait (int pid);
int sysopen (const char *file, uint8_t *esp);
int syswrite (int fd, const void * buffer, unsigned size, uint8_t *esp);
#endif /* userprog/syscall.h */
