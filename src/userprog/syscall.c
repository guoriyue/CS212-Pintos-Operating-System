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
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "vm/page.h"
static void check_usr_ptr(const void *u_ptr, void *esp);
static void check_usr_string(const char *str, void *esp);
static void check_usr_buffer(const void *buffer, unsigned length, void *esp, bool check_writable);
static bool pinning_file(const char *filename, bool get_length);
static void select_pinning_page(const void *begin, unsigned length, bool pinning);
static unsigned pinning_len(const char *string);

static unsigned pinning_len(const char *string)
{
  void *last_page = NULL;
  void *curr_page = NULL;
  unsigned str_len = 0;
  char *str = (char *)string;
  while (true)
  {
    const void *ptr = (const void *)str;
    curr_page = pg_round_down(ptr);
    if (curr_page != last_page)
    {
      select_pinning_page(curr_page, 0, true);
      last_page = curr_page;
    }
    if (*str == '\0')
      break;
    str_len++;
    str = (char *)str + 1;
  }
  return str_len;
}

static bool pinning_file(const char *filename, bool get_length)
{

  void *last_page = NULL;
  void *curr_page = NULL;
  unsigned str_len = 0;
  char *str = (char *)filename;
  while (true)
  {
    const void *ptr = (const void *)str;
    curr_page = pg_round_down(ptr);
    if (curr_page != last_page)
    {
      select_pinning_page(curr_page, 0, true);
      last_page = curr_page;
    }
    if (*str == '\0')
      break;
    str_len++;
    str = (char *)str + 1;
  }
  if (get_length)
  {
    return str_len;
  }
  return true;
}

static void check_usr_buffer(const void *buffer, unsigned length, void *esp, bool check_writable)
{
  check_usr_ptr(buffer, esp);
  struct supplementary_page_table_entry *spte = supplementary_page_table_entry_find((void *)buffer);
  if (check_writable && spte->writeable == false)
  {
    sysexit(-1);
  }
  unsigned curr_offset = PGSIZE;
  while (true)
  {
    if (curr_offset >= length)
      break;
    check_usr_ptr((const void *)((char *)buffer + curr_offset), esp);
    struct supplementary_page_table_entry *spte = supplementary_page_table_entry_find((void *)((char *)buffer + curr_offset));
    if (spte->writeable == false)
    {
      sysexit(-1);
    }
    curr_offset += PGSIZE;
  }
  check_usr_ptr((const void *)((char *)buffer + length), esp);
}

static void check_usr_ptr(const void *ptr, void *esp)
{
  if (ptr == NULL)
  {
    sysexit(-1);
  }
  if (!is_user_vaddr(ptr))
  {
    sysexit(-1);
  }
  if (supplementary_page_table_entry_find((void *)ptr) == NULL)
  {
    if ((ptr == esp - 4 || ptr == esp - 32 || ptr >= esp)           // SUB $n, %esp instruction, and then use a MOV ..., m(%esp) instruction to write to a stack location within the allocated space that is m bytes above the current stack pointer.
        && (uint32_t)ptr >= (uint32_t)(PHYS_BASE - 8 * 1024 * 1024) // 8 MB stack size
        && (uint32_t)ptr <= (uint32_t)PHYS_BASE)
    {
      void *new_stack_page = pg_round_down(ptr);
      stack_growth(new_stack_page);
    }
    else
    {
      sysexit(-1);
    }
  }
}

static void check_usr_string(const char *str, void *esp)
{
  while (true)
  {
    const void *ptr = (const void *)str;
    check_usr_ptr(ptr, esp);
    if (*str == '\0')
    {
      break;
    }
    str = (char *)str + 1;
  }
}

static void select_pinning_page(const void *begin, unsigned length, bool pinning)
{
  void *curr_addr = (void *)begin;
  void *curr_page = pg_round_down(curr_addr);
  if (pinning)
  {
    pin_page(curr_page);
  }
  else
  {
    unpin_page(curr_page);
  }
  while (length > PGSIZE)
  {
    curr_addr = (void *)((char *)curr_addr + PGSIZE);
    curr_page = pg_round_down(curr_addr);
    if (pinning)
    {
      pin_page(curr_page);
    }
    else
    {
      unpin_page(curr_page);
    }
    length -= PGSIZE;
  }
  curr_addr = (void *)((char *)curr_addr + length);
  void *last_page = pg_round_down(curr_addr);
  if ((uint32_t)last_page != (uint32_t)curr_page)
  {
    if (pinning)
    {
      pin_page(last_page);
    }
    else
    {
      unpin_page(last_page);
    }
  }
}

static void syscall_handler(struct intr_frame *);

/* This function checks whether a user pointer passed into a function
  as a parameter (for a syscall) is actually valid (i.e. not null,
  not in kernel memory, and mapped). */
