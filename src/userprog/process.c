#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
static thread_func start_process NO_RETURN;
static bool load (char *file_name, void (**eip) (void), void **esp, 
            char** command_arguments, int command_arguments_number);
/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *command_line)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of command_line.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, command_line, PGSIZE);
  
  /* Our own structure for arguments, good for extensibility. */
  struct aux_args_struct *aux_args = malloc(sizeof(struct aux_args_struct));
  char *save_ptr;
  char *token;
  bool first_strtok = true;
  sema_init (&aux_args->sema_for_loading, 0);

  /* Get all arguments and save in array. */
  int i = 0;
  /* ONLY USE fn_copy HERE!!!! Otherwise there's a race between the caller and load(). */
  for (token = strtok_r ((char *) fn_copy, " ", &save_ptr); token != NULL;
    token = strtok_r (NULL, " ", &save_ptr))
  {
    if (first_strtok)
    {
      aux_args->file_name = token;
      first_strtok = false;
    }
    else
    {
      aux_args->command_arguments[i] = token;
      i++;
    }
  }

  aux_args->command_arguments_number = i;
  aux_args->success = false;
  aux_args->fn_copy = fn_copy;

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (aux_args->file_name, PRI_DEFAULT, start_process, aux_args);
  sema_down (&aux_args->sema_for_loading);

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
  int ret = TID_ERROR;
  if (aux_args->success)
  {
    ret = tid;
  }
  free (aux_args);
  return ret;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux_args_)
{
  /* This would get aux_args. */
  struct aux_args_struct *aux_args = (struct aux_args_struct*) aux_args_;
  char *file_name = aux_args->file_name;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp, aux_args->command_arguments, 
                                  aux_args->command_arguments_number);
  aux_args->success = success;

  if (success)
  {
    struct thread* cur = thread_current ();
    /* A user process. */
    cur->kernel = false;
  } 
  
  palloc_free_page (aux_args->fn_copy);
  if (!success)
  {
    struct thread* cur = thread_current ();
    lock_acquire (&cur->list_lock);
    struct list_elem *e;
    for (e = list_begin (&cur->parent->children_exit_status_list); 
         e != list_end (&cur->parent->children_exit_status_list);
         e = list_next (e))
    {
      /* If ITD was a child of the calling process. */
      struct exit_status_struct* es = list_entry (e, struct exit_status_struct, exit_status_elem);
      if (es->process_id == cur->tid) {
        es->terminated= 1;
        break;
      }
    }
    lock_release (&cur->list_lock);
    sema_up (&aux_args->sema_for_loading);
    thread_exit ();
  }
  sema_up (&aux_args->sema_for_loading);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.
   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  /* Waits for a child process pid and retrieves the child's exit status. 
  thread_current () is the parent process. */
  struct thread *cur = thread_current ();
  int ret_exit_status = -1;
  struct list_elem *e;

  /* Acquire the lock of the list. */
  lock_acquire (&cur->list_lock);
  for (e = list_begin (&cur->children_exit_status_list); 
      e != list_end (&cur->children_exit_status_list);
       e = list_next (e))
    {
      /* If TID was a child of the calling process. */
      struct exit_status_struct* es = list_entry (e, struct exit_status_struct, exit_status_elem);
      if (es->process_id == child_tid) {
        sema_down (&es->sema_wait_for_child);
        ret_exit_status = es->exit_status;
        /* One process only wait once. */
        list_remove (&es->exit_status_elem);
        free (es);
        lock_release (&cur->list_lock);
        return ret_exit_status;
      }
    }
  lock_release (&cur->list_lock);
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
 struct thread *cur = thread_current ();
 uint32_t *pd;
 /* Free terminated children threads. */


 struct list_elem *e;
 /* Acquire the lock of the list. */
 lock_acquire (&cur->list_lock);
 for (e = list_begin (&cur->children_exit_status_list); e != list_end (&cur->children_exit_status_list);
      e = list_next (e))
   {
     /* If ITD was a child of the calling process. */
     struct exit_status_struct* es = list_entry (e, struct exit_status_struct, exit_status_elem);
     if(es->terminated == 1)
     {
       /* list_lock is already held. Remove it directly */
       list_remove (&es->exit_status_elem);
       free (es);
     }
   }
 lock_release (&cur->list_lock);


 /* Free current thread. */
 if(cur->exit_status->terminated == 1)
 {
   list_remove (&cur->exit_status->exit_status_elem);
   free (cur->exit_status);


   lock_acquire (&cur->list_lock);
   for (e = list_begin (&cur->children_exit_status_list); e != list_end (&cur->children_exit_status_list);
       e = list_next (e))
     {
       /* If ITD was a child of the calling process. */
       struct exit_status_struct* es = list_entry (e, struct exit_status_struct, exit_status_elem);
    
       /* list_lock is already held. Remove it directly */
       list_remove (&es->exit_status_elem);
       free (es);
    
     }
   lock_release (&cur->list_lock);
 }


 if (cur->file_handlers != NULL)
 {
   for (int fd = 2; fd < cur->file_handlers_number; fd++)
   {
     if (cur->file_handlers[fd] != NULL)
     {
       file_close (cur->file_handlers[fd]);
     }
   }
   free (cur->file_handlers);
 }

 /* Close executable of process (which allows write). */
 file_close(cur->exec_file); 


 /* Destroy the current process's page directory and switch back
    to the kernel-only page directory. */
 pd = cur->pagedir;
 if (pd != NULL)
   {
     /* Correct ordering here is crucial.  We must set
        cur->pagedir to NULL before switching page directories,
        so that a timer interrupt can't switch back to the
        process page directory.  We must activate the base page
        directory before destroying the process's page
        directory, or our active page directory will be one
        that's been freed (and cleared). */
     cur->pagedir = NULL;
     pagedir_activate (NULL);
     pagedir_destroy (pd);
   }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();
  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char* file_name, char** command_arguments, 
                              int command_arguments_number);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (char *file_name, void (**eip) (void), void **esp, char** command_arguments, 
                                          int command_arguments_number)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  

  process_activate ();

  file = filesys_open (file_name);
  /* Save executable file to thread specific variable. */
  t->exec_file = file;
  
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  
  /* Deny writes to executables. */
  file_deny_write(file);   

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, file_name, command_arguments, command_arguments_number))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  // printf ("done load\n");
  return success;
}


