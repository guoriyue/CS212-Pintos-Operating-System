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
#include "vm/page.h"
// BEGIN LP DEFINED HELPER FUNCTIONS//
static void check_usr_ptr (const void* u_ptr, void* esp);
static void check_usr_string (const char* str, void* esp);
static void check_usr_buffer (const void* buffer, unsigned length, void* esp, bool check_writable);
static bool pinning_file (const char* filename, bool get_length);
// static uint32_t read_frame(struct intr_frame* f, int byteOffset);
// static int add_to_open_file_list(struct file* fp);
static void select_pinning_page (const void* begin, unsigned length, bool pinning);
static unsigned pinning_len(const char* string);
// static mapid_t mmap (int fd, void *addr);
// static void munmap (mapid_t mapping);
/*
 */
static unsigned pinning_len(const char* string) {
    // void* last_page = NULL;
    // void* curr_page = NULL;
    // unsigned str_len = 0;
    // char* str = (char*)string;
    // while (true) {
    //     const void* ptr = (const void*)str;
    //     curr_page = pg_round_down (ptr);
    //     if (curr_page != last_page) {
    //         select_pinning_page (curr_page, 0, true);
    //         last_page = curr_page;
    //     }
    //     if (*str == '\0') break;
    //     str_len++;
    //     str = (char*)str + 1;
    // }
      void* last_page = NULL;
  void* curr_page = NULL;
  unsigned str_len = 0;
  char* str = (char*)string;
  while (true) {
    const void* ptr = (const void*)str;
    curr_page = pg_round_down (ptr);
    if (curr_page != last_page) {
        select_pinning_page (curr_page, 0, true);
        last_page = curr_page;
    }
    if (*str == '\0') break;
    str_len++;
    str = (char*)str + 1;
  }
    return str_len;
}

/*
 --------------------------------------------------------------------
 Description: ensures that the length of the filename does not 
    exceed 14 characters. 
 --------------------------------------------------------------------
 */
// #define MAX_FILENAME_LENGTH 14
static bool pinning_file (const char* filename, bool get_length)
{
  // if (str_len > 14) {
  //   return false;
  // }
    
  void* last_page = NULL;
  void* curr_page = NULL;
  unsigned str_len = 0;
  char* str = (char*)filename;
  while (true) {
    const void* ptr = (const void*)str;
    curr_page = pg_round_down (ptr);
    if (curr_page != last_page) {
        select_pinning_page (curr_page, 0, true);
        last_page = curr_page;
    }
    if (*str == '\0') break;
    str_len++;
    str = (char*)str + 1;
  }
  if (get_length)
  {
    return str_len;
  }
  // if (str_len > 14) {
  //     last_page = NULL;
  //     curr_page = NULL;
  //     str = (char*)filename;
  //     while (true) {
  //         const void* ptr = (const void*)str;
  //         curr_page = pg_round_down (ptr);
  //         if (curr_page != last_page) {
  //             select_pinning_page (curr_page, 0, false);
  //             last_page = curr_page;
  //         }
  //         if (*str == '\0') break;
  //         str = (char*)str + 1;
  //     }
  //     return false;
  // }
  return true;
}

/*
 --------------------------------------------------------------------
 Description: Checks to ensure that all pointers within the usr's
    supplied buffer are proper for user space. 
 NOTE: If this function completes and returns, we know that the 
    buffer is solid. 
 ADDITOIN: CHeck every 4kb for every page until length is exceeded. 
 --------------------------------------------------------------------
 */
// #define BYTES_PER_PAGE PGSIZE
static void check_usr_buffer (const void* buffer, unsigned length, void* esp, bool check_writable)
{
  check_usr_ptr (buffer, esp);
  // struct supplementary_page_table_entry* spte = find_spte((void*)buffer, thread_current());
  struct supplementary_page_table_entry* spte = supplementary_page_table_entry_find ((void*)buffer);
  if (check_writable && spte->writeable == false) {
    sysexit (-1);
  }
  unsigned curr_offset = PGSIZE;
  while (true) {
    if (curr_offset >= length) break;
    check_usr_ptr ((const void*)((char*)buffer + curr_offset), esp);
    // struct supplementary_page_table_entry* spte = find_spte((void*)((char*)buffer + curr_offset), thread_current());
    struct supplementary_page_table_entry* spte = supplementary_page_table_entry_find ((void*)((char*)buffer + curr_offset));
    if (spte->writeable == false) {
      sysexit (-1);
    }
    curr_offset += PGSIZE;
  }
  check_usr_ptr ((const void*)((char*)buffer + length), esp);
}


/*
 --------------------------------------------------------------------
 Description: checks the pointer to make sure that it is valid.
    A pointer is valid only if it is within user virtual address 
    space, it is not null, and it is mapped. 
 NOTE: We use the is_usr_vaddr in thread/vaddr.h and pagedir_get_page 
    in userprg/pagedir.c
 NOTE: If the pointer is determined to be invalid, we call
    the exit system call which will terminate the current program.
    We pass the appropriate error as well. 
 NOTE: If this function completes and returns, than we know the pointer
    is valid, and we continue operation in the kernel processing
    the system call.
 NOTE: We need to replace the call to pagedir_get_page with a call
    to get spte, as the page we are accessing might not be mapped.
 --------------------------------------------------------------------
 */
static void check_usr_ptr (const void* ptr, void* esp)
{
  if (ptr == NULL) {
    sysexit (-1);
  }
  if (!is_user_vaddr(ptr)) {
    sysexit (-1);
  } 
  if (supplementary_page_table_entry_find ((void*)ptr) == NULL)
  {
    // if (is_valid_stack_access (esp, (void*)ptr))
    if ((ptr == esp - 4 || ptr == esp - 32
         || ptr >= esp) // SUB $n, %esp instruction, and then use a MOV ..., m(%esp) instruction to write to a stack location within the allocated space that is m bytes above the current stack pointer.
         && (uint32_t) ptr >= (uint32_t) (PHYS_BASE - 8 * 1024 * 1024) // 8 MB stack size
         && (uint32_t) ptr <= (uint32_t) PHYS_BASE)
    {
      void* new_stack_page = pg_round_down (ptr);
      stack_growth (new_stack_page);
    }
    else
    {
      sysexit (-1);
    }
  }
}

