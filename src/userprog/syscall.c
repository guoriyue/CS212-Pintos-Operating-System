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
struct lock syscall_lock;

/* Need to modify syscall function names. e.g. open -> sysopen. Otherwise may have include errors. */
bool 
valid_user_pointer (void* user_pointer) 
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
  /* A pointer to unmapped virtual memory. */
  if (pagedir_get_page (cur->pagedir, user_pointer) == NULL)
  {
    return false;
  }

  return true;
}

bool
valid_address_within_size (const void * vaddr, unsigned size)
{
  if (vaddr == NULL || !is_user_vaddr (vaddr) || !is_user_vaddr (vaddr + size))
    return false;

  return true;
}

int
get_valid_argument (int* esp, int i)
{
  
  /* Validation. */
  if (!valid_user_pointer (esp))
    sysexit (-1);
  if (!valid_user_pointer (esp + i))
    sysexit (-1);

  /* For test case boundary 3. */
  if (!valid_user_pointer ((int *)((char*)(esp + i) + 1)))
    sysexit (-1);
  if (!valid_user_pointer ((int *)((char*)(esp + i) + 2)))
    sysexit (-1);
  if (!valid_user_pointer ((int *)((char*)(esp + i) + 3)))
    sysexit (-1);

  return *(esp + i);
}

// void*
// get_valid_pointer (void **esp, int i)
// {
//   /* Validation. */
//   if (!valid_user_pointer (esp))
//     sysexit (-1);
//   if (!valid_user_pointer (esp + i))
//     sysexit (-1);

//   if (!valid_user_pointer ((void **)((char*)esp + 1)))
//     sysexit (-1);
//   if (!valid_user_pointer ((void **)((char*)esp + 2)))
//     sysexit (-1);
//   if (!valid_user_pointer ((void **)((char*)esp + 3)))
//     sysexit (-1);

//   /* For test case boundary 3. */

//   return *(esp + i);
// }

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
  if (!get_valid_argument (esp, 0))
    sysexit (-1);
  // void** esp_for_pointer = (void**) f->esp;
  uint32_t syscall_number = (uint32_t)*esp;
  /* Use char* to get arguments. */
  if (syscall_number == SYS_EXIT)
  {
    // int status = (int)*(esp + 1);
    int status = (int)get_valid_argument (esp, 1);
    sysexit (status);
  }
  else if (syscall_number == SYS_EXEC)
  {
    // char* cmd_line = (char*)*(esp + 1);
    char* cmd_line = (char*)get_valid_argument (esp, 1);
    f->eax = (uint32_t) sysexec (cmd_line);
  }
  else if (syscall_number == SYS_WAIT)
  {
    // int pid = (int)*(esp + 1);
    int pid = (int)get_valid_argument (esp, 1);
    f->eax = (uint32_t) syswait (pid);
  }
  else if (syscall_number == SYS_OPEN)
  {
    /* Cast warning, why integer? But we have to cast to char* anyway. */
    // char* file = (char*)*(esp + 1);
    char* file = (char*)get_valid_argument (esp, 1);
    f->eax = (uint32_t) sysopen (file);
  }
  else if (syscall_number == SYS_WRITE)
  {
    // int fd = *(esp + 1);
    int fd = (int)get_valid_argument (esp, 1);
    // void * buffer = (void *)*(esp + 2);
    void * buffer = (void *)get_valid_argument (esp, 2);
    // unsigned size = (unsigned)*(esp + 3);
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
  }
  else
  {
    sysexit (-1);
  }

  // thread_exit ();
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
  // printf("%s: exit(%d)\n", thread_current()->name, status);

  // thread_exit();

  // /* Do not directly acquire cur->exit_status here, otherwise we may get an already freed exit_status. 
  //   For parent and children threads sync problem, we only maintain the children_exit_status_list to make it more simple. */
  // struct thread *cur = thread_current ();
  // cur->exit_status->exit_status = status;
  // /* It is possible for a parent to call exit() without waiting for its children. */
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
  // if (cur->exit_status->wait_for_child)
  // {
  //   sema_up (cur->exit_status->sema_wait_for_child);
  //   cur->exit_status->wait_for_child --;
  // }
  // struct thread *cur = thread_current ();
  // if (cur->parent)
  // {
  //   if (cur->exit_status)
  //   {
  //     sema_up (&cur->exit_status->sema_wait_for_child);
  //   }
  // }
  
  if (!cur->kernel)
  {
    /* Whenever a user process terminates, because it called exit or for any other reason, print the process's name and exit code. */
    printf ("%s: exit(%d)\n", thread_current ()->name, status);
  }
  thread_exit ();
  // sema_up (cur->exit_status->sema_wait_for_child);
}

int
sysopen (const char *file_name)
{
  if (!valid_address_within_size (file_name, strlen (file_name)))
    sysexit (-1);
  // if (!valid_user_pointer (file_name + strlen (file_name)))
  //   sysexit (-1);
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

int
syswrite (int fd, const void * buffer, unsigned size)
{
  if (!valid_user_pointer (buffer))
    sysexit (-1);
  if (!valid_user_pointer (buffer + size))
    sysexit (-1);
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
  if (!valid_user_pointer (cmd_line))
    sysexit (-1);

  if (!valid_user_pointer (cmd_line + strlen(cmd_line)))
    sysexit (-1);
  tid_t pid = process_execute (cmd_line);
  return pid;
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
  // if (!valid_user_pointer ((void *) file) || !valid_user_pointer ((void *) file + initial_size))
  // {
  //   sysexit(-1);
  // }
  if (!valid_address_within_size (file, initial_size))
    sysexit (-1);

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
  // if (!valid_user_pointer (buffer) || !valid_user_pointer (buffer + size))
  // {
  //   sysexit (-1);
  // }
  if (!valid_address_within_size (buffer, size))
    sysexit (-1);
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
}

void 
sysseek (int fd, unsigned position) 
{
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  file_seek(f, position);
}

unsigned 
systell (int fd) 
{
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  return file_tell(f);
}

void
sysclose (int fd) 
{
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  file_close (f);
}