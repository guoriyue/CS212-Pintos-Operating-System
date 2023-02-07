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
void halt (void);
bool create (const char *file, unsigned initial_size, uint8_t *esp);
bool remove (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

#endif /* userprog/syscall.h */