/*
 --------------------------------------------------------------------
 Description: checks each character in the string to make sure that
    the pointers are non null, are within user virtual address space,
    and are properly mapped. 
 --------------------------------------------------------------------
 */
static void check_usr_string (const char* str, void* esp)
{
  while (true)
  {
    const void* ptr = (const void*)str;
    check_usr_ptr (ptr, esp);
    if (*str == '\0')
    {
      break;
    }
    str = (char*)str + 1;
  }
}

// /*
//  --------------------------------------------------------------------
//  Description: This is a helper method for reading values from the 
//     frame. We increment f->esp by offset, check the pointer to make
//     sure it is valid, and then return the numerical value that resides
//     at the address of the pointer. 
//  NOTE: The return value of this function is the uinsigned int equivalent
//     of the bits at said address. It is the responsibility of the caller
//     to cast this return value to the appropriate type.
//  --------------------------------------------------------------------
//  */
// static uint32_t read_frame(struct intr_frame* f, int byteOffset) {
//     void* addr = f->esp + byteOffset;
//     check_usr_ptr (addr, f->esp);
//     return *(uint32_t*)addr;
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: This function pins all of the pages that make up the 
    space from begin to length.
 NOTE: length is given in bytes
 NOTE: We assume that the validation of pointers has allready been
    done, so that these addresses are all valid.
 NOTE: We define pages by rounding down to the page boundary. If we 
    call pg_dir_round_up + 1, we will be in the next page. The 
    differnce between thus rounded up value + 1 and the current
    address is the offset of the address in the page.
 NOTE: if we ever encounter length being less than distance to 
    next page, we have covered all of the necessary pages.
 NOTE: if pinning is true, we pin, otherwise, we unpin
 NOTE: must be called first with pin, then unpin!


  //add page size to curr_address to get next page
  //subtract page size from length
 --------------------------------------------------------------------
 */