bool valid_user_pointer(void *user_pointer, unsigned size)
{
  struct thread *cur = thread_current();
  /* Null pointer. */
  if (user_pointer == NULL)
  {
    return false;
  }
  /* A pointer to kernel virtual address space (above PHYS_BASE). */
  if (!is_user_vaddr((void *)(user_pointer)))
  {
    return false;
  }
  /* Pointer of end of buffer to kernel virtual
    address space (above PHYS_BASE). */
  if (!is_user_vaddr((void *)(user_pointer + size)))
  {
    return false;
  }
  /* A pointer to unmapped virtual memory. */
  if (pagedir_get_page(cur->pagedir, user_pointer) == NULL)
  {
    return false;
  }
  return true;
}

/* This function makes sure that the command line arguments
  passed in are valid. */
int get_valid_argument(int *esp, int i)
{

  /* Validation. */
  if (!valid_user_pointer(esp, 0))
    sysexit(-1);
  if (!valid_user_pointer(esp + i, 0))
    sysexit(-1);

  /* For test case boundary 3. */
  if (!valid_user_pointer((int *)((char *)(esp + i) + 1), 0))
    sysexit(-1);
  if (!valid_user_pointer((int *)((char *)(esp + i) + 2), 0))
    sysexit(-1);
  if (!valid_user_pointer((int *)((char *)(esp + i) + 3), 0))
    sysexit(-1);

  return *(esp + i);
}

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Call the respective syscall based on the syscall number.
  Make sure it is called in a critical section to avoid
  race conditions. */