static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:
        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.
        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.
   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      void* spid = pg_round_down (upage);
      struct supplementary_page_table_entry* spte = supplementary_page_table_entry_create (
        spid,
        writable,
        FILE_SYSTEM,
        page_read_bytes,
        page_zero_bytes,
        file,
        ofs
      );
      if (spte == NULL) {
          return false;
      }
      supplementary_page_table_entry_insert (spte);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      // DONT FORGET TO UPDATE OFFSET
      ofs += page_read_bytes;
    }
  // printf ("load segment return\n");
  return true;
}


/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char* file_name, char** command_arguments, int command_arguments_number)
{
  uint8_t *kpage;
  bool success = false;
  int stack_size = 0;

  char** arg_pointer = malloc ((((PGSIZE / sizeof(char *) - 8) / 2) 
                            * sizeof(char *) - 8) * sizeof (char*));
  memset(arg_pointer, 0, ((((PGSIZE / sizeof(char *) - 8) / 2) 
                            * sizeof(char *) - 8) * sizeof (char*)));
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      /* PGSIZE (1 << 12) PGBITS = 12 */
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
      {
        /* Argument passing. */
        *esp = PHYS_BASE;

        /* Place the words at the top of the stack. */
        for (int i = command_arguments_number - 1; i >= 0; i--)
        {
          /* +1 for \0. */
          int cmd_line_arg_len = strlen (command_arguments[i]) + 1;
          stack_size += cmd_line_arg_len;
          *esp -= cmd_line_arg_len;
          memcpy(*esp, command_arguments[i], cmd_line_arg_len);
          /* Save the address. arg_pointer points to esp address. */
          arg_pointer[i] = *esp;
        }
        
        int file_name_len = strlen (file_name) + 1;
        stack_size += file_name_len;
        *esp -= file_name_len;
        memcpy(*esp, file_name, file_name_len);
        char* file_name_address = *esp;

        /* Round the stack pointer down to a multiple of 4 before the 
        first push. Set 0 uint8_t. */
        int round_bit = (4 - stack_size % 4) % 4;
        *esp -= round_bit;
        memset(*esp, 0, round_bit);

        /* Push the address of each string plus a null pointer sentinel, 
          on the stack, in right-to-left order. */
        /* For esp, char[4] == char *, both -4. */
        *esp -= sizeof (char *);
        /* Save char* in *esp. So it's like esp->char_pointer->char. */
        *(char**)*esp = NULL;

        for (int i = command_arguments_number - 1; i >= 0; i--) 
        {
          *esp -= sizeof (char *);
          *(char**)*esp = arg_pointer[i];
        }

        *esp -= sizeof (char *);
        *(char**)*esp = file_name_address;

        /* Push argv (the address of argv[0]) and argc. Here 
          argc is the size of command_arguments. */
        *esp -= sizeof (char **);
        *((char **)*esp) = *esp + sizeof (char **);
        // *(char***)*esp = *esp + sizeof (char **);
        *esp -= sizeof (int);
        *(int *)*esp = command_arguments_number + 1;

        /* Finally, push a fake "return address". */
        *esp -= sizeof (void *);
        *(void**)*esp = NULL;
      }
      else
      {
        palloc_free_page (kpage);
      }
    }
  free (arg_pointer);
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
} 