static void select_pinning_page (const void* begin, unsigned length, bool pinning)
{
  void* curr_addr = (void*)begin;
  void* curr_page = pg_round_down (curr_addr);
  if (pinning)
  {
    pin_page (curr_page);
  }
  else
  {
    unpin_page (curr_page);
  }
  while (length > PGSIZE)
  {
    curr_addr = (void*)((char*)curr_addr + PGSIZE);
    curr_page = pg_round_down (curr_addr);
    if (pinning)
    {
      pin_page (curr_page);
    }
    else
    {
      unpin_page (curr_page);
    }
    length -= PGSIZE;
  }
  curr_addr = (void*)((char*)curr_addr + length);
  void* last_page = pg_round_down (curr_addr);
  if ((uint32_t)last_page != (uint32_t)curr_page) {
    if (pinning)
    {
      pin_page (last_page);
    }
    else
    {
      unpin_page (last_page);
    }
  }
}



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
  struct lock syscall_system_lock;
  lock_init(&syscall_system_lock);
  if (syscall_number == SYS_EXIT)
  {
    int status = (int) get_valid_argument (esp, 1);
    sysexit (status);
  }
  else if (syscall_number == SYS_EXEC)
  {
    lock_acquire(&syscall_system_lock);
    char* cmd_line = (char*) get_valid_argument (esp, 1);
    f->eax = sysexec (cmd_line, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_WAIT)
  {
    int pid = (int)get_valid_argument (esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t) syswait (pid);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_OPEN)
  {
    lock_acquire(&syscall_system_lock);
    char* file = (char*)get_valid_argument (esp, 1);
    
    f->eax = (uint32_t) sysopen (file, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_WRITE)
  {
    int fd = (int)get_valid_argument (esp, 1);
    void * buffer = (void *)get_valid_argument (esp, 2);
    unsigned size = (unsigned)get_valid_argument (esp, 3);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t) syswrite (fd, buffer, size, esp);
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
    char* file = (char*)get_valid_argument (esp, 1);
    unsigned initial_size = (unsigned)get_valid_argument (esp, 2);
    
    f->eax = (uint32_t) syscreate(file, initial_size, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_REMOVE)
  {
    char* file = (char*)get_valid_argument (esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t) sysremove (file, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_FILESIZE)
  {
    int fd = get_valid_argument (esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t) sysfilesize (fd);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_READ)
  {
    int fd = get_valid_argument (esp, 1);
    void * buffer = (void *)get_valid_argument (esp, 2);
    unsigned size = (unsigned)get_valid_argument (esp, 3);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t) sysread (fd, buffer, size, esp);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_SEEK)
  {
    int fd = get_valid_argument (esp, 1);
    unsigned position = (unsigned) get_valid_argument (esp, 2);
    lock_acquire(&syscall_system_lock);
    sysseek (fd, position);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_TELL)
  {
    int fd = get_valid_argument (esp, 1);
    lock_acquire(&syscall_system_lock);
    f->eax = (uint32_t) systell (fd);
    lock_release(&syscall_system_lock);
  }
  else if (syscall_number == SYS_CLOSE)
  {
    int fd = get_valid_argument (esp, 1);
    lock_acquire(&syscall_system_lock);
    sysclose (fd);
    lock_release(&syscall_system_lock);
  }
  // else if (syscall_number == SYS_MMAP)
  // {
  //   uint32_t arg1 = read_frame(f, 4);
  //   uint32_t arg2 = read_frame(f, 8);
  //   lock_acquire(&file_system_lock);
  //   f->eax = mmap((int)arg1, (void*)arg2);
  //   lock_release(&file_system_lock);
  // }
  // else if (syscall_number == SYS_MUNMAP)
  // {
  //   uint32_t arg1 = read_frame(f, 4);
  //   lock_acquire(&file_system_lock);
  //   munmap((mapid_t) arg1);
  //   lock_release(&file_system_lock);
  // }
  else
  {
    sysexit (-1);
  }
}



// static void
// syscall_handler (struct intr_frame *f ) 
// {
//     unsigned arg1, arg2, arg3;
//     int systemCall_num = (int)read_frame(f, 0);
//     struct lock system_lock;
//     lock_init(&system_lock);
//     switch (systemCall_num) {
//         case SYS_HALT:
//             lock_acquire(&system_lock);
//             syshalt();
//             lock_release(&system_lock);
//             break;
//         case SYS_EXIT:
//             arg1 = read_frame(f, 4);
//             sysexit ((int)arg1);
//             break;
//         case SYS_EXEC:
//             lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             f->eax = (uint32_t)sysexec((char*)arg1, f->esp);
//             lock_release(&system_lock);
//             break;
//         case SYS_WAIT:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             f->eax = (uint32_t)syswait((int)arg1);
//             lock_release(&system_lock);
//             break;
//         case SYS_CREATE:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             arg2 = read_frame(f, 8);
//             f->eax = (uint32_t)syscreate((const char*)arg1, (unsigned)arg2, f->esp);
//             lock_release(&system_lock);
//             break;
//         case SYS_REMOVE:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             f->eax = (uint32_t)sysremove((const char*)arg1, f->esp);
//             lock_release(&system_lock);
//             break;
//         case SYS_OPEN:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             f->eax = (uint32_t)sysopen((const char*)arg1, f->esp);
//             lock_release(&system_lock);
//             break;
//         case SYS_FILESIZE:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             f->eax = (uint32_t)sysfilesize((int)arg1);
//             lock_release(&system_lock);
//             break;
//         case SYS_READ:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             arg2 = read_frame(f, 8);
//             arg3 = read_frame(f, 12);
//             f->eax = (int)sysread((int)arg1, (void*)arg2, (unsigned)arg3, f->esp);
//             lock_release(&system_lock);
//             break;
//         case SYS_WRITE:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             arg2 = read_frame(f, 8);
//             arg3 = read_frame(f, 12);
//             f->eax = (uint32_t)syswrite((int)arg1, (const void*)arg2, (unsigned)arg3, f->esp);
//             lock_release(&system_lock);
//             break;
//         case SYS_SEEK:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             arg2 = read_frame(f, 8);
//             sysseek((int)arg1, (unsigned)arg2);
//             lock_release(&system_lock);
//             break;
//         case SYS_TELL:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             f->eax = (uint32_t)systell((int)arg1);
//             lock_release(&system_lock);
//             break;
//         case SYS_CLOSE:
//         lock_acquire(&system_lock);
//             arg1 = read_frame(f, 4);
//             sysclose((int)arg1);
//             lock_release(&system_lock);
//             break;
//         // case SYS_MMAP:
//         //     arg1 = read_frame(f, 4);
//         //     arg2 = read_frame(f, 8);
//         //     f->eax = mmap((int)arg1, (void*)arg2);
//         //     break;
//         // case SYS_MUNMAP:
//         //     arg1 = read_frame(f, 4);
//         //     munmap((mapid_t) arg1);
//         //     break;
//         // case SYS_CHDIR:
//         //     arg1 = read_frame(f, 4);
//         //     f->eax = (uint32_t)chdir((const char*)arg1);
//         //     break;
//         // case SYS_MKDIR:
//         //     arg1 = read_frame(f, 4);
//         //     f->eax = (uint32_t)mkdir((const char*)arg1);
//         //     break;
//         // case SYS_READDIR:
//         //     arg1 = read_frame(f, 4);
//         //     arg2 = read_frame(f, 8);
//         //     f->eax = (uint32_t)readdir((int)arg1, (char *)arg2);
//         //     break;
//         // case SYS_ISDIR:
//         //     arg1 = read_frame(f, 4);
//         //     f->eax = (uint32_t)isdir((int)arg1);
//         //     break;
//         // case SYS_INUMBER:
//         //     arg1 = read_frame(f, 4);
//         //     f->eax = (uint32_t)inumber((int)arg1);
//         //     break;
//         default:
//             sysexit (-1); //should never get here. If we do, exit with -1.
//             break;
//     }
// }



// /* The mmap system call. fd is the user process's file descriptor to be mapped,
//  * and addr is the virtual address to map it to. */
// static mapid_t
// mmap(int fd, void *addr)
// {
//     /* Validate the parameters */
//     if (((uint32_t)addr) % PGSIZE != 0) {
//         return -1;
//     }
//     if (fd == 0 || fd == 1) {
//         return -1;
//     }

//     /* Ensure the fd has been assigned to the user */
//     // lock_acquire(&file_system_lock);
//     // struct file* fp = get_file_from_open_list(fd);
//     struct file* fp = filesys_open(fd);
//     if (fp == NULL) {
//         // lock_release(&file_system_lock);
//         return -1;
//     }
//     off_t size = file_length(fp);
//     // lock_release(&file_system_lock);
    
//     /* Ensure that the requested VM region wouldn't contain invalid addresses
//      * or overlap other user memory */
//     struct thread *t = thread_current();
//     void *page;
//     for (page = addr; page < addr + size; page += PGSIZE)
//     {
//         if (page == NULL) {
//             return -1;
//         }
//         if (!is_user_vaddr(page)) {
//             return -1;
//         }
//         if (find_spte(page, t) != NULL) {
//             return -1;
//         }
//     }

//     /* Fill in the mmap state data */
//     struct mmap_state *mmap_s = malloc(sizeof(struct mmap_state));
//     if (mmap_s == NULL) {
//       sysexit (-1);
//         // thread_current()->vital_info->exit_status = -1;
//         // if (thread_current()->is_running_user_program) {
//         //     printf("%s: exit(%d)\n", thread_name(), -1);
//         // }
//         // thread_exit();
//     }

//     // lock_acquire(&file_system_lock);
//     mmap_s->fp = file_reopen(fp);
//     if (mmap_s->fp == NULL)
//     {
//         // lock_release(&file_system_lock);
//         return -1;
//     }
//     // lock_release(&file_system_lock);

//     mmap_s->vaddr = addr;
//     mmap_s->mapping = t->mapid_counter;
//     list_push_back(&t->mmapped_files, &mmap_s->elem);

//     /* Finally, create the necessary SPTEs */
//     for (page = addr; page < addr + size; page += PGSIZE)
//     {
//         uint32_t read_bytes = page + PGSIZE < addr + size ? PGSIZE
//                                                           : addr + size - page;
//         uint32_t zero_bytes = PGSIZE - read_bytes;
//         struct supplementary_page_table_entry* spte = create_spte_and_add_to_table(MMAPED_PAGE,             /* Location */
//                                      page,                    /* Address */
//                                      true,                    /* Writeable */
//                                      false,                   /* Loaded */
//                                      mmap_s->fp,              /* File */
//                                      page - addr,             /* File offset */
//                                      read_bytes,
//                                      zero_bytes);
//         if (spte == NULL) {
//             PANIC("null spte");
//         }
//         lock_release(&spte->page_lock);
//     }
//     return t->mapid_counter++;
// }

// /* munmap system call. mapping is the identifier of the mapping to unmap */
// static void
// munmap(mapid_t mapping)
// {
//     /* Find the mmap_state */
//     struct thread *t = thread_current();
//     struct list_elem *e = list_head(&t->mmapped_files);
//     while ((e = list_next (e)) != list_end (&t->mmapped_files)) 
//     {
//         struct mmap_state *mmap_s = list_entry(e, struct mmap_state, elem);
//         if (mmap_s->mapping == mapping) {
//             // lock_acquire(&file_system_lock);
//             unsigned size = file_length(mmap_s->fp);
//             // lock_release(&file_system_lock);
//             select_pinning_page (mmap_s->vaddr, size, true);
//             munmap_state(mmap_s, t);
//             return;
//         }
//     }
// }

// /* If a child process is in its parent's child_exit_status list, the parent is 
//   still waiting for it, which means we need to call sema_up and resume the parent 
//   process. */
// bool
// child_is_waiting (struct exit_status_struct* child_es)
// {
//   struct thread *cur = thread_current ();
//   struct list_elem *e;
//   for (e = list_begin (&cur->parent->children_exit_status_list); 
//       e != list_end (&cur->parent->children_exit_status_list);
//        e = list_next (e))
//     {
//       struct exit_status_struct* es = list_entry (e, struct exit_status_struct, exit_status_elem);
//       if (es->process_id == child_es->process_id) {
//         return true;
//       }
//     }
//   return false;
// }

// void
// sysexit (int status)
// {
//   struct thread *cur = thread_current ();
//   cur->exit_status->exit_status = status;
//   // lock_acquire (&cur->list_lock);
  
//   // if (child_is_waiting (cur->exit_status))
//   // {
//   //   cur->exit_status->exit_status = status;
//   //   sema_up (&cur->exit_status->sema_wait_for_child);
//   // }
//   // else
//   // {
//   //   cur->exit_status->terminated = 1;
//   // }
//   // lock_release (&cur->list_lock);
//   if (!cur->kernel)
//   {
//     printf ("%s: exit(%d)\n", thread_current ()->name, status);
//   }
//   thread_exit ();
// }

// // int
// // sysopen (const char *file_name)
// // {
// //   if (!valid_user_pointer ((void *) file_name, 0))
// //     sysexit (-1);
// //   if (!valid_user_pointer ((void *) file_name, strlen (file_name)))
// //     sysexit (-1);
// //   if (!file_name)
// //     sysexit (-1);

// //   char* ret = (char *) file_name;
// //   while (*ret) {
// //     if (ret + 1)
// //     {
// //       if (!valid_user_pointer (++ret, 0))
// //         sysexit (-1);
// //     }
// //     else
// //     {
// //       sysexit (-1);
// //     }
// //   }
  
// //   struct file *f = filesys_open (file_name);
// //   if (f)
// //   {
// //     struct thread* cur = thread_current ();
// //     struct file** tmp;
// //     if (cur->file_handlers_number == 2)
// //     {
// //       tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
// //       memset (tmp, 0, 3 * sizeof(struct file*));
// //     }
// //     else
// //     {
// //       tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
// //       memcpy (tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file*));
// //       free (cur->file_handlers);
// //     }
// //     cur->file_handlers = tmp;
// //     cur->file_handlers[cur->file_handlers_number] = f;
// //     int ret_file_handlers_number = cur->file_handlers_number;
// //     cur->file_handlers_number++;
// //     return ret_file_handlers_number;
// //   }
// //   return -1;
// // }
// int
// sysopen (const char *file_name, void* esp) {
//     // printf ("sysopen begin\n");
//   //     if (!valid_user_pointer ((void *) file_name, 0))
//   //   sysexit (-1);
//   // if (!valid_user_pointer ((void *) file_name, strlen (file_name)))
//   //   sysexit (-1);
//   // if (!file_name)
//   //   sysexit (-1);

//   // char* ret = (char *) file_name;
//   // while (*ret) {
//   //   if (ret + 1)
//   //   {
//   //     if (!valid_user_pointer (++ret, 0))
//   //       sysexit (-1);
//   //   }
//   //   else
//   //   {
//   //     sysexit (-1);
//   //   }
//   // }

//     check_usr_string(file_name, esp);
//     if (!pinning_file(file_name)) {
//       // printf ("sysopen return 111\n");
//         return -1;
        
//     }
//     // lock_acquire(&file_system_lock);
//     unsigned length = strlen(file_name);
//     struct file* f = filesys_open(file_name);
//     select_pinning_page (file_name, length, false);

//     // lock_release(&file_system_lock);
//     // if (fp == NULL) {
//     //     select_pinning_page (file, length, false);
//     //     // lock_release(&file_system_lock);
//     //     return -1;
//     // }
//     // select_pinning_page (file, length, false);
//     // int f = add_to_open_file_list(fp);
//     // lock_release(&file_system_lock);
//     // return fd;
//     if (f)
//     {
//       struct thread* cur = thread_current ();
//       struct file** tmp;
//       if (cur->file_handlers_number == 2)
//       {
//         tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
//         memset (tmp, 0, 3 * sizeof(struct file*));
//       }
//       else
//       {
//         tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
//         memcpy (tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file*));
//         free (cur->file_handlers);
//       }
//       cur->file_handlers = tmp;
//       cur->file_handlers[cur->file_handlers_number] = f;
//       int ret_file_handlers_number = cur->file_handlers_number;
//       cur->file_handlers_number++;
//       // printf ("sysopen return 222, with file number %d\n", ret_file_handlers_number);
//       return ret_file_handlers_number;
//     }
//     // printf ("sysopen return 333\n");
//     return -1;
// }

// int
// syswait (int pid)
// {
//    return process_wait (pid);
// }

// // int
// // syswrite (int fd, const void * buffer, unsigned size)
// // {
// //   if (!valid_user_pointer ((void *) buffer, 0))
// //     sysexit (-1);
// //   if (!valid_user_pointer ((void *) buffer, size))
// //     sysexit (-1);
// //   if (fd == 0) sysexit (-1);
// //   if (!valid_user_pointer ((void *) buffer, size))
// //   {
// //     sysexit (-1);
// //   }
// //   if (fd > (thread_current()->file_handlers_number - 1)) 
// //   {
// //     sysexit (-1);
// //   }
// //   int ans;
// //   if (fd == 1)
// //   {
// //     putbuf (buffer, size);
// //     ans = size;
// //   }
// //   else
// //   {
// //     struct thread *cur = thread_current ();
// //     struct file *f = cur->file_handlers[fd];
// //     if (fd >= cur->file_handlers_number)
// //     {
// //       sysexit (-1);
// //     }
// //     else 
// //     {
// //       ans = file_write (f, buffer, size);
// //     }
// //   }
// //   return ans;
// // }

// int
// syswrite (int fd, const void *buffer, unsigned length, void* esp) {
//     if (fd == 0) 
//     {
//       return -1;
//     }
//     // sysexit (-1);
//     check_usr_buffer(buffer, length, esp, false);
//     select_pinning_page (buffer, length, true);
    
//     // if (fd == STDOUT_FILENO) {
//     //     putbuf(buffer, length);
//     //     select_pinning_page (buffer, length, false);
//     //     return length;
//     // }
    
//     // // lock_acquire(&file_system_lock);
//     // struct file_package* package = get_file_package_from_open_list(fd);
//     // if (package == NULL) {
//     //     // lock_release(&file_system_lock);
//     //     select_pinning_page (buffer, length, false);
//     //     return -1;
//     // }
//     // int num_bytes_written = file_write_at(package->fp, buffer, length, package->position);
//     // select_pinning_page (buffer, length, false);
//     // package->position += num_bytes_written;
//     // // lock_release(&file_system_lock);
//     // return num_bytes_written;

    
  
//   int ans;
//   if (fd == 1)
//   {
//     putbuf (buffer, length);
//     ans = length;
//     // printf ("end call write 111\n");
//   }
//   else
//   {
//     struct thread *cur = thread_current ();
//     // printf ("fd %d\n", fd);
//     if (fd >= cur->file_handlers_number || fd <= 0)
//     {
//       select_pinning_page (buffer, length, false);
//       // sysexit (-1);
//       // printf ("end call write 222\n");
//       // return ans;
//       return -1;
//     }
//     else 
//     {

//       struct file *f = cur->file_handlers[fd];
//       ans = file_write (f, buffer, length);
//     }
//   }
//   select_pinning_page (buffer, length, false);
//   // printf ("end call write\n");
//   return ans;
// }


// // tid_t 
// // sysexec (const char * cmd_line)
// // {
// //   if (!valid_user_pointer ((void *) cmd_line, 0))
// //     sysexit (-1);

// //   char* ret = (char *) cmd_line;
// //   while (*ret) {
// //     if (!valid_user_pointer (++ret, 0))
// //       sysexit (-1);
// //   }

// //   tid_t pid = process_execute (cmd_line);
// //   return pid;
// // }

// tid_t
// sysexec (const char* command_line, void* esp) {
//   // if (!valid_user_pointer ((void *) command_line, 0))
//   //   sysexit (-1);

//   // char* ret = (char *) command_line;
//   // while (*ret) {
//   //   if (!valid_user_pointer (++ret, 0))
//   //     sysexit (-1);
//   // }

//     check_usr_string(command_line, esp);
//     struct thread* curr_thread = thread_current();
    
//     unsigned length = pinning_len(command_line);
    
//     int pid = process_execute(command_line);
//     select_pinning_page (command_line, length, false);
//     if (pid == TID_ERROR) {
//         return -1;
//     }
//     return pid;
//     // if (pid == TID_ERROR) {
//     //     return -1;
//     // }
//     // sema_down (&curr_thread->sema_child_load);
//     // if (curr_thread->child_did_load_successfully) {
//     //     curr_thread->child_did_load_successfully = false;
//     //     return pid;
//     // }
//     // return -1;
// }

// void 
// syshalt (void) 
// {
//   shutdown_power_off();
// }

// // bool 
// // syscreate (const char *file, unsigned initial_size) 
// // {

// //   bool ans = false;
// //   if (!valid_user_pointer ((void *) file, 0) || 
// //       !valid_user_pointer ((void *) file, initial_size))
// //   {
// //     sysexit (-1);
// //   }


// //   char* ret = (char *) file;
// //   while (*ret) {
// //     if (!valid_user_pointer (++ret, 0))
// //       sysexit (-1);
// //   }

// //   if (file == NULL) {
// //     sysexit (-1);
// //   } else {
// //     ans = filesys_create (file, initial_size);
// //   }
// //   return ans;
// // }


// bool syscreate (const char *file, unsigned initial_size, void* esp) {
//   // if (!valid_user_pointer ((void *) file, 0) || 
//   //     !valid_user_pointer ((void *) file, initial_size))
//   // {
//   //   sysexit (-1);
//   // }


//   // char* ret = (char *) file;
//   // while (*ret) {
//   //   if (!valid_user_pointer (++ret, 0))
//   //     sysexit (-1);
//   // }

//   // if (file == NULL) {
//   //   sysexit (-1);
//   // }

//     check_usr_string(file, esp);
//     if (!pinning_file(file)) {
//         return false;
//     }
//     // lock_acquire(&file_system_lock);
//     unsigned length = strlen(file);
//     bool outcome = filesys_create(file, initial_size);
//     select_pinning_page (file, length, false);
//     // lock_release(&file_system_lock);
    
//     return outcome;
// }

// // bool 
// // sysremove (const char *file) 
// // {
// //   char* ret = (char *) file;
// //   while (*ret) {
// //     if (!valid_user_pointer (++ret, 0))
// //       sysexit (-1);
// //   }
// //   return filesys_remove(file);
// // }
// bool 
// sysremove (const char *file, void* esp) {
//     check_usr_string(file, esp);
//   //   char* ret = (char *) file;
//   // while (*ret) {
//   //   if (!valid_user_pointer (++ret, 0))
//   //     sysexit (-1);
//   // }

//     if (!pinning_file(file)) {
//         // return -1;
//         return false;
//     }
    
//     // lock_acquire(&file_system_lock);
//     unsigned length = strlen(file);
//     bool outcome = filesys_remove(file);
//     select_pinning_page (file, length, false);
//     // lock_release(&file_system_lock);
    
//     return outcome;
// }

// int 
// sysfilesize (int fd) 
// {
//   if (fd > (thread_current()->file_handlers_number - 1) || fd <= 0) 
//   {
//     // sysexit (-1);
//     return -1;
//   }
//   struct thread *cur = thread_current ();
//   struct file *f = cur->file_handlers[fd];
//   return (int) file_length (f); 
// }

// // int 
// // sysread (int fd, void *buffer, unsigned size) 
// // {
// //   if (fd > (thread_current()->file_handlers_number - 1)) 
// //   {
// //     sysexit (-1);
// //   }
  
// //   if (!valid_user_pointer (buffer, 0) || 
// //       !valid_user_pointer (buffer, size))
// //   {
// //     sysexit (-1);
// //   }
// //   if (fd == 1) sysexit (-1);
// //   int ans = -1;
// //   if (fd == 0) {
// //     char *input_buffer = (char *) buffer;
// //     for (unsigned i = 0; i < size; i++) {
// //       uint8_t input_char = input_getc();
// //       input_buffer[i] = input_char;
// //     }
// //     ans = size; 
// //   } else {
// //     struct thread *cur = thread_current ();
// //     struct file *f = cur->file_handlers[fd];
// //     if (f == NULL) ans = -1;
// //     else ans = file_read (f, buffer, size);
// //   }
// //   return ans;
// // }

// int 
// sysread (int fd, void *buffer, unsigned length, void* esp) {
//   if (fd > (thread_current()->file_handlers_number - 1)) 
//   {
//     sysexit (-1);
//     // return -1;
//   }
//   if (fd == 1)
//   {
//     sysexit (-1);
//   } 
//   // return -1;
  
//   // if (!valid_user_pointer (buffer, 0) || 
//   //     !valid_user_pointer (buffer, length))
//   // {
//   //   sysexit (-1);
//   // }
//   // if (fd == 1) sysexit (-1);

//     check_usr_buffer(buffer, length, esp, true);
//     select_pinning_page (buffer, length, true);

//   // pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/read-stdout -a read-stdout --swap-size=4 -- -q  -f run read-stdout
//   int ans = -1;

//   struct thread *cur = thread_current ();
//   if (fd == 0) 
//   {
//     char *input_buffer = (char *) buffer;
//     for (unsigned i = 0; i < length; i++) 
//     {
//       uint8_t input_char = input_getc();
//       input_buffer[i] = input_char;
//     }
//     ans = length;
//   }
//   else if (fd == 1 || fd >= cur->file_handlers_number)
//   {
//     ans = -1;
//   }
//   else 
//   {
//     struct file *f = cur->file_handlers[fd];
//     if (f == NULL) ans = -1;
//     else ans = file_read (f, buffer, length);
//   }
//   select_pinning_page (buffer, length, false);
//   return ans;
    
//     // if (fd == STDIN_FILENO) {
//     //     char* char_buff = (char*)buffer;
//     //     unsigned i;
//     //     for (i = 0; i < length; i++) {
//     //         char_buff[i] = input_getc();
//     //     }
//     //     select_pinning_page (buffer, length, false);
//     //     return length;
//     // }
    
//     // lock_acquire(&file_system_lock);
//     // struct file_package* package = get_file_package_from_open_list(fd);
//     // if (package == NULL) {
//     //     // lock_release(&file_system_lock);
//     //     select_pinning_page (buffer, length, false);
//     //     return -1;
//     // }
//     // int num_bytes_read = file_read_at(package->fp, buffer, length, package->position);
//     // select_pinning_page (buffer, length, false);
//     // // package->position += num_bytes_read;
//     // // lock_release(&file_system_lock);
//     // return num_bytes_read;
// }


// void 
// sysseek (int fd, unsigned position) 
// {
//   if (fd > (thread_current()->file_handlers_number - 1) || fd <= 0) 
//   {
//     sysexit (-1);
// // return -1;
//   }
//   struct thread *cur = thread_current ();
//   struct file *f = cur->file_handlers[fd];
//   file_seek(f, position);
// }

// unsigned 
// systell (int fd) 
// {
//   if (fd > (thread_current()->file_handlers_number - 1) || fd <= 0) 
//   {
//     sysexit (-1);
//     // return -1;
//   }
//   struct thread *cur = thread_current ();
//   struct file *f = cur->file_handlers[fd];
//   return file_tell(f);
// }

// void
// sysclose (int fd) 
// {
//   if (fd > (thread_current()->file_handlers_number - 1) || fd <= 1) 
//   {
//     sysexit (-1);
//     // return -1;
//   }
//   // if (fd == 1) sysexit (-1);
//   // if (fd == 0) sysexit (-1);
//   struct thread *cur = thread_current ();
//   struct file *f = cur->file_handlers[fd];
//   file_close (f);
//   cur->file_handlers_number--;
// }



void
sysexit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status->exit_status = status;
  // lock_acquire (&cur->list_lock);
  
  // if (child_is_waiting (cur->exit_status))
  // {
  //   cur->exit_status->exit_status = status;
  //   sema_up (&cur->exit_status->sema_wait_for_child);
  // }
  // else
  // {
  //   cur->exit_status->terminated = 1;
  // }
  // lock_release (&cur->list_lock);
  if (!cur->kernel)
  {
    printf ("%s: exit(%d)\n", thread_current ()->name, status);
  }
  thread_exit ();
}

