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
#include "devices/shutdown.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "threads/thread.h"


// always check that buffer is valid (every syscall) - make sure pointer is valid 

static void syscall_handler (struct intr_frame *);
struct lock syscall_lock;

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
  if (!is_user_vaddr ((void *) (user_pointer)))
  {
    return false;
  }
  /* Pointer of end of buffer to kernel virtual address space (above PHYS_BASE). */
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

int
get_valid_argument (int* esp, int i)
{
  
  /* Validation. */
  if (!valid_user_pointer (esp, 0))
    sysexit (-1);
  if (!valid_user_pointer (esp + i, 0))
    sysexit (-1);

  /* For test case boundary 3. */
  if (!valid_user_pointer ((int *)((char*)(esp + i) + 1), 0))
    sysexit (-1);
  if (!valid_user_pointer ((int *)((char*)(esp + i) + 2), 0))
    sysexit (-1);
  if (!valid_user_pointer ((int *)((char*)(esp + i) + 3), 0))
    sysexit (-1);

  return *(esp + i);
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
  if (!get_valid_argument (esp, 0)) sysexit (-1);
  uint32_t syscall_number = (uint32_t)*esp;
  /* Use char* to get arguments. */
  if (syscall_number == SYS_EXIT)
  {
    int status = (int) get_valid_argument (esp, 1);
    sysexit (status);
  }
  else if (syscall_number == SYS_EXEC)
  {
    char* cmd_line = (char*)get_valid_argument (esp, 1);
    f->eax = (uint32_t) sysexec (cmd_line);
  }
  else if (syscall_number == SYS_WAIT)
  {
    int pid = (int)get_valid_argument (esp, 1);
    f->eax = (uint32_t) syswait (pid);
  }
  else if (syscall_number == SYS_OPEN)
  {
    /* Cast warning, why integer? But we have to cast to char* anyway. */
    char* file = (char*)get_valid_argument (esp, 1);
    f->eax = (uint32_t) sysopen (file);
  }
  else if (syscall_number == SYS_WRITE)
  {
    int fd = (int)get_valid_argument (esp, 1);
    void * buffer = (void *)get_valid_argument (esp, 2);
    unsigned size = (unsigned)get_valid_argument (esp, 3);
    f->eax = (uint32_t) syswrite (fd, buffer, size);
  }
  else if (syscall_number == SYS_HALT)
  {
    syshalt();
  }
  else if (syscall_number == SYS_CREATE)
  {
    char* file = (char*)get_valid_argument (esp, 1);
    unsigned initial_size = (unsigned)get_valid_argument (esp, 2);
    f->eax = (uint32_t) syscreate(file, initial_size, f->esp);
  }
  else if (syscall_number == SYS_REMOVE)
  {
    char* file = (char*)get_valid_argument (esp, 1);
    f->eax = (uint32_t) sysremove (file);
  }
  else if (syscall_number == SYS_FILESIZE)
  {
    int fd = get_valid_argument (esp, 1);
    f->eax = (uint32_t) sysfilesize (fd);
  }
  else if (syscall_number == SYS_READ)
  {
    int fd = get_valid_argument (esp, 1);
    void * buffer = (void *)get_valid_argument (esp, 2);
    unsigned size = (unsigned)get_valid_argument (esp, 3);
    f->eax = (uint32_t) sysread (fd, buffer, size);
  }
  else if (syscall_number == SYS_SEEK)
  {
    int fd = get_valid_argument (esp, 1);
    unsigned position = (unsigned) get_valid_argument (esp, 2);
    sysseek (fd, position);
  }
  else if (syscall_number == SYS_TELL)
  {
    int fd = get_valid_argument (esp, 1);
    f->eax = (uint32_t) systell (fd);
  }
  else if (syscall_number == SYS_CLOSE)
  {
    int fd = get_valid_argument (esp, 1);
    sysclose (fd);
  } else {
    sysexit (-1);
  }
}

bool
child_is_waiting (struct exit_status_struct* child_es)
{
  struct thread *cur = thread_current ();
  /* No need to acquire lock because we call this in cur->list_lock. */
  struct list_elem *e;
  for (e = list_begin (&cur->parent->children_exit_status_list); e != list_end (&cur->parent->children_exit_status_list);
       e = list_next (e))
    {
      struct exit_status_struct* es = list_entry (e, struct exit_status_struct, exit_status_elem);
      if (es->process_id == child_es->process_id) {
        return true;
      }
    }
  return false;
}

void
sysexit (int status)
{
  struct thread *cur = thread_current ();
  lock_acquire (&cur->list_lock);
  
  if (child_is_waiting (cur->exit_status))
  {
    cur->exit_status->exit_status = status;
    sema_up (&cur->exit_status->sema_wait_for_child);
  }
  else
  {
    cur->exit_status->terminated = 1;
  }
  lock_release (&cur->list_lock);
  if (!cur->kernel)
  {
    /* Whenever a user process terminates, because it called exit or for any other reason, print the process's name and exit code. */
    printf ("%s: exit(%d)\n", thread_current ()->name, status);
  }
  thread_exit ();
}

