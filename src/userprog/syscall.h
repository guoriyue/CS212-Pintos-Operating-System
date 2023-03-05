#include <stdbool.h>
#include <stdint.h>
#include "threads/thread.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

bool valid_user_pointer (void* user_pointer, unsigned size);
int get_valid_argument (int* esp, int i);
bool child_is_waiting (struct exit_status_struct* child_es);
void sysexit (int status);
int syswait (int pid);
int sysopen (const char *file, void* esp);
int syswrite (int fd, const void * buffer, unsigned size, void* esp);
tid_t sysexec (const char * cmd_line, void* esp);
void syshalt (void);
bool syscreate (const char *file, unsigned initial_size, void* esp);
bool sysremove (const char *file, void* esp);
int sysfilesize (int fd);
int sysread (int fd, void *buffer, unsigned size, void* esp);
void sysseek (int fd, unsigned position);
unsigned systell (int fd);
void sysclose (int fd);

#endif /* userprog/syscall.h */