// int
// sysopen (const char *file_name)
// {
//   if (!valid_user_pointer ((void *) file_name, 0))
//     sysexit (-1);
//   if (!valid_user_pointer ((void *) file_name, strlen (file_name)))
//     sysexit (-1);
//   if (!file_name)
//     sysexit (-1);

//   char* ret = (char *) file_name;
//   while (*ret) {
//     if (ret + 1)
//     {
//       if (!valid_user_pointer (++ret, 0))
//         sysexit (-1);
//     }
//     else
//     {
//       sysexit (-1);
//     }
//   }
  
//   struct file *f = filesys_open (file_name);
//   if (f)
//   {
//     struct thread* cur = thread_current ();
//     struct file** tmp;
//     if (cur->file_handlers_number == 2)
//     {
//       tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
//       memset (tmp, 0, 3 * sizeof(struct file*));
//     }
//     else
//     {
//       tmp = malloc ((cur->file_handlers_number + 1) * sizeof(struct file*));
//       memcpy (tmp, cur->file_handlers, cur->file_handlers_number * sizeof(struct file*));
//       free (cur->file_handlers);
//     }
//     cur->file_handlers = tmp;
//     cur->file_handlers[cur->file_handlers_number] = f;
//     int ret_file_handlers_number = cur->file_handlers_number;
//     cur->file_handlers_number++;
//     return ret_file_handlers_number;
//   }
//   return -1;
// }
int
sysopen (const char *file, void* esp) {
    // printf ("sysopen begin\n");
    check_usr_string(file, esp);
    pinning_file (file, false);
    // if (!pinning_file(file)) {
    //   // printf ("sysopen return 111\n");
    //     return -1;
    // }
    // lock_acquire(&file_system_lock);
    unsigned length = strlen(file);
    lock_acquire(&file_system_lock);
    struct file* f = filesys_open(file);
    lock_release(&file_system_lock);
    select_pinning_page (file, length, false);

    // lock_release(&file_system_lock);
    // if (fp == NULL) {
    //     select_pinning_page (file, length, false);
    //     // lock_release(&file_system_lock);
    //     return -1;
    // }
    // select_pinning_page (file, length, false);
    // int f = add_to_open_file_list(fp);
    // lock_release(&file_system_lock);
    // return fd;
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
      // printf ("sysopen return 222, with file number %d\n", ret_file_handlers_number);
      return ret_file_handlers_number;
    }
    // printf ("sysopen return 333\n");
    return -1;
}

