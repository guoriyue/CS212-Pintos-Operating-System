#include "vm/frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <debug.h>
#include <stdio.h>
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "userprog/syscall.h"

static uint32_t clock_hand = 0;

static void clock_algorithm(void);

void frame_table_init(size_t frame_table_entry_number, uint8_t *frame_table_base)
{
    frame_table = (struct frame_table *)malloc(sizeof(struct frame_table));
    frame_table->base = frame_table_base;
    frame_table->frame_table_entry = (struct frame_table_entry *)malloc(frame_table_entry_number * sizeof(struct frame_table_entry));
    frame_table->frame_table_entry_number = frame_table_entry_number;
    for (size_t i = 0; i < frame_table_entry_number; i++)
    {
        lock_init(&(frame_table->frame_table_entry[i].frame_lock));
        frame_table->frame_table_entry[i].frame = (uint32_t *)(frame_table->base + (i << 12));
        frame_table->frame_table_entry[i].spte = NULL;
    }
    lock_init(&(frame_table->frame_table_lock));
}

static void clock_algorithm()
{
    ASSERT(lock_held_by_current_thread(&frame_table->frame_table_lock));
    clock_hand++;
    if (clock_hand >= frame_table->frame_table_entry_number)
    {
        clock_hand = 0;
    }
}

uint32_t
frame_table_get_id(void *kaddr)
{
    return ((uint32_t)kaddr - (uint32_t)frame_table->base) >> 12;
}

uint32_t
frame_table_find_id(struct frame_table_entry *frame)
{
    for (uint32_t i = 0; i < frame_table->frame_table_entry_number; i++)
    {
        if (frame_table->frame_table_entry[i].frame == frame->frame)
        {
            return i;
        }
    }
}

bool frame_lock_try_aquire(struct frame_table_entry *frame, struct supplementary_page_table_entry *page_try_pin)
{

    if (frame == NULL)
    {
        page_try_pin->in_physical_memory = false;
        return false;
    }
    if (lock_held_by_current_thread(&frame->frame_lock))
    {
        return true;
    }
    lock_acquire(&frame->frame_lock);
    if (frame->spte == page_try_pin)
    {
        return true;
    }
    lock_release(&frame->frame_lock);
    return false;
}

void load_page_from_map_memory(struct supplementary_page_table_entry *spte, struct frame_table_entry *fte)
{
    load_page_from_file_system(spte, fte);
}

void load_page_from_swap_block(struct supplementary_page_table_entry *spte, struct frame_table_entry *fte)
{
    swap_block_read(fte->frame, (uint32_t)spte->sid);
}

void load_page_from_file_system(struct supplementary_page_table_entry *spte, struct frame_table_entry *fte)
{
    /* All zero page. */
    if (spte->page_zero_bytes == PGSIZE)
    {
        memset(fte->frame, 0, PGSIZE);
        return;
    }

    // Fetch data into the frame.
    lock_acquire(&file_system_lock);
    uint32_t read_bytes = file_read_at(spte->file, fte->frame, spte->page_read_bytes, spte->file_ofs);
    lock_release(&file_system_lock);

    if (spte->page_read_bytes != PGSIZE)
    {
        memset(fte->frame + spte->page_read_bytes, 0, PGSIZE - spte->page_read_bytes);
    }
    if (read_bytes != spte->page_read_bytes)
    {
        // palloc_free_page (kaddr);
        sysexit(-1);
    }

    return;
}

struct frame_table_entry *frame_table_evict(void)
{
    ASSERT(lock_held_by_current_thread(&frame_table->frame_table_lock));
    clock_algorithm();
    struct frame_table_entry *fte = NULL;
    while (true)
    {
        fte = &(frame_table->frame_table_entry[clock_hand]);

        if (!lock_held_by_current_thread(&fte->frame_lock))
        {
            if (lock_try_acquire(&fte->frame_lock))
            {
                if (fte->spte == NULL || fte->spte->owner_thread->pagedir == NULL)
                {
                    lock_release(&fte->frame_lock);
                    clock_algorithm();

                    continue;
                }

                lock_release(&frame_table->frame_table_lock);

                uint32_t *pagedir = fte->spte->owner_thread->pagedir;

                lock_acquire(&fte->spte->owner_thread->pagedir_lock);
                bool accessed = pagedir_is_accessed(pagedir, fte->spte->pid);
                pagedir_set_accessed(pagedir, fte->spte->pid, false);
                lock_release(&fte->spte->owner_thread->pagedir_lock);
                if (accessed)
                {
                    lock_release(&fte->frame_lock);
                }
                else
                {
                    lock_acquire(&fte->spte->page_lock);

                    clear_page_copy(fte->spte->pid, fte->spte->owner_thread);

                    if (fte->spte->location == MAP_MEMORY)
                    {
                        evict_page_from_map_memory(fte->spte);
                    }
                    else if (fte->spte->location == SWAP_BLOCK)
                    {
                        evict_page_from_swap_block(fte->spte);
                    }
                    else if (fte->spte->location == FILE_SYSTEM)
                    {
                        evict_page_from_file_system(fte->spte);
                    }
                    else
                    {
                        sysexit(-1);
                    }

                    fte->spte->fid = NULL;
                    fte->spte->in_physical_memory = false;
                    lock_release(&fte->spte->page_lock);
                    fte->spte = NULL;

                    return fte;
                }
                lock_acquire(&frame_table->frame_table_lock);
            }
        }
        clock_algorithm();
    }
    return NULL;
}

bool frame_table_entry_free(struct supplementary_page_table_entry *spte)
{

    if (spte->fid == NULL)
    {
        return true;
    }
    struct frame_table_entry *frame = &frame_table->frame_table_entry[(uint32_t)spte->fid];
    lock_acquire(&frame->frame_lock);
    if (frame->spte == spte)
    {
        // we are the owner of the frame, so we can free the page
        palloc_free_page(frame->frame);
        spte->fid = NULL;
        spte->in_physical_memory = false;
        frame->spte = NULL;
    }
    // we are not the current owner, we do not free it
    lock_release(&frame->frame_lock);
    return true;
}
