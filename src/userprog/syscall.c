#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include <stdio.h>
#include <round.h>
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
#include "threads/palloc.h"
#include "devices/input.h"
#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static void syscall_handler (struct intr_frame *);

/* This function checks whether a user pointer passed into a function
  as a parameter (for a syscall) is actually valid (i.e. not null,
  not in kernel memory, and mapped). */
bool 
valid_user_pointer (void* user_pointer, unsigned size) 
{
  struct thread *cur = thread_current ();
  /* Null pointer. */
  if (user_pointer == NULL)
  {
    return false;
  }
  /* A pointer to kernel virtual address space (above PHYS_BASE). */
  if (!is_user_vaddr ((void *) (user_pointer)))
  {
    return false;
  }
  /* Pointer of end of buffer to kernel virtual 
    address space (above PHYS_BASE). */
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

/* This function makes sure that the command line arguments 
  passed in are valid. */
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

/* Call the respective syscall based on the syscall number. 
  Make sure it is called in a critical section to avoid 
  race conditions. */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* System call number is pushed onto stack as uint32_t. */
  int* esp = f->esp;
  if (!get_valid_argument (esp, 0)) sysexit (-1);
  uint32_t syscall_number = (uint32_t)*esp;
  /* Use char* to get arguments. */
  struct lock sys_lock;
  lock_init(&sys_lock);
  if (syscall_number == SYS_EXIT)
  {
    int status = (int) get_valid_argument (esp, 1);
    sysexit (status);
  }
  else if (syscall_number == SYS_EXEC)
  {
    lock_acquire(&sys_lock);
    char* cmd_line = (char*) get_valid_argument (esp, 1);
    f->eax = sysexec (cmd_line);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_WAIT)
  {
    int pid = (int)get_valid_argument (esp, 1);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) syswait (pid);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_OPEN)
  {
    lock_acquire(&sys_lock);
    char* file = (char*)get_valid_argument (esp, 1);
    
    f->eax = (uint32_t) sysopen (file);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_WRITE)
  {
    int fd = (int)get_valid_argument (esp, 1);
    void * buffer = (void *)get_valid_argument (esp, 2);
    unsigned size = (unsigned)get_valid_argument (esp, 3);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) syswrite (fd, buffer, size);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_HALT)
  {
    lock_acquire(&sys_lock);
    syshalt();
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_CREATE)
  {
    lock_acquire(&sys_lock);
    char* file = (char*)get_valid_argument (esp, 1);
    unsigned initial_size = (unsigned)get_valid_argument (esp, 2);
    
    f->eax = (uint32_t) syscreate(file, initial_size);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_REMOVE)
  {
    char* file = (char*)get_valid_argument (esp, 1);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) sysremove (file);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_FILESIZE)
  {
    int fd = get_valid_argument (esp, 1);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) sysfilesize (fd);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_READ)
  {
    int fd = get_valid_argument (esp, 1);
    void * buffer = (void *)get_valid_argument (esp, 2);
    unsigned size = (unsigned)get_valid_argument (esp, 3);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) sysread (fd, buffer, size);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_SEEK)
  {
    int fd = get_valid_argument (esp, 1);
    unsigned position = (unsigned) get_valid_argument (esp, 2);
    lock_acquire(&sys_lock);
    sysseek (fd, position);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_TELL)
  {
    int fd = get_valid_argument (esp, 1);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) systell (fd);
    lock_release(&sys_lock);
  }
  else if (syscall_number == SYS_CLOSE)
  {
    int fd = get_valid_argument (esp, 1);
    lock_acquire(&sys_lock);
    sysclose (fd);
    lock_release(&sys_lock);
  } 
  else if (syscall_number == SYS_MMAP)
  {
    int fd = get_valid_argument (esp, 1);
    void * addr = (void *)get_valid_argument (esp, 2);
    lock_acquire(&sys_lock);
    f->eax = (uint32_t) sysmmap (fd, addr);
    lock_release(&sys_lock);
    
  } 
  else if (syscall_number == SYS_MUNMAP)
  {
    mapid_t mapping = (mapid_t) get_valid_argument (esp, 1);
    lock_acquire(&sys_lock);
    sysmunmap (mapping);
    lock_release(&sys_lock);
  } 
  else {
    sysexit (-1);
  }
}

/* If a child process is in its parent's child_exit_status list, the parent is 
  still waiting for it, which means we need to call sema_up and resume the parent 
  process. */
bool
child_is_waiting (struct exit_status_struct* child_es)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->parent->children_exit_status_list); 
      e != list_end (&cur->parent->children_exit_status_list);
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
    printf ("%s: exit(%d)\n", thread_current ()->name, status);
  }
  thread_exit ();
}

