#include <stdbool.h>
#include <stdint.h>
#include "threads/thread.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

bool valid_user_pointer (void* user_pointer, unsigned size);
void sysexit (int status);
int syswait (int pid);
int sysopen (const char *file);
int syswrite (int fd, const void * buffer, unsigned size);
tid_t sysexec (const char * cmd_line);
int get_valid_argument (int* esp, int i);
void syshalt (void);
bool syscreate (const char *file, unsigned initial_size);
bool sysremove (const char *file);
int sysfilesize (int fd);
int sysread (int fd, void *buffer, unsigned size);
void sysseek (int fd, unsigned position);
unsigned systell (int fd);
void sysclose (int fd);
bool child_is_waiting (struct exit_status_struct* child_es);

#endif /* userprog/syscall.h */