static void
syscall_handler(struct intr_frame *f UNUSED)
{
  /* System call number is pushed onto stack as uint32_t. */
  int *esp = f->esp;
  if (!get_valid_argument(esp, 0))
    sysexit(-1);
  uint32_t syscall_number = (uint32_t)*esp;
  /* Use char* to get arguments. */
  struct lock syscall_system_lock;
  lock_init(&syscall_system_lock);
  if (syscall_number == SYS_EXIT)
  {
    int status = (int)get_valid_argument(esp, 1);
    sysexit(status);
  }
  else if (syscall_number == SYS_EXEC)
  {
    lock_acquire(&syscall_system_lock);
    char *cmd_line = (char *)get_valid_argument(esp, 1);
    f->eax = sysexec(cmd_line, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_WAIT)
  {
    int pid = (int)get_valid_argument(esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t)syswait(pid);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_OPEN)
  {
    lock_acquire(&syscall_system_lock);
    char *file = (char *)get_valid_argument(esp, 1);

    f->eax = (uint32_t)sysopen(file, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_WRITE)
  {
    int fd = (int)get_valid_argument(esp, 1);
    void *buffer = (void *)get_valid_argument(esp, 2);
    unsigned size = (unsigned)get_valid_argument(esp, 3);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t)syswrite(fd, buffer, size, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_HALT)
  {
    lock_acquire(&syscall_system_lock);
    syshalt();
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_CREATE)
  {
    lock_acquire(&syscall_system_lock);
    char *file = (char *)get_valid_argument(esp, 1);
    unsigned initial_size = (unsigned)get_valid_argument(esp, 2);

    f->eax = (uint32_t)syscreate(file, initial_size, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_REMOVE)
  {
    char *file = (char *)get_valid_argument(esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t)sysremove(file, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_FILESIZE)
  {
    int fd = get_valid_argument(esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t)sysfilesize(fd);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_READ)
  {
    int fd = get_valid_argument(esp, 1);
    void *buffer = (void *)get_valid_argument(esp, 2);
    unsigned size = (unsigned)get_valid_argument(esp, 3);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t)sysread(fd, buffer, size, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_SEEK)
  {
    int fd = get_valid_argument(esp, 1);
    unsigned position = (unsigned)get_valid_argument(esp, 2);
    lock_acquire(&syscall_system_lock);
    sysseek(fd, position);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_TELL)
  {
    int fd = get_valid_argument(esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t)systell(fd);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_CLOSE)
  {
    int fd = get_valid_argument(esp, 1);
    lock_acquire(&syscall_system_lock);
    sysclose(fd);
    lock_release(&syscall_system_lock);
  }
  else
  {
    sysexit(-1);
  }
}

void sysexit(int status)
{
  struct thread *cur = thread_current();
  cur->exit_status->exit_status = status;

  if (!cur->kernel)
  {
    printf("%s: exit(%d)\n", thread_current()->name, status);
  }
  thread_exit();
}

int sysopen(const char *file, void *esp)
{
  check_usr_string(file, esp);
  pinning_file(file, false);
  unsigned length = strlen(file);
  lock_acquire(&file_system_lock);
  struct file *f = filesys_open(file);
  lock_release(&file_system_lock);
  select_pinning_page(file, length, false);
  if (f)
  {
    struct thread *cur = thread_current();
    struct file **tmp;
    if (cur->file_handlers_number == 2)
    {
      tmp = malloc((cur->file_handlers_number + 1) * sizeof(struct file *));
      memset(tmp, 0, 3 * sizeof(struct file *));
    }
    else
    {
      tmp = malloc((cur->file_handlers_number + 1) * sizeof(struct file *));
      memcpy(tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file *));
      free(cur->file_handlers);
    }
    cur->file_handlers = tmp;
    cur->file_handlers[cur->file_handlers_number] = f;
    int ret_file_handlers_number = cur->file_handlers_number;
    cur->file_handlers_number++;
    return ret_file_handlers_number;
  }
  return -1;
}

int syswait(int pid)
{
  return process_wait(pid);
}

int syswrite(int fd, const void *buffer, unsigned length, void *esp)
{
  check_usr_buffer(buffer, length, esp, false);
  select_pinning_page(buffer, length, true);

  int ans;
  if (fd == 1)
  {
    putbuf(buffer, length);
    ans = length;
  }
  else
  {
    struct thread *cur = thread_current();
    if (fd >= cur->file_handlers_number || fd <= 0)
    {
      select_pinning_page(buffer, length, false);
      return -1;
    }
    else
    {

      struct file *f = cur->file_handlers[fd];
      lock_acquire(&file_system_lock);
      ans = file_write(f, buffer, length);
      lock_release(&file_system_lock);
    }
  }
  select_pinning_page(buffer, length, false);
  return ans;
}

tid_t sysexec(const char *command_line, void *esp)
{
  check_usr_string(command_line, esp);
  struct thread *curr_thread = thread_current();

  unsigned length = pinning_len(command_line);

  int pid = process_execute(command_line);
  select_pinning_page(command_line, length, false);
  if (pid == TID_ERROR)
  {
    return -1;
  }
  return pid;
}

void syshalt(void)
{
  shutdown_power_off();
}

bool syscreate(const char *file, unsigned initial_size, void *esp)
{
  check_usr_string(file, esp);
  pinning_file(file, false);
  unsigned length = strlen(file);
  lock_acquire(&file_system_lock);
  bool outcome = filesys_create(file, initial_size);
  lock_release(&file_system_lock);
  select_pinning_page(file, length, false);

  return outcome;
}

bool sysremove(const char *file, void *esp)
{
  check_usr_string(file, esp);
  pinning_file(file, false);

  unsigned length = strlen(file);
  lock_acquire(&file_system_lock);
  bool outcome = filesys_remove(file);
  lock_release(&file_system_lock);
  select_pinning_page(file, length, false);

  return outcome;
}

int sysfilesize(int fd)
{
  struct thread *cur = thread_current();
  struct file *f = cur->file_handlers[fd];
  return (int)file_length(f);
}

int sysread(int fd, void *buffer, unsigned length, void *esp)
{
  if (fd > (thread_current()->file_handlers_number - 1))
  {
    sysexit(-1);
  }
  if (fd == 1)
  {
    sysexit(-1);
  }

  check_usr_buffer(buffer, length, esp, true);
  select_pinning_page(buffer, length, true);

  int ans = -1;

  struct thread *cur = thread_current();
  if (fd == 0)
  {
    char *input_buffer = (char *)buffer;
    for (unsigned i = 0; i < length; i++)
    {
      uint8_t input_char = input_getc();
      input_buffer[i] = input_char;
    }
    ans = length;
  }
  else if (fd == 1 || fd >= cur->file_handlers_number)
  {
    ans = -1;
  }
  else
  {
    struct file *f = cur->file_handlers[fd];
    if (f == NULL)
      ans = -1;
    else
    {
      lock_acquire(&file_system_lock);
      ans = file_read(f, buffer, length);
      lock_release(&file_system_lock);
    }
  }
  select_pinning_page(buffer, length, false);
  return ans;
}

void sysseek(int fd, unsigned position)
{
  if (fd > (thread_current()->file_handlers_number - 1))
  {
    sysexit(-1);
  }
  struct thread *cur = thread_current();
  struct file *f = cur->file_handlers[fd];
  file_seek(f, position);
}

unsigned
systell(int fd)
{
  if (fd > (thread_current()->file_handlers_number - 1))
  {
    sysexit(-1);
  }
  struct thread *cur = thread_current();
  struct file *f = cur->file_handlers[fd];
  return file_tell(f);
}

void sysclose(int fd)
{
  if (fd > (thread_current()->file_handlers_number - 1))
  {
    sysexit(-1);
  }
  if (fd == 1)
    sysexit(-1);
  if (fd == 0)
    sysexit(-1);
  struct thread *cur = thread_current();
  struct file *f = cur->file_handlers[fd];
  file_close(f);
  cur->file_handlers_number--;
}