int
sysopen (const char *file_name)
{
  if (!valid_user_pointer ((void *) file_name, 0)) 
    sysexit (-1);
  if (!valid_user_pointer ((void *) file_name, strlen (file_name)))
    sysexit (-1);
  if (!file_name)
    sysexit (-1);

  char* ret = (char *) file_name;
  while (*ret) {
    if (ret + 1)
    {
      if (!valid_user_pointer (++ret, 0))
        sysexit (-1);
    }
    else
    {
      sysexit (-1);
    }
  }
  struct file *f = filesys_open (file_name);
  if (f)
  {
    struct thread* cur = thread_current ();
    struct file** tmp;
    if (cur->file_handlers_number == 2)
    {
      tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
      memset (tmp, 0, 3 * sizeof(struct file*));
    }
    else
    {
      tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
      memcpy (tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file*));
      free (cur->file_handlers);
    }
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
syswrite (int fd, const void * buffer, unsigned size)
{
  if (!valid_user_pointer ((void *) buffer, 0))
    sysexit (-1);
  if (!valid_user_pointer ((void *) buffer, size))
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
    if (fd >= cur->file_handlers_number)
    {
      sysexit(-1);
    }
    else 
    {
      ans = file_write (f, buffer, size);
    }
  }
  return ans;
}

tid_t 
sysexec (const char * cmd_line)
{
  if (!valid_user_pointer ((void *) cmd_line, 0))
    sysexit (-1);

  char* ret = (char *) cmd_line;
  while (*ret) {
    if (!valid_user_pointer (++ret, 0))
      sysexit (-1);
  }

  tid_t pid = process_execute (cmd_line);
  return pid;
}

void 
syshalt (void) 
{
  shutdown_power_off();
}

bool 
syscreate (const char *file, unsigned initial_size) 
{

  bool ans = false;
  if (!valid_user_pointer ((void *) file, 0) || 
      !valid_user_pointer ((void *) file, initial_size))
  {
    sysexit(-1);
  }


  char* ret = (char *) file;
  while (*ret) {
    if (!valid_user_pointer (++ret, 0))
      sysexit (-1);
  }

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
  char* ret = (char *) file;
  while (*ret) {
    if (!valid_user_pointer (++ret, 0))
      sysexit (-1);
  }
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
  if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit(-1);
  }
  if (!valid_user_pointer (buffer, 0) || 
      !valid_user_pointer (buffer, size))
  {
    sysexit (-1);
  }
  if (fd == 1) sysexit (-1);
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
}

mapid_t 
sysmmap (int fd, void *addr)
{
  if (fd > (thread_current()->file_handlers_number - 1)) return -1;
  if (fd == 0) return -1;   // stdin not mappable
  if (fd == 1) return -1;   // stdout not mappable
  if (sysfilesize(fd) == 0) sysexit(-1);  // file size 0 bytes
  if ((int) addr % PGSIZE != 0) return -1;    // addr not page aligned
  if (addr == NULL) return -1;

  struct file *f = thread_current()->file_handlers[fd];
  struct file *f_reopened = file_reopen(f);
  //void* spid = pg_round_down (addr);
  void* spid = addr;
  bool writable = true; 
  page_location location = MAP_MEMORY;
  int file_size = sysfilesize(fd);
  size_t page_read_bytes = sysfilesize(fd);
  size_t page_zero_bytes = ((size_t) ROUND_UP(file_size, PGSIZE)) - ((size_t) file_size);
  struct thread *thread = thread_current(); // for inherit
  struct supplementary_page_table_entry *spte = supplementary_page_table_entry_create
    (spid, writable, location, page_read_bytes,
    page_zero_bytes, f_reopened, 0, thread);
  struct supplementary_page_table_entry* spte_temp = supplementary_page_table_entry_find
    (addr);

  if (spte_temp != NULL) return -1; // address mapped already (stack/code/data)
  if (supplementary_page_table_entry_find_between (spid, (void *)((int)spid + file_size))) return -1;
  supplementary_page_table_entry_insert (spte);
  mapid_t mid = (int)spid >> 12;
  return mid;
}

void 
sysmunmap (mapid_t mapping)
{
  //void *vaddr = (void *)mapping << 12;
  //struct supplementary_page_table_entry *spte = supplementary_page_table_entry_find
    //(vaddr);
  void *uaddr;
  //if (spte == NULL) sysexit(-1); // unmapping an unmapped file
  struct list_elem *e;
  struct thread *cur = thread_current();
  lock_acquire (&cur->supplementary_page_table_lock);
  for (e = list_begin (&cur->supplementary_page_table); 
       e != list_end (&cur->supplementary_page_table);
       e = list_next (e))
    {
      struct supplementary_page_table_entry* spte = 
        list_entry (e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
      if ((((int)spte->pid) >> 12) == mapping) {
        uaddr = spte->pid;
        if (pagedir_is_dirty(cur->pagedir, uaddr)) {
          file_write_at(spte->file, uaddr, PGSIZE, spte->file_ofs);
        }
        pagedir_clear_page(cur->pagedir, uaddr);
        list_remove(&spte->supplementary_page_table_entry_elem);
        //free(spte); 
        break;
      }
    }
  lock_release (&cur->supplementary_page_table_lock);
}
