#include "vm/frame.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "vm/page.h"
// #include "userporg/process.h"

uint32_t clock = 0;

bool
frame_table_empty (struct frame_table *f, size_t start, size_t cnt) 
{
  size_t i;
  
  ASSERT (f != NULL);
  ASSERT (start <= f->frame_table_entry_number);
  ASSERT (start + cnt <= f->frame_table_entry_number);

  for (i = 0; i < cnt; i++)
    if (f->frame_table_entry[start + i].frame)
      return true;
  return false;
}

/* Finds the first index of CNT consecutive entries in f at 
   or after START that are empty. (Entries for frame table).
   returns the index of the first bit in the group.
   If there is no such group, returns FRAME_TABLE_ERROR. 
   Implement frame table scan following bitmap bitmap_scan. */
size_t
frame_table_scan (struct frame_table *f, size_t start, size_t cnt)
{
  ASSERT (f != NULL);
  ASSERT (start <= f->frame_table_entry_number);

  if (cnt <= f->frame_table_entry_number) 
    {
      size_t last = f->frame_table_entry_number - cnt;
      size_t i;
      for (i = start; i <= last; i++)
      {
        if (f->frame_table_entry[i].frame)
        {
          /* Non empty, continue scan. */
        }
        else
        {
          if (frame_table_empty (f, i, cnt))
          {
            /* All empty, scan success. */
            return i; 
          }
        }
      }
    }
  return FRAME_TABLE_ERROR;
}

void
frame_table_init (size_t frame_table_entry_number, uint8_t* frame_table_base)
{
  frame_table = (struct frame_table*) malloc (sizeof(struct frame_table));
  frame_table->base = frame_table_base;
  frame_table->frame_table_entry = (struct frame_table_entry*) malloc (frame_table_entry_number * sizeof(struct frame_table_entry));
  frame_table->frame_table_entry_number = frame_table_entry_number;
  for(size_t i = 0; i < frame_table_entry_number; i++)
  {
    lock_init (&(frame_table->frame_table_entry[i].frame_lock));
    frame_table->frame_table_entry[i].frame = (uint32_t*) (frame_table->base + (i << 12));
  }
  lock_init (&(frame_table->frame_table_lock));
}

uint32_t
frame_table_get_id (void* kaddr)
{
  return ((uint32_t) kaddr - (uint32_t) frame_table->base) >> 12;
}

void
frame_table_entry_free (struct supplementary_page_table_entry* spte) {
  // no frame at the beginning
  if (spte->fid == NULL)
  {
    return ;
  }
  struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  lock_acquire (&fte.frame_lock);
  // the frame is still occupied
  if (fte.spte->pid == spte->pid)
  {
    spte->fid = NULL;
    fte.spte = NULL;
    palloc_free_page(fte.frame);
  }
  lock_release (&fte.frame_lock);
}


void clock_algorithm (void)
{
  clock += 1;
  clock = clock % frame_table->frame_table_entry_number;
}

struct frame_table_entry* frame_table_evict (void) {
  // clock_algorithm ();
  struct frame_table_entry fte;
  while (true)
  {
    fte = frame_table->frame_table_entry[clock];
    if (!lock_held_by_current_thread (&fte.frame_lock))
    {
      if (lock_try_acquire (&fte.frame_lock))
      {
        lock_acquire (&fte.spte->page_lock);
        uint32_t* pagedir = fte.spte->pagedir;
        // Returns true if page directory pd contains a page table entry for page that is marked dirty (or accessed). Otherwise, returns false.
        bool accessed = pagedir_is_accessed (pagedir, fte.spte->pid);
        // If page directory pd has a page table entry for page, then its dirty (or accessed) bit is set to value.
        pagedir_set_accessed (pagedir, fte.spte->pid, false);
        lock_release (&fte.spte->page_lock);
        if (accessed) {
          lock_release(&fte.frame_lock);
        } else {
          lock_acquire (&fte.spte->page_lock);

          // lock_acquire(&fte.spte->page_lock);
          pagedir_clear_page (fte.spte->pagedir, fte.spte->pid);
          // lock_release(&fte.spte->page_lock);

          if (fte.spte->location == MAP_MEMORY)
          {
            evict_page_map (fte.spte);
          }
          else if (fte.spte->location == FILE_SYSTEM)
          {
            evict_page_file (fte.spte);
          }
          else if (fte.spte->location == SWAP_BLOCK)
          {
            evict_page_swap (fte.spte);
          }
          else
          {
            sysexit (-1);
          }
          lock_release (&fte.spte->page_lock);
          fte.spte = NULL;

          return &fte;
        }
      }
    }
    clock_algorithm ();
  }
  return NULL;
}