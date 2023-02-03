#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

/* Need to modify syscall function names. e.g. open -> sysopen. Otherwise may have include errors. */

bool 
valid_user_pointer (void* user_pointer, unsigned size) 
{
  struct thread *cur = thread_current ();
  /* The user can pass a null pointer, a pointer to unmapped virtual memory, or a pointer to kernel virtual address space (above PHYS_BASE). */
  if (user_pointer == NULL)
  {
    return false;
  }
  /* A pointer to kernel virtual address space (above PHYS_BASE). */
  if (!is_user_vaddr ((void *) (user_pointer + size)))
  {
    return false;
  }
  /* A pointer to unmapped virtual memory. */
  if (pagedir_get_page (cur->pagedir, user_pointer) == NULL)
  {
    return false;
  }

  return true;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf ("system call!\n");
  /* System call number is pushed onto stack as uint32_t. */
  uint32_t* esp = f->esp;
  uint32_t syscall_number = (uint32_t)*esp;
  /* Use char* to get arguments. */
  if (syscall_number == SYS_EXIT)
  {
    
  }
  else if (syscall_number == SYS_WAIT)
  {
    
  }
  else if (syscall_number == SYS_OPEN)
  {
    /* Cast warning, why integer? But we have to cast to char* anyway. */
    char* file = (char*)*(esp + 1);
    f->eax = (uint32_t) sysopen (file, f->esp);
  }
  else if (syscall_number == SYS_WRITE)
  {
    int fd = *(esp + 1);
    void * buffer = (void *)*(esp + 2);
    unsigned size = (unsigned)*(esp + 1);
    f->eax = (uint32_t) syswrite (fd, buffer, size, f->esp);
  }

  // thread_exit ();
}

/* Thread TID should die here. */
void
sysexit (int status)
{
  struct thread *cur = thread_current ();
  sema_up (cur->sema_wait_for_child);
  cur->exit_status = status;
  thread_exit ();
}

int
syswait (int pid)
{
  /* Should be pid_t pid, but get an error: unknown type name ‘pid_t’. */
  return process_wait(pid);
}

int
sysopen (const char *file, uint8_t *esp)
{
  if (!valid_user_pointer (file, strlen (file)))
  {
    thread_exit ();
  }

  struct file *f = filesys_open (file);

  if(f == NULL)
  {
    /* -1 file discriptor if the file could not be opened. */
    return -1;
  }
  else
  {
    struct thread *cur = thread_current ();
    /* -1 0 1 are reserved. */
    int i = 2;
    while (cur->file_handlers[i])
    {
      i++;
    }
    /* Get a Null. */
    cur->file_handlers[i] = file;
    cur->file_handlers_number = i + 1;
    return i;
  }
}


int
syswrite (int fd, const void * buffer, unsigned size, uint8_t *esp)
{
  if (fd == 1)
  {
    putbuf (buffer, size);
    return size;
  }
  else
  {
    struct thread *cur = thread_current ();
    struct file *file = cur->file_handlers[fd];

    return file_write (file, buffer, size);
  }
}