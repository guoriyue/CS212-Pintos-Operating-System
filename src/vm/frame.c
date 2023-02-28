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
// #include "userporg/process.h"
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
}

uint32_t
frame_table_get_id (void* kaddr)
{
  return ((uint32_t) kaddr - (uint32_t) frame_table->base) >> 12;
}