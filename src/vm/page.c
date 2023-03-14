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

struct supplementary_page_table_entry 
*supplementary_page_table_entry_create(void *pid,
bool writeable,
page_location location,
size_t page_read_bytes,
size_t page_zero_bytes,
struct file *file,
off_t file_ofs,
bool in_physical_memory)
{
    struct supplementary_page_table_entry *spte = 
            malloc(sizeof(struct supplementary_page_table_entry));
    lock_init(&spte->page_lock);

    spte->pid = pid;
    spte->sid = NULL;
    spte->fid = NULL;

    spte->writeable = writeable;
    spte->location = location;
    spte->in_physical_memory = in_physical_memory;

    spte->page_read_bytes = page_read_bytes;
    spte->page_zero_bytes = page_zero_bytes;

    spte->file = file;
    spte->file_ofs = file_ofs;

    spte->owner_thread = thread_current();

    return spte;
}

void supplementary_page_table_entry_insert(struct supplementary_page_table_entry *spte)
{
    struct thread *cur = thread_current();
    lock_acquire(&cur->supplementary_page_table_lock);
    list_push_back(&thread_current()->supplementary_page_table, 
            &spte->supplementary_page_table_entry_elem);
    lock_release(&cur->supplementary_page_table_lock);
}

void evict_page_from_swap_block(struct supplementary_page_table_entry *spte)
{
    struct frame_table_entry *fte = &(frame_table->frame_table_entry[(uint32_t)spte->fid]);
    uint32_t swap_index = swap_block_write(fte->frame);
    spte->sid = swap_index;
}

void evict_page_from_file_system(struct supplementary_page_table_entry *spte)
{
    lock_acquire(&spte->owner_thread->pagedir_lock);
    uint32_t *pagedir = spte->owner_thread->pagedir;
    struct frame_table_entry *fte = &(frame_table->frame_table_entry[(uint32_t)spte->fid]);
    bool dirty = pagedir_is_dirty(pagedir, fte->spte->pid);
    lock_release(&spte->owner_thread->pagedir_lock);
    if (dirty)
    {
        spte->location = SWAP_BLOCK;
        evict_page_from_swap_block(spte);
    }
}

void evict_page_from_map_memory(struct supplementary_page_table_entry *spte)
{

    uint32_t *pagedir = spte->owner_thread->pagedir;
    lock_acquire(&spte->owner_thread->pagedir_lock);
    void *page_id = spte->pid;
    bool dirty = pagedir_is_dirty(pagedir, page_id);
    if (dirty)
    {
        pagedir_set_dirty(pagedir, page_id, false);
        lock_release(&spte->owner_thread->pagedir_lock);
        lock_acquire(&file_system_lock);
        struct frame_table_entry *fte = &(frame_table->frame_table_entry[(uint32_t)spte->fid]);
        file_write_at(spte->file, fte->frame, spte->page_read_bytes,
                      spte->file_ofs);
        lock_release(&file_system_lock);
    }
    else
    {
        lock_release(&spte->owner_thread->pagedir_lock);
    }
}

struct supplementary_page_table_entry *supplementary_page_table_entry_find(void *vaddr)
{
    struct thread *cur = thread_current();
    void *pid = pg_round_down(vaddr);
    lock_acquire(&cur->supplementary_page_table_lock);
    struct list_elem *e;
    for (e = list_begin(&cur->supplementary_page_table); 
         e != list_end(&cur->supplementary_page_table);
         e = list_next(e))
    {
        struct supplementary_page_table_entry *spte = 
            list_entry(e, struct supplementary_page_table_entry, 
                supplementary_page_table_entry_elem);
        if (spte->pid == pid)
        {
            lock_release(&cur->supplementary_page_table_lock);
            return spte;
        }
    }
    lock_release(&cur->supplementary_page_table_lock);
    return NULL;
}

void free_supplementary_page_table_entry(struct list_elem *e)
{
    struct supplementary_page_table_entry *spte = 
        list_entry(e, struct supplementary_page_table_entry, 
            supplementary_page_table_entry_elem);
    lock_acquire(&spte->page_lock);
    if (spte->in_physical_memory)
    {
        clear_page_copy(spte->pid, spte->owner_thread);
        frame_table_entry_free(spte);
        spte->in_physical_memory = false;
    }
    lock_release(&spte->page_lock);
    free(spte);
}

void free_supplementary_page_table(struct list *supplementary_page_table)
{

    while (!list_empty(supplementary_page_table))
    {
        struct list_elem *list_elem = list_pop_front(supplementary_page_table);
        free_supplementary_page_table_entry(list_elem);
    }
    list_init(supplementary_page_table);
}

bool install_page_copy(void *upage, void *kpage, bool writeable)
{
    struct thread *t = thread_current();

    lock_acquire(&t->pagedir_lock);
    bool result = (pagedir_get_page(t->pagedir, upage) == NULL 
            && pagedir_set_page(t->pagedir, upage, kpage, writeable));
    lock_release(&t->pagedir_lock);
    return result;
}

void clear_page_copy(void *upage, struct thread *t)
{
    lock_acquire(&t->pagedir_lock);
    if (t->pagedir != NULL)
    {
        pagedir_clear_page(t->pagedir, upage);
    }
    lock_release(&t->pagedir_lock);
}

