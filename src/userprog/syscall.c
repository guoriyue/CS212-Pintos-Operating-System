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
#include "filesys/file.h"
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
  // printf ("system call! system call! system call!\n");
  /* System call number is pushed onto stack as uint32_t. */
  int* esp = f->esp;
  uint32_t syscall_number = (uint32_t)*esp;
  /* Use char* to get arguments. */
  if (syscall_number == SYS_EXIT)
  {
    int status = (int)*(esp + 1);
    sysexit (status);
  }
  if (syscall_number == SYS_EXEC)
  {
    char* cmd_line = (char*)*(esp + 1);
    f->eax = (uint32_t) sysexec (cmd_line);
  }
  else if (syscall_number == SYS_WAIT)
  {
    int pid = (int)*(esp + 1);
    f->eax = (uint32_t) syswait (pid);
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
    unsigned size = (unsigned)*(esp + 3);
    f->eax = (uint32_t) syswrite (fd, buffer, size, f->esp);
  }

  // thread_exit ();
}

void
sysexit (int status)
{
  struct thread *cur = thread_current ();

  cur->exit_status->exit_status = status;
  if (cur->status == THREAD_BLOCKED)
  {
    sema_up (cur->exit_status->sema_wait_for_child);
  }
  
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

int
sysopen (const char *file_name, uint8_t *esp)
{
  struct file *f = filesys_open (file_name);
  if (f)
  {
    struct thread* cur = thread_current ();
    struct file** tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
    memcpy (tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file*));
    free (cur->file_handlers);

    cur->file_handlers = tmp;
    cur->file_handlers[cur->file_handlers_number] = f;
    int ret_file_handlers_number = cur->file_handlers_number;
    cur->file_handlers_number++;
    return ret_file_handlers_number;
  }
  return -1;
}

int
syswait (int pid)
{
   return process_wait (pid);
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
    int ans;
    struct thread *cur = thread_current ();
    struct file *f = cur->file_handlers[fd];
    if (f == NULL) ans = -1;
    else ans = file_write (f, buffer, size);
    return ans;
  }
}

tid_t sysexec (const char * cmd_line)
{
  tid_t pid = process_execute (cmd_line);
  return pid;
}