int
syswait (int pid)
{
   return process_wait (pid);
}

// int
// syswrite (int fd, const void * buffer, unsigned size)
// {
//   if (!valid_user_pointer ((void *) buffer, 0))
//     sysexit (-1);
//   if (!valid_user_pointer ((void *) buffer, size))
//     sysexit (-1);
//   if (fd == 0) sysexit (-1);
//   if (!valid_user_pointer ((void *) buffer, size))
//   {
//     sysexit (-1);
//   }
//   if (fd > (thread_current()->file_handlers_number - 1)) 
//   {
//     sysexit (-1);
//   }
//   int ans;
//   if (fd == 1)
//   {
//     putbuf (buffer, size);
//     ans = size;
//   }
//   else
//   {
//     struct thread *cur = thread_current ();
//     struct file *f = cur->file_handlers[fd];
//     if (fd >= cur->file_handlers_number)
//     {
//       sysexit (-1);
//     }
//     else 
//     {
//       ans = file_write (f, buffer, size);
//     }
//   }
//   return ans;
// }

int
syswrite (int fd, const void *buffer, unsigned length, void* esp) {
    check_usr_buffer(buffer, length, esp, false);
    select_pinning_page (buffer, length, true);
    
    // if (fd == STDOUT_FILENO) {
    //     putbuf(buffer, length);
    //     select_pinning_page (buffer, length, false);
    //     return length;
    // }
    
    // // lock_acquire(&file_system_lock);
    // struct file_package* package = get_file_package_from_open_list(fd);
    // if (package == NULL) {
    //     // lock_release(&file_system_lock);
    //     select_pinning_page (buffer, length, false);
    //     return -1;
    // }
    // int num_bytes_written = file_write_at(package->fp, buffer, length, package->position);
    // select_pinning_page (buffer, length, false);
    // package->position += num_bytes_written;
    // // lock_release(&file_system_lock);
    // return num_bytes_written;
  
  int ans;
  if (fd == 1)
  {
    putbuf (buffer, length);
    ans = length;
    // printf ("end call write 111\n");
  }
  else
  {
    struct thread *cur = thread_current ();
    // printf ("fd %d\n", fd);
    if (fd >= cur->file_handlers_number || fd <= 0)
    {
      select_pinning_page (buffer, length, false);
      // sysexit (-1);
      // printf ("end call write 222\n");
      // return ans;
      return -1;
    }
    else 
    {

      struct file *f = cur->file_handlers[fd];
      lock_acquire(&file_system_lock);
      ans = file_write (f, buffer, length);
      lock_release(&file_system_lock);
    }
  }
  select_pinning_page (buffer, length, false);
  // printf ("end call write\n");
  return ans;
}