bool stack_growth(void *fault_addr)
{
    void *spid = pg_round_down(fault_addr);
    bool writeable = true;

    size_t page_read_bytes = 0;
    size_t page_zero_bytes = 0;

    struct file *file = NULL;
    off_t file_ofs = 0;
    struct supplementary_page_table_entry *spte = supplementary_page_table_entry_create(
        spid,
        writeable,
        SWAP_BLOCK,
        page_read_bytes,
        page_zero_bytes,
        file,
        file_ofs,
        true);
    if (spte == NULL)
    {
        return false;
    }

    lock_acquire(&spte->page_lock);
    supplementary_page_table_entry_insert(spte);
    /* Kernel address. */
    lock_acquire(&frame_table->frame_table_lock);
    void *kaddr = palloc_get_page(PAL_USER | PAL_ZERO);
    struct frame_table_entry *fte;

    if (kaddr)
    {
        spte->fid = (void *)frame_table_get_id(kaddr);
        fte = &frame_table->frame_table_entry[frame_table_get_id(kaddr)];
        lock_acquire(&fte->frame_lock);
        lock_release(&frame_table->frame_table_lock);
    }
    else
    {
        fte = frame_table_evict();
        spte->fid = frame_table_find_id(fte);
    }

    bool success = false;
    memset(fte->frame, 0, PGSIZE);

    success = install_page_copy(spte->pid, fte->frame, spte->writeable);
    if (!success)
    {
        spte->fid = NULL;
        fte->spte = NULL;
        spte->in_physical_memory = false;
        palloc_free_page(kaddr);
        lock_release(&fte->frame_lock);
        lock_release(&spte->page_lock);
    }
    else
    {

        fte->spte = spte;
        spte->in_physical_memory = true;

        lock_release(&fte->frame_lock);
        lock_release(&spte->page_lock);
    }
    return success;
}

void pin_frame(struct supplementary_page_table_entry *spte)
{
    lock_acquire(&frame_table->frame_table_lock);
    void *kaddr = palloc_get_page(PAL_USER);
    struct frame_table_entry *fte;

    if (kaddr)
    {
        spte->fid = (void *)frame_table_get_id(kaddr);
        fte = &frame_table->frame_table_entry[frame_table_get_id(kaddr)];
        lock_acquire(&fte->frame_lock);
        lock_release(&frame_table->frame_table_lock);
    }
    else
    {
        fte = frame_table_evict();
        spte->fid = frame_table_find_id(fte);
    }

    if (spte->location == MAP_MEMORY)
    {
        load_page_from_map_memory(spte, fte);
    }
    else if (spte->location == SWAP_BLOCK)
    {
        load_page_from_swap_block(spte, fte);
    }
    else if (spte->location == FILE_SYSTEM)
    {
        load_page_from_file_system(spte, fte);
    }
    else
    {
        sysexit(-1);
    }
    bool success = install_page_copy(spte->pid, fte->frame, spte->writeable);
    if (!success)
    {
        spte->fid = NULL;

        fte->spte = NULL;
        spte->in_physical_memory = false;
        palloc_free_page(kaddr);
        lock_release(&fte->frame_lock);
        lock_release(&spte->page_lock);

        sysexit(-1);
    }
    else
    {

        fte->spte = spte;
        spte->in_physical_memory = true;

        lock_release(&spte->page_lock);
        return;
    }
}

void pin_page(void *virtual_address)
{
    struct supplementary_page_table_entry *spte = 
            supplementary_page_table_entry_find(virtual_address);
    lock_acquire(&spte->page_lock);
    if (spte->in_physical_memory != true)
    {
        pin_frame(spte);
    }
    else
    {
        struct frame_table_entry *fte = &(frame_table->frame_table_entry[(uint32_t)spte->fid]);
        if (frame_lock_try_aquire(fte, spte))
        {
            lock_release(&spte->page_lock);
            return;
        }
        else
        {
            if (spte->in_physical_memory != true)
            {
                pin_frame(spte);
                return;
            }
            else
            {
                if (spte->in_physical_memory == true)
                {
                    frame_table_entry_free(spte);
                    pin_frame(spte);
                    return;
                }
            }
        }
    }
}

void unpin_page(void *virtual_address)
{
    struct supplementary_page_table_entry *spte = 
            supplementary_page_table_entry_find(virtual_address);
    lock_acquire(&spte->page_lock);
    struct frame_table_entry *fte = &(frame_table->frame_table_entry[(uint32_t)spte->fid]);
    lock_release(&fte->frame_lock);
    lock_release(&spte->page_lock);
}

bool supplementary_page_table_entry_find_between
(struct file *file, void *start, void *end)
{
    struct thread* cur = thread_current ();
    struct list_elem *e;
    for (e = list_begin (&cur->supplementary_page_table); 
         e != list_end (&cur->supplementary_page_table);
        e = list_next (e))
    {
      struct supplementary_page_table_entry* spte = 
            list_entry (e, struct supplementary_page_table_entry, 
                supplementary_page_table_entry_elem);
      if (spte->file) {
        int spte_size = file_length(spte->file);
        if ((end >= spte->pid) && (end <= (void *)((int)spte->pid + spte_size))
            && (spte->file != file)) {
            return true;
        }

        if ((start >= spte->pid) && (start <= (void *)((int)spte->pid + spte_size))
            && (spte->file != file)) {
            return true;
        }
      }
    }
    return false;
}

void clear_supplementary_page_table (void)
{
  struct thread* cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->supplementary_page_table); 
        e != list_end (&cur->supplementary_page_table);
      e = list_next (e))
  {
    struct supplementary_page_table_entry* spte = 
        list_entry (e, struct supplementary_page_table_entry, 
            supplementary_page_table_entry_elem);
    if (spte->location == MAP_MEMORY) {
      mapid_t mapping = (int)spte->pid >> 12;
      sysmunmap(mapping);
    }
  }
}