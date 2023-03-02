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
  spte->sid = NULL; 
  spte->fid = NULL; 

  spte->writable = writable;
  spte->location = location;

  spte->page_read_bytes = page_read_bytes;
  spte->page_zero_bytes = page_zero_bytes;

  spte->file = file;
  spte->file_ofs = file_ofs;

  spte->pagedir = &thread_current()->pagedir;

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
  struct frame_table_entry fte;

  if (kaddr)
  {
    spte->fid = (void*) frame_table_get_id (kaddr);
    // Obtain a frame to store the page.
    fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  }
  else
  {
    printf ("eviction need!\n");
    fte.frame = frame_table_evict();
  }

  /* All zero page. */
  if (spte->page_zero_bytes == PGSIZE)
  {
    memset (fte.frame, 0, PGSIZE);
    if (install_page_copy (spte->pid, fte.frame, spte->writable))
    {
    }
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
    palloc_free_page (kaddr);
    sysexit (-1);
  }

  if (install_page_copy (spte->pid, fte.frame, spte->writable))
  {
  }
  else
  {
    palloc_free_page (kaddr);
    sysexit (-1);
    return ;
  }
}



/* Have some problem to include process.h.
   So just copy install_page here. */
/* If static, can't be called in other classes. (Why?) */
bool
install_page_copy (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
} 

void
evict_page_swap (struct supplementary_page_table_entry* spte)
{
  struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  uint32_t sid = swap_block_write (fte.frame);
  spte->sid = sid;
}

void 
evict_page_file (struct supplementary_page_table_entry* spte)
{
  lock_acquire (&spte->page_lock);
  uint32_t* pagedir = spte->pagedir;
  // struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  
  if (pagedir_is_dirty (spte->pagedir, spte->fid))
  {
    spte->location = SWAP_BLOCK;
    evict_page_swap (spte);
  }
  lock_release (&spte->page_lock);
}

void
evict_page_map(struct supplementary_page_table_entry* spte)
{
  lock_acquire (&spte->page_lock);
  if (pagedir_is_dirty (spte->pagedir, spte->pid))
  {
    pagedir_set_dirty (spte->pagedir, spte->pid, false);
    struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
    lock_acquire (&fte.frame_lock);
    file_write_at (spte->file, fte.frame, spte->page_read_bytes, spte->file_ofs);
    lock_acquire (&fte.frame_lock);
  }
  lock_release (&spte->page_lock);
}