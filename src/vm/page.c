#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/file.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
struct supplementary_page_table_entry* supplementary_page_table_entry_create
(void* pid,
bool writable,
page_location location,
size_t page_read_bytes,
size_t page_zero_bytes,
struct file* file,
off_t file_ofs)
{
  struct supplementary_page_table_entry* spte = malloc (sizeof(struct supplementary_page_table_entry));
  lock_init (&spte->page_lock);
  
  spte->pid = pid;
  spte->writable = writable;

  spte->page_read_bytes = page_read_bytes;
  spte->page_zero_bytes = page_zero_bytes;

  spte->file = file;
  spte->file_ofs = file_ofs;

  return spte;
}

void supplementary_page_table_entry_insert (struct supplementary_page_table_entry* spte)
{
  lock_acquire (&spte->page_lock);
  list_push_back (&thread_current ()->supplementary_page_table, &spte->supplementary_page_table_entry_elem);
  lock_release (&spte->page_lock);
}

struct supplementary_page_table_entry* supplementary_page_table_entry_find
(void* vaddr)
{
  struct thread* cur = thread_current ();
  void* pid = pg_round_down (vaddr);
  struct list_elem *e;
  for (e = list_begin (&cur->supplementary_page_table); e != list_end (&cur->supplementary_page_table);
      e = list_next (e))
  {
    struct supplementary_page_table_entry* spte = list_entry (e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
    if (spte->pid == pid) {
      return spte;
    }
  }
  return NULL;
}

void load_page_from_map_memory (struct supplementary_page_table_entry* spte)
{
  // void* kaddr = palloc_get_page (PAL_USER);
  // spte->fid = frame_table_get_id (kaddr);
  // struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  load_page_from_file_system (spte);
}

void load_page_from_swap_block (struct supplementary_page_table_entry* spte)
{
  void* kaddr = palloc_get_page (PAL_USER);
  spte->fid = (void*) frame_table_get_id (kaddr);
  struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
}

void load_page_from_file_system (struct supplementary_page_table_entry* spte)
{
  /* Kernel address. */
  void* kaddr = palloc_get_page (PAL_USER);
  spte->fid = (void*) frame_table_get_id (kaddr);
  // Obtain a frame to store the page.
  struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];

  /* All zero page. */
  if (spte->page_zero_bytes == PGSIZE)
  {
    memset (fte.frame, 0, PGSIZE);
    return ;
  }

  
  // Fetch data into the frame.
  uint32_t read_bytes = file_read_at (spte->file, fte.frame, spte->page_read_bytes, spte->file_ofs);

  if (spte->page_read_bytes != PGSIZE)
  {
    memset (fte.frame + spte->page_read_bytes, 0, PGSIZE - spte->page_read_bytes);
  }

  if (read_bytes != spte->page_read_bytes)
  {
    sysexit (-1);
  }
  // Update the page table entry to point the virtual address to the new 
  // physical address
  struct thread *cur = thread_current ();
  bool success = false;
  if (pagedir_get_page (cur->pagedir, spte->pid) == NULL)
  {
    if (pagedir_set_page (cur->pagedir, spte->pid, fte.frame, spte->writable))
    {
      success = true;
    }
  }

  if (success)
  {

  }
  else
  {
    palloc_free_page (kaddr);
    sysexit (-1);
  }
}