// tid_t 
// sysexec (const char * cmd_line)
// {
//   if (!valid_user_pointer ((void *) cmd_line, 0))
//     sysexit (-1);

//   char* ret = (char *) cmd_line;
//   while (*ret) {
//     if (!valid_user_pointer (++ret, 0))
//       sysexit (-1);
//   }

//   tid_t pid = process_execute (cmd_line);
//   return pid;
// }

tid_t
sysexec (const char* command_line, void* esp) {
    check_usr_string(command_line, esp);
    struct thread* curr_thread = thread_current();
    
    unsigned length = pinning_len(command_line);
    
    int pid = process_execute(command_line);
    select_pinning_page (command_line, length, false);
    if (pid == TID_ERROR) {
        return -1;
    }
    return pid;
    // if (pid == TID_ERROR) {
    //     return -1;
    // }
    // sema_down (&curr_thread->sema_child_load);
    // if (curr_thread->child_did_load_successfully) {
    //     curr_thread->child_did_load_successfully = false;
    //     return pid;
    // }
    // return -1;
}

void 
syshalt (void) 
{
  shutdown_power_off();
}

// bool 
// syscreate (const char *file, unsigned initial_size) 
// {

//   bool ans = false;
//   if (!valid_user_pointer ((void *) file, 0) || 
//       !valid_user_pointer ((void *) file, initial_size))
//   {
//     sysexit (-1);
//   }


