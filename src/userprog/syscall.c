#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
static void syscall_handler (struct intr_frame *);

bool 
valid_user_pointer (void* user_pointer, unsigned size) 
{
  struct thread *t = thread_current ();
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
  if (pagedir_get_page (t->pagedir, user_pointer) == NULL)
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
  uint32_t* esp_uint32_t = (uint32_t*) f->esp;
  uint32_t syscall_number = *esp_uint32_t;
  /* Use char* to get arguments. */
  char* esp_char = (char*) f->esp;
  if (syscall_number == SYS_OPEN)
  {
    char* arg = *(esp_char + 4);
    f->eax = (uint32_t) open (arg, f->esp);
  }

  // thread_exit ();
}

int
open (const char *file, uint8_t *esp)
{
  if (!valid_user_pointer (file, strlen (file)))
  {
    /* TODO */
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

  }
  return 1;
}
