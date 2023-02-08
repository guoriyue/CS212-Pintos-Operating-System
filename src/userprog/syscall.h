#include <stdbool.h>
#include <stdint.h>
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

bool valid_user_pointer (void* user_pointer, unsigned size);
void sysexit (int status);
int syswait (int pid);
int sysopen (const char *file, uint8_t *esp);
int syswrite (int fd, const void * buffer, unsigned size, uint8_t *esp);
void syshalt (void);
bool syscreate (const char *file, unsigned initial_size, uint8_t *esp);
bool sysremove (const char *file);
int sysfilesize (int fd);
int sysread (int fd, void *buffer, unsigned size);
void sysseek (int fd, unsigned position);
unsigned systell (int fd);
void sysclose (int fd);
tid_t sysexec (const char * cmd_line);

#endif /* userprog/syscall.h */