//   char* ret = (char *) file;
//   while (*ret) {
//     if (!valid_user_pointer (++ret, 0))
//       sysexit (-1);
//   }

//   if (file == NULL) {
//     sysexit (-1);
//   } else {
//     ans = filesys_create (file, initial_size);
//   }
//   return ans;
// }


bool syscreate (const char *file, unsigned initial_size, void* esp) {
    check_usr_string(file, esp);
    pinning_file (file, false);
    // if (!pinning_file(file)) {
    //     return false;
    // }
    // 
    unsigned length = strlen(file);
    lock_acquire(&file_system_lock);
    bool outcome = filesys_create(file, initial_size);
    lock_release(&file_system_lock);
    select_pinning_page (file, length, false);
    // lock_release(&file_system_lock);
    
    return outcome;
}

// bool 
// sysremove (const char *file) 
// {
//   char* ret = (char *) file;
//   while (*ret) {
//     if (!valid_user_pointer (++ret, 0))
//       sysexit (-1);
//   }
//   return filesys_remove(file);
// }
bool 
sysremove (const char *file, void* esp) {
    check_usr_string(file, esp);
    pinning_file (file, false);
    // if (!pinning_file(file)) {
    //     // return -1;
    //     return false;
    // }
    
    
    unsigned length = strlen(file);
    lock_acquire(&file_system_lock);
    bool outcome = filesys_remove(file);
        lock_release(&file_system_lock);
    select_pinning_page (file, length, false);

    
    return outcome;
}