int
sysopen (const char *file_name)
{
  if (!valid_user_pointer (file_name, 0))
    sysexit (-1);
  if (!valid_user_pointer (file_name, strlen (file_name)))
    sysexit (-1);
  if (!file_name)
    sysexit (-1);
  
  struct file *f = filesys_open (file_name);
  if (f)
  {
    struct thread* cur = thread_current ();
    struct file** tmp;
    if (cur->file_handlers_number == 2)
    {
      // struct file** 
      tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
      memset (tmp, 0, 3 * sizeof(struct file*));
      // cur->file_handlers = tmp;
    }
    else
    {
      // struct file** 
      tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
      memcpy (tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file*));
      free (cur->file_handlers);

      // cur->file_handlers = tmp;
      // cur->file_handlers[cur->file_handlers_number] = f;
      // int ret_file_handlers_number = cur->file_handlers_number;
      // cur->file_handlers_number++;
      // return ret_file_handlers_number;
    }
    cur->file_handlers = tmp;
    cur->file_handlers[cur->file_handlers_number] = f;
    int ret_file_handlers_number = cur->file_handlers_number;
    cur->file_handlers_number++;
    return ret_file_handlers_number;
  }
  // else
  // {
  //   sysexit (-1);
  // }
  return -1;
}

int
syswait (int pid)
{
   return process_wait (pid);
}

// could check here whether file is executable and deny writes (if other implementation doesn't work)
int
syswrite (int fd, const void * buffer, unsigned size)
{
  if (!valid_user_pointer (buffer, 0))
    sysexit (-1);
  if (!valid_user_pointer (buffer, size))
    sysexit (-1);
  if (fd == 0) sysexit(-1);
  if (!valid_user_pointer ((void *) buffer, size))
  {
    sysexit(-1);
  }
  if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit(-1);
  }
  int ans;
  if (fd == 1)
  {
    putbuf (buffer, size);
    ans = size;
  }
  else
  {
    struct thread *cur = thread_current ();
    struct file *f = cur->file_handlers[fd];
    if (f == NULL) sysexit(-1);
    else if (f->deny_write == true) ans = 0;
    else ans = file_write (f, buffer, size);
  }
  return ans;
  // kernel panik
}

tid_t 
sysexec (const char * cmd_line)
{
  if (!valid_user_pointer (cmd_line, 0))
    sysexit (-1);

  if (!valid_user_pointer (cmd_line, strlen(cmd_line)))
    sysexit (-1);
  if (!valid_user_pointer ((void *) cmd_line, 0))
  {
    sysexit(-1);
  }
  tid_t pid = process_execute (cmd_line);
  return pid;
  // kernel panik
}

void 
syshalt (void) 
{
  shutdown_power_off();
}

bool 
syscreate (const char *file, unsigned initial_size, uint8_t *esp) 
{
  bool ans = false;
  // , initial_size
  if (!valid_user_pointer ((void *) file, 0) || !valid_user_pointer ((void *) file, initial_size))
  {
    sysexit(-1);
  }
  // if (!valid_address_within_size (file, initial_size))
  //   sysexit (-1);

  if (file == NULL) {
    sysexit(-1);
  } else {
    ans = filesys_create (file, initial_size);
  }
  return ans;
}

bool 
sysremove (const char *file) 
{
  return filesys_remove(file);
}

int 
sysfilesize (int fd) 
{
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  return (int) file_length (f); 
}

int 
sysread (int fd, void *buffer, unsigned size) 
{
  if (!valid_user_pointer (buffer, 0) || !valid_user_pointer (buffer, size))
  {
    sysexit (-1);
  }
  // if (!valid_address_within_size (buffer, size))
  //   sysexit (-1);
  // for (int i=0; i <= size; i++)
  // {
  //   if (!valid_user_pointer (buffer + i))
  //     sysexit (-1);
  // }

  int ans = -1;
  if (fd == 0) {
    char *input_buffer = (char *) buffer;
    for (unsigned i = 0; i < size; i++) {
      uint8_t input_char = input_getc();
      input_buffer[i] = input_char;
    }
    ans = size; 
  } else {
    struct thread *cur = thread_current ();
    struct file *f = cur->file_handlers[fd];
    if (f == NULL) ans = -1;
    else ans = file_read (f, buffer, size);
  }
  return ans;
  // kernel panik
}

void 
sysseek (int fd, unsigned position) 
{
  if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit(-1);
  }
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  file_seek(f, position);
}

unsigned 
systell (int fd) 
{
  if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit(-1);
  }
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  return file_tell(f);
}

void
sysclose (int fd) 
{
  if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit(-1);
  }
  if (fd == 1) sysexit(-1);
  if (fd == 0) sysexit(-1);
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  file_close (f);
  cur->file_handlers_number--;
  // close and free stuff
  // kernel panik
}