int 
sysfilesize (int fd) 
{
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  return (int) file_length (f); 
}

// int 
// sysread (int fd, void *buffer, unsigned size) 
// {
//   if (fd > (thread_current()->file_handlers_number - 1)) 
//   {
//     sysexit (-1);
//   }
  
//   if (!valid_user_pointer (buffer, 0) || 
//       !valid_user_pointer (buffer, size))
//   {
//     sysexit (-1);
//   }
//   if (fd == 1) sysexit (-1);
//   int ans = -1;
//   if (fd == 0) {
//     char *input_buffer = (char *) buffer;
//     for (unsigned i = 0; i < size; i++) {
//       uint8_t input_char = input_getc();
//       input_buffer[i] = input_char;
//     }
//     ans = size; 
//   } else {
//     struct thread *cur = thread_current ();
//     struct file *f = cur->file_handlers[fd];
//     if (f == NULL) ans = -1;
//     else ans = file_read (f, buffer, size);
//   }
//   return ans;
// }

int 
sysread (int fd, void *buffer, unsigned length, void* esp) {
    if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit (-1);
    // return -1;
  }
  if (fd == 1)
  {
    sysexit (-1);
  } 

    check_usr_buffer(buffer, length, esp, true);
    select_pinning_page (buffer, length, true);

  // pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/read-stdout -a read-stdout --swap-size=4 -- -q  -f run read-stdout
  int ans = -1;

  struct thread *cur = thread_current ();
  if (fd == 0) 
  {
    char *input_buffer = (char *) buffer;
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
    if (f == NULL) ans = -1;
    else 
    {
      lock_acquire(&file_system_lock);
      ans = file_read (f, buffer, length);
      lock_release(&file_system_lock);
    }
  }
  select_pinning_page (buffer, length, false);
  return ans;
    
    // if (fd == STDIN_FILENO) {
    //     char* char_buff = (char*)buffer;
    //     unsigned i;
    //     for (i = 0; i < length; i++) {
    //         char_buff[i] = input_getc();
    //     }
    //     select_pinning_page (buffer, length, false);
    //     return length;
    // }
    
    // lock_acquire(&file_system_lock);
    // struct file_package* package = get_file_package_from_open_list(fd);
    // if (package == NULL) {
    //     // lock_release(&file_system_lock);
    //     select_pinning_page (buffer, length, false);
    //     return -1;
    // }
    // int num_bytes_read = file_read_at(package->fp, buffer, length, package->position);
    // select_pinning_page (buffer, length, false);
    // // package->position += num_bytes_read;
    // // lock_release(&file_system_lock);
    // return num_bytes_read;
}


void 
sysseek (int fd, unsigned position) 
{
  if (fd > (thread_current()->file_handlers_number - 1)) 
  {
    sysexit (-1);
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
    sysexit (-1);
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
    sysexit (-1);
  }
  if (fd == 1) sysexit (-1);
  if (fd == 0) sysexit (-1);
  struct thread *cur = thread_current ();
  struct file *f = cur->file_handlers[fd];
  file_close (f);
  cur->file_handlers_number--;
}
