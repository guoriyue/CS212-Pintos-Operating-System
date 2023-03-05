// // #include <inttypes.h>
// // #include <round.h>
// // #include <stdio.h>
// // #include <stdlib.h>
// // #include <string.h>
// // #include <stdbool.h>
// // #include "threads/synch.h"
// // #include "threads/thread.h"
// // #include "threads/pte.h"
// // #include "threads/palloc.h"
// // #include "vm/page.h"
// // #include "vm/frame.h"
// // #include "vm/swap.h"
// // #include "filesys/file.h"
// // #include "userprog/syscall.h"
// // #include "userprog/pagedir.h"



// // struct supplementary_page_table_entry* supplementary_page_table_entry_find
// // (void* vaddr)
// // {
// //   struct thread* cur = thread_current ();
// //   void* pid = pg_round_down (vaddr);
// //   struct list_elem *e;
// //   for (e = list_begin (&cur->supplementary_page_table); e != list_end (&cur->supplementary_page_table);
// //       e = list_next (e))
// //   {
// //     struct supplementary_page_table_entry* spte = list_entry (e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
// //     if (spte->pid == pid) {
// //       return spte;
// //     }
// //   }
// //   return NULL;
// // }




// // /* Have some problem to include process.h.
// //    So just copy install_page here. */
// // /* If static, can't be called in other classes. (Why?) */
// // bool
// // install_page_copy (void *upage, void *kpage, bool writeable)
// // {
// //   struct thread *t = thread_current ();

// //   /* Verify that there's not already a page at that virtual
// //      address, then map our page there. */
// //   return (pagedir_get_page (t->pagedir, upage) == NULL
// //           && pagedir_set_page (t->pagedir, upage, kpage, writeable));
// // } 

// // void
// // evict_page_swap (struct supplementary_page_table_entry* spte)
// // {
// //   struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
// //   uint32_t sid = swap_block_write (fte.frame);
// //   spte->sid = &sid;
// // }

// // void 
// // evict_page_file (struct supplementary_page_table_entry* spte)
// // {
// //   ASSERT (&spte->page_lock != NULL);
// //   lock_acquire (&spte->page_lock);
// //   uint32_t* pagedir = spte->pagedir;
// //   // struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  
// //   if (pagedir_is_dirty (spte->pagedir, spte->fid))
// //   {
// //     spte->location = SWAP_BLOCK;
// //     evict_page_swap (spte);
// //   }
// //   lock_release (&spte->page_lock);
// // }

// // void
// // evict_page_map(struct supplementary_page_table_entry* spte)
// // {
// //   ASSERT (&spte->page_lock != NULL);
// //   lock_acquire (&spte->page_lock);
// //   if (pagedir_is_dirty (spte->pagedir, spte->pid))
// //   {
// //     pagedir_set_dirty (spte->pagedir, spte->pid, false);
// //     struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
// //     ASSERT (&fte.frame_lock != NULL);
// //     lock_acquire (&fte.frame_lock);
// //     file_write_at (spte->file, fte.frame, spte->page_read_bytes, spte->file_ofs);
// //     lock_release (&fte.frame_lock);
// //   }
// //   lock_release (&spte->page_lock);
// // }




#include <stdbool.h>
#include "threads/thread.h"
#include "threads/malloc.h"  
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/frame.h"
#include <stdio.h>
#include <stddef.h>
#include "filesys/file.h"
#include <string.h>
#include "userprog/syscall.h"


// static void free_hash_entry(struct hash_elem* e, void* aux UNUSED);


static void
assert_spte_consistency(struct supplementary_page_table_entry* spte)
{
    ASSERT (spte != NULL);
    ASSERT (spte->location == SWAP_BLOCK ||
            spte->page_read_bytes + spte->page_zero_bytes == PGSIZE);

    // ASSERT (spte->frame == NULL ||
    //         spte->frame->frame >= PHYS_BASE);

}


/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we aquire the lock here, to handle the case where we 
    allocate a swap page, in which case we pass is_loaded
    value of true. 
 NOTE: On failure of malloc, we return null. 
 NOTE: On failure to add the spte to the spte table, 
    we exit the thread. 
 --------------------------------------------------------------------
 */
struct supplementary_page_table_entry* create_spte_and_add_to_table(page_location location, void* page_id, bool is_writeable, bool is_loaded, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {

    struct supplementary_page_table_entry* spte = malloc(sizeof(struct supplementary_page_table_entry));
    if (spte == NULL) {
        return NULL;
    }
    spte->location = location;
    spte->owner_thread = thread_current();
    spte->pid = page_id;
    spte->writeable = is_writeable;
    spte->in_physical_memory = is_loaded;
    // spte->frame = NULL;
    spte->file = file_ptr;
    spte->file_ofs = offset;
    spte->page_read_bytes = read_bytes;
    spte->page_zero_bytes = zero_bytes;
    spte->sid = NULL; 
    spte->fid = NULL; 
    lock_init(&spte->page_lock);
    // lock_acquire (&spte->page_lock);
    // struct hash* target_table = &thread_current()->spte_table;
    // struct supplementary_page_table_entry* outcome = hash_entry(hash_insert(target_table, &spte->elem), struct supplementary_page_table_entry, elem);
    // if (outcome != NULL) {
    //     // thread_current()->vital_info->exit_status = -1;
    //     // if (thread_current()->is_running_user_program) {
    //     //     printf("%s: exit(%d)\n", thread_name(), -1);
    //     // }
    //     // thread_exit();
    //     sysexit(-1);
    // }
    supplementary_page_table_entry_insert (spte);

    // assert_spte_consistency(spte);
    return spte;
}


struct supplementary_page_table_entry* supplementary_page_table_entry_create
(void* pid,
bool writeable,
page_location location,
size_t page_read_bytes,
size_t page_zero_bytes,
struct file* file,
off_t file_ofs,
bool in_physical_memory)
{
  struct supplementary_page_table_entry* spte = malloc (sizeof(struct supplementary_page_table_entry));
  lock_init (&spte->page_lock);
  
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

  spte->pagedir = thread_current()->pagedir;
  spte->pagedir_lock = thread_current()->pagedir_lock;

  spte->owner_thread = thread_current();


  return spte;
}

// void supplementary_page_table_entry_insert (struct supplementary_page_table_entry* spte)
// {
//   ASSERT (&spte->page_lock != NULL);
//   lock_acquire (&spte->page_lock);
//   list_push_back (&thread_current ()->supplementary_page_table, &spte->supplementary_page_table_entry_elem);
//   lock_release (&spte->page_lock);
// }




void supplementary_page_table_entry_insert (struct supplementary_page_table_entry* spte)
{
  ASSERT (&spte->page_lock != NULL);
  lock_acquire (&spte->page_lock);
  struct thread* cur = thread_current ();
  lock_acquire (&cur->supplementary_page_table_lock);
  list_push_back (&thread_current ()->supplementary_page_table, &spte->supplementary_page_table_entry_elem);
  lock_release (&cur->supplementary_page_table_lock);
//   lock_release (&spte->page_lock);
}

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  --------------------------------------------------------------------
//  */
// void free_spte(struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     free(spte);
// }


// void load_page_from_map_memory (struct supplementary_page_table_entry* spte)
// {
//   // void* kaddr = palloc_get_page (PAL_USER);
//   // spte->fid = frame_table_get_id (kaddr);
//   // struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//   load_page_from_file_system (spte);
// }

// void load_page_from_swap_block (struct supplementary_page_table_entry* spte)
// {
//   void* kaddr = palloc_get_page (PAL_USER);
//   spte->fid = (void*) frame_table_get_id (kaddr);
//   struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
// }

// void load_page_from_file_system (struct supplementary_page_table_entry* spte)
// {
//   /* Kernel address. */
//   void* kaddr = palloc_get_page (PAL_USER);
//   struct frame_table_entry fte;

//   if (kaddr)
//   {
//     spte->fid = (void*) frame_table_get_id (kaddr);
//     // Obtain a frame to store the page.
//     fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//     // ASSERT (&fte.spte == NULL);
//     // ASSERT (&fte.spte != NULL);
//     ASSERT (&fte.spte->page_lock == NULL);
//   }
//   else
//   {
//     printf ("eviction need!\n");
//     frame_table->frame_table_entry[(uint32_t) spte->fid] = frame_table_evict ();
//     fte = frame_table->frame_table_entry[(uint32_t) spte->fid];

//     // ASSERT (&fte.spte->page_lock != NULL);
//   }

//   /* All zero page. */
//   if (spte->page_zero_bytes == PGSIZE)
//   {
//     memset (fte.frame, 0, PGSIZE);
//     if (install_page_copy (spte->pid, fte.frame, spte->writeable))
//     {
//     }
//     return ;
//   }

  
//   // Fetch data into the frame.
//   uint32_t read_bytes = file_read_at (spte->file, fte.frame, spte->page_read_bytes, spte->file_ofs);

//   if (spte->page_read_bytes != PGSIZE)
//   {
//     memset (fte.frame + spte->page_read_bytes, 0, PGSIZE - spte->page_read_bytes);
//   }
//   if (read_bytes != spte->page_read_bytes)
//   {
//     palloc_free_page (kaddr);
//     sysexit (-1);
//   }

//   if (install_page_copy (spte->pid, fte.frame, spte->writeable))
//   {
//     printf ("successfully install\n");
//     fte.spte = spte;
//     ASSERT (&fte.spte != NULL);
//     ASSERT (&fte.spte->page_lock != NULL);
//   }
//   else
//   {
//     fte.spte = NULL;
//     palloc_free_page (kaddr);
//     sysexit (-1);
//     return ;
//   }
// }



/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a page from swap to physical memory
 --------------------------------------------------------------------
 */
void load_swap_page (struct supplementary_page_table_entry* spte) {
    assert_spte_consistency(spte);
    struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    swap_block_read(fte->frame, (uint32_t) spte->sid);
    // swap_block_read(spte->frame->frame, (uint32_t) spte->sid);
    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a page from swap to physical memory
 NOTE: this function is called by frame_handler_palloc, which
    locks the frame, so we do not need to pin here.
 --------------------------------------------------------------------
 */
void load_file_page (struct supplementary_page_table_entry* spte) {
    assert_spte_consistency(spte);
    // printf ("start load_file_page\n");

    struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    if (spte->page_zero_bytes == PGSIZE) {
        memset(fte->frame, 0, PGSIZE);
        // memset(spte->frame->frame, 0, PGSIZE);
        return;
    }
    lock_acquire (&file_system_lock);
    // printf ("start load_file_page\n");
    uint32_t bytes_read = file_read_at (spte->file, fte->frame, spte->page_read_bytes, spte->file_ofs);
    
    // uint32_t bytes_read = file_read_at (spte->file, spte->frame->frame, spte->page_read_bytes, spte->file_ofs);
    // printf ("end load_file_page\n");
    lock_release (&file_system_lock);
    if (bytes_read != spte->page_read_bytes) {
        // thread_current()->vital_info->exit_status = -1;
        // if (thread_current()->is_running_user_program) {
        //     printf("%s: exit(%d)\n", thread_name(), -1);
        // }
        // thread_exit();
        sysexit(-1);
    }
    if (spte->page_read_bytes != PGSIZE) {
        // memset (spte->frame->frame + spte->page_read_bytes, 0, spte->page_zero_bytes);
        memset (fte->frame + spte->page_read_bytes, 0, spte->page_zero_bytes);
    }

    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: loads a memory mapped page into memory
 --------------------------------------------------------------------
 */
void load_mmaped_page (struct supplementary_page_table_entry* spte) {
    /* No difference between loading mmapped pages and file pages in */
    return load_file_page (spte);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we only add the mapping of virtual address to frame 
    after the load has completed. 
 NOTE: This function assumes that the caller has aquired the 
    page lock, thus ensuring that eviction and loading
    cannot be done at the same time. 
 --------------------------------------------------------------------
 */
bool load_page_into_physical_memory(struct supplementary_page_table_entry* spte, bool is_fresh_stack_page) {
    assert_spte_consistency(spte);
    // ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
    if (is_fresh_stack_page == false) {
        switch (spte->location) {
            case SWAP_BLOCK:
                load_swap_page (spte);
                break;
            case FILE_SYSTEM:
                load_file_page (spte);
                break;
            case MAP_MEMORY:
                load_mmaped_page (spte);
                break;
            default:
                // thread_current()->vital_info->exit_status = -1;
                // if (thread_current()->is_running_user_program) {
                //     printf("%s: exit(%d)\n", thread_name(), -1);
                // }
                // thread_exit();
                sysexit(-1);
                break;
        }
    }
    // bool success = install_page_copy(spte->pid, spte->frame->frame, spte->writeable);
    struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    bool success = install_page_copy(spte->pid, fte->frame, spte->writeable);
    assert_spte_consistency(spte);
    return success;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION:In this function, we copy a page from a physcial
    frame to a swap slot
 --------------------------------------------------------------------
 */
void evict_page_from_swap_block (struct supplementary_page_table_entry* spte) {
    assert_spte_consistency(spte);
    struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    // uint32_t swap_index = swap_block_write(spte->frame->frame);
    uint32_t swap_index = swap_block_write(fte->frame);
    spte->sid = swap_index;
    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: moves a page containing file data to a swap slot
    if the page is dirty. Else, we do nothing.
 NOTE: Once the page is dirty once, as discussed in OH, we treat
    it as always dirty, which means it will forever more be a swap. 
 --------------------------------------------------------------------
 */
void evict_page_from_file_system (struct supplementary_page_table_entry* spte) {
    assert_spte_consistency(spte);
    lock_acquire (&spte->owner_thread->pagedir_lock);
    uint32_t* pagedir = spte->owner_thread->pagedir;
    struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    bool dirty = pagedir_is_dirty(pagedir, fte->spte->pid);
    // bool dirty = pagedir_is_dirty(pagedir, spte->frame->spte->pid);
    lock_release (&spte->owner_thread->pagedir_lock);
    if (dirty) {
        spte->location = SWAP_BLOCK;
        evict_page_from_swap_block (spte);
    }
    assert_spte_consistency(spte);
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: moves a mmapped page from physical memory to another
    location
 NOTE: Reset's the dirty value if the page is currently dirty so
    that subsequent checks will only write if dirty again. 
 --------------------------------------------------------------------
 */
void evict_page_from_map_memory (struct supplementary_page_table_entry* spte) {
    assert_spte_consistency(spte);

    uint32_t* pagedir = spte->owner_thread->pagedir;
    lock_acquire (&spte->owner_thread->pagedir_lock);
    void *page_id = spte->pid;
    bool dirty = pagedir_is_dirty(pagedir, page_id);
    if (dirty) {
        pagedir_set_dirty(pagedir, page_id, false);
        lock_release (&spte->owner_thread->pagedir_lock);
        lock_acquire (&file_system_lock);
        struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
        file_write_at (spte->file, fte->frame, spte->page_read_bytes,
                       spte->file_ofs);
        // file_write_at (spte->file, spte->frame->frame, spte->page_read_bytes,
        //                spte->file_ofs);
        lock_release (&file_system_lock);
    } else {
        lock_release (&spte->owner_thread->pagedir_lock);
    }

    assert_spte_consistency(spte);
}


/*
 --------------------------------------------------------------------
 DESCRIPTION: frees the resources aquired to mmap.
 --------------------------------------------------------------------
 */
// struct list_elem *
// munmap_state(struct mmap_state *mmap_s, struct thread *t)
// {
//     void *page;
//     lock_acquire (&file_system_lock);
//     int size = file_length(mmap_s->fp);
//     lock_release (&file_system_lock);
    
//     /* Write back dirty pages, and free all pages in use */
//     for (page = mmap_s->vaddr; page < mmap_s->vaddr + size; page += PGSIZE)
//     {
//         struct supplementary_page_table_entry *entry = find_spte(page, t);
//         ASSERT (entry != NULL);
//         lock_acquire (&entry->page_lock);
//         if (entry->in_physical_memory == true) {
//             if (lock_held_by_current_thread(&entry->frame->frame_lock) == false) {
//                 lock_acquire (&entry->frame->frame_lock);
//             }
//             clear_page_copy (entry->pid, entry->owner_thread);
//             evict_page_from_map_memory (entry);
//             entry->in_physical_memory = false;
//             palloc_free_page (entry->frame->frame);
//             lock_release (&entry->frame->frame_lock);
//             entry->frame->spte = NULL;
//             entry->frame = NULL;
//         }
//         lock_release (&entry->page_lock);
//         hash_delete(&t->spte_table, &entry->elem);
//         free(entry);
//     }

//     lock_acquire (&file_system_lock);
//     file_close(mmap_s->fp);
//     lock_release (&file_system_lock);
    
//     struct list_elem *next = list_remove(&mmap_s->elem);
//     free(mmap_s);
//     return next;
// }

/*
 --------------------------------------------------------------------
 IMPLIMENTATION:
 NOTE: need to break the prevous mapping from virtual address to
    physcial frame by calling page_dir_clear_page
 --------------------------------------------------------------------
 */
// bool evict_page_from_physical_memory(struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     clear_page_copy (spte->pid, spte->owner_thread);
//     switch (spte->location) {
//         case SWAP_BLOCK:
//             evict_page_from_swap_block (spte);
//             break;
//         case FILE_SYSTEM:
//             evict_page_from_file_system (spte);
//             break;
//         case MAP_MEMORY:
//             evict_page_from_map_memory (spte);
//             break;
//         default:
//             sysexit(-1);
//             // thread_current()->vital_info->exit_status = -1;
//             // if (thread_current()->is_running_user_program) {
//             //     printf("%s: exit(%d)\n", thread_name(), -1);
//             // }
//             // thread_exit();
//             break;
//     }
//     assert_spte_consistency(spte);
//     return true;
// }


struct supplementary_page_table_entry* supplementary_page_table_entry_find
(void* vaddr)
{
  struct thread* cur = thread_current ();
  void* pid = pg_round_down (vaddr);
  lock_acquire (&cur->supplementary_page_table_lock);
  struct list_elem *e;
  for (e = list_begin (&cur->supplementary_page_table); e != list_end (&cur->supplementary_page_table);
      e = list_next (e))
  {
    struct supplementary_page_table_entry* spte = list_entry (e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
    if (spte->pid == pid) {
        lock_release (&cur->supplementary_page_table_lock);
      return spte;
    }
  }
  lock_release (&cur->supplementary_page_table_lock);
  return NULL;
}

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES: declare a local spte on the stack to 
//     search against. 
//  NOTE: Returns null if no element could be found. 
//  --------------------------------------------------------------------
//  */
// struct supplementary_page_table_entry* find_spte(void* virtual_address, struct thread *t) {
//     void* spte_id = (void*)pg_round_down(virtual_address);
//     struct supplementary_page_table_entry dummy;
//     dummy.pid = spte_id;
    
//     struct hash* table = &t->spte_table;
//     struct hash_elem* match = hash_find(table, &dummy.elem);
//     if (match) {
//         return hash_entry(match, struct supplementary_page_table_entry, elem);
//     }
//     return NULL;
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: hashes based on the spte id which is the rounded
    down virtual address, ie the page number. 
 --------------------------------------------------------------------
 */
// static unsigned hash_func(const struct hash_elem* e, void* aux UNUSED) {
//     struct supplementary_page_table_entry* spte = hash_entry(e, struct supplementary_page_table_entry, elem);
//     return hash_int((uint32_t)spte->pid);
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: Compares the keys stored in elements a and b. 
    Returns true if a is less than b, false if a is greater 
    than or equal to b.
 --------------------------------------------------------------------
 */
// static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
//     struct supplementary_page_table_entry* A_spte = hash_entry(a, struct supplementary_page_table_entry, elem);
//     struct supplementary_page_table_entry* B_spte = hash_entry(b, struct supplementary_page_table_entry, elem);
//     if ((uint32_t)A_spte->pid < (uint32_t)B_spte->pid) return true;
//     return false;
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: frees all resources associated with a given spte
    entry. 
 NOTE: need to check if the page is currently in a frame. If it is
    we have to free those frame resources.
 --------------------------------------------------------------------
 */

void free_supplementary_page_table_entry (struct list_elem* e) {
    struct supplementary_page_table_entry* spte = list_entry(e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
    lock_acquire (&spte->page_lock);
    if (spte->in_physical_memory) {
        clear_page_copy (spte->pid, spte->owner_thread);
        frame_table_entry_free (spte);
        spte->in_physical_memory = false;
    }
    lock_release (&spte->page_lock);
    // free_spte(spte);
    free(spte);
}

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES: initializes the given hash table.
//  --------------------------------------------------------------------
//  */
// void init_spte_table(struct hash* thread_hash_table) {
//     bool success = hash_init(thread_hash_table, hash_func, less_func, NULL);
//     if (!success) {
//         // thread_current()->vital_info->exit_status = -1;
//         // if (thread_current()->is_running_user_program) {
//         //     printf("%s: exit(%d)\n", thread_name(), -1);
//         // }
//         // thread_exit();
//         sysexit(-1);
//     }
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  --------------------------------------------------------------------
//  */
void free_supplementary_page_table (struct list* supplementary_page_table) {

    while (!list_empty (supplementary_page_table)) 
    {
    struct list_elem *list_elem = list_pop_front (supplementary_page_table);
    // struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
    // destructor (hash_elem, h->aux);
    free_supplementary_page_table_entry (list_elem);
    }
    list_init (supplementary_page_table);
//     hash_destroy(thread_hash_table, free_hash_entry);

//       size_t i;

//   for (i = 0; i < h->bucket_cnt; i++) 
//     {
//       struct list *bucket = &h->buckets[i];

//       if (destructor != NULL) 
//         while (!list_empty (bucket)) 
//           {
//             struct list_elem *list_elem = list_pop_front (bucket);
//             struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
//             destructor (hash_elem, h->aux);
//           }

//       list_init (bucket); 
//     }    

//   h->elem_cnt = 0;

//         if (destructor != NULL)
//     hash_clear (h, destructor);
//   free (h->buckets);
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: Implimentation verifies that there's not already a 
    page at that virtual address, then map our page there. 
 --------------------------------------------------------------------
 */
bool install_page_copy(void *upage, void *kpage, bool writeable) {
    struct thread *t = thread_current ();
    
    lock_acquire (&t->pagedir_lock);
    bool result = (pagedir_get_page (t->pagedir, upage) == NULL
            && pagedir_set_page (t->pagedir, upage, kpage, writeable));
    lock_release (&t->pagedir_lock);
    return result;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: note, pagedir may be null of the thread
    is exiting and has set its pagedir to null. Thus, we 
    must check for this case.
 --------------------------------------------------------------------
 */
// void clear_page_copy (void* upage, struct thread* t) {
//     lock_acquire (&t->pagedir_lock);
//     if (t->pagedir != NULL) {
//         pagedir_clear_page (t->pagedir, upage);
//     }
//     lock_release (&t->pagedir_lock);
// }

void clear_page_copy (void* upage, struct thread* t) {
    lock_acquire (&t->pagedir_lock);
    if (t->pagedir != NULL) {
        pagedir_clear_page (t->pagedir, upage);
    }
    lock_release (&t->pagedir_lock);
}

// #define PUSHA_BYTE_DEPTH 32
// #define PUSH_BYTE_DEPTH 4
// #define MAX_STACK_SIZE_IN_BYTES 8392000
/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we need to check if the stack pointer is below the faulting 
    address.
 --------------------------------------------------------------------
 */
// bool is_valid_stack_access(void* esp, void* user_virtual_address) {
//     uint32_t stack_bottom_limit = (uint32_t)(PHYS_BASE - MAX_STACK_SIZE_IN_BYTES);
//     if ((uint32_t)user_virtual_address < stack_bottom_limit) {
//         return false;
//     }
//     if ((uint32_t)user_virtual_address >= (uint32_t)PHYS_BASE) {
//         return false;
//     }
//     if ((uint32_t)user_virtual_address >= (uint32_t)esp) {
//         return true;
//     }
//     void* acceptable_depth_pushA = (void*)((char*)esp - PUSHA_BYTE_DEPTH);
//     if ((uint32_t)user_virtual_address == (uint32_t)acceptable_depth_pushA) {
//         return true;
//     }
//     void* acceptable_depth_push = (void*)((char*)esp - PUSH_BYTE_DEPTH);
//     if ((uint32_t)user_virtual_address == (uint32_t)acceptable_depth_push) {
//         return true;
//     }
//     return false;
// }

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: we take care of freeing the palloc'd page on error within 
    frame_handler_palloc
 NOTE: Create_spte_and_add aquires the page lock for us, to handle
    this race between creation and access to swap memory before 
    frame gets allocated.
 --------------------------------------------------------------------
 */
// bool grow_stack(void* page_id) {
//     struct supplementary_page_table_entry* spte = create_spte_and_add_to_table(SWAP_BLOCK, page_id, true, true, NULL, 0, 0, 0);
//     if (spte == NULL) {
//         return false;
//     }
//     bool outcome = frame_handler_palloc(true, spte, false, true);
//     return outcome;
// }




bool
stack_growth (void *fault_addr)
{
    // void* next_stack_page = (void*)pg_round_down(fault_addr);
    // return grow_stack(next_stack_page);

   // printf ("stack_growth start\n");
   void* spid = pg_round_down (fault_addr);
   bool writeable = true;

   size_t page_read_bytes = 0;
   size_t page_zero_bytes = 0;

   struct file *file = NULL;
   off_t file_ofs = 0;
   struct supplementary_page_table_entry* spte = supplementary_page_table_entry_create (
      spid,
      writeable,
      SWAP_BLOCK,
      page_read_bytes,
      page_zero_bytes,
      file,
      file_ofs,
      true
   );
   // printf ("stack_growth reach here\n");
   if (spte == NULL) {
    //   sysexit (-1);
      return false;
   }



   supplementary_page_table_entry_insert (spte);
//    lock_release (&spte->page_lock);

    // bool outcome = frame_handler_palloc(true, spte, false, true);
    // return outcome;

//    bool outcome = frame_handler_palloc(true, spte, false, true);
// return outcome;



    // lock_acquire (&spte->page_lock);
      /* Kernel address. */
      lock_acquire (&frame_table->frame_table_lock);
    //   void* kaddr = palloc_get_page (PAL_USER);
      void* kaddr = palloc_get_page (PAL_USER | PAL_ZERO);
      struct frame_table_entry* fte;
      
      if (kaddr)
      {
         spte->fid = (void*) frame_table_get_id (kaddr);
         // // Obtain a frame to store the page.
         // fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
         // fte = frame_table + get_frame_index(kaddr);
         fte = &frame_table->frame_table_entry[frame_table_get_id(kaddr)];
         lock_acquire (&fte->frame_lock);
         lock_release (&frame_table->frame_table_lock);
         // ASSERT (&fte.spte == NULL);
         // ASSERT (&fte.spte != NULL);
         // ASSERT (&fte.spte->page_lock == NULL);
      }
      else
      {
         // printf ("eviction need!\n");
         fte = frame_table_evict ();
         spte->fid = frame_table_find_id (fte);
         // frame_table->frame_table_entry[(uint32_t) spte->fid] = *fte;
         // frame_table->frame_table_entry[(uint32_t) spte->fid] = *(evict_frame());
         // // frame_table_evict ();
         // fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);

         // ASSERT (&fte.spte->page_lock != NULL);
      }

    //   spte->frame = fte;
bool success = false;
//    // frame_handler_palloc(true, spte, false, true);
//    void* kaddr = palloc_get_page (PAL_USER | PAL_ZERO);
//    struct frame_table_entry* fte;
//    if (kaddr)
//    {
//       spte->fid = (void*) frame_table_get_id (kaddr);
//       fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//       ASSERT (&fte.spte == NULL);
//       // ASSERT (&fte.spte->page_lock != NULL);
//    }
//    else
//    {
//     fte = evict_frame ();
//     //   printf ("stack growth eviction need!\n");
//     //   frame_table->frame_table_entry[(uint32_t) spte->fid] = frame_table_evict ();
//     //   fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//       // ASSERT (&fte.spte != NULL);
//       // ASSERT (&fte.spte->page_lock != NULL);
//    }
//    spte->frame = fte;
   memset (fte->frame, 0, PGSIZE);

// struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    //    success = install_page_copy(spte->pid, spte->frame->frame, spte->writeable);
    success = install_page_copy(spte->pid, fte->frame, spte->writeable);
      if (!success)
      {
        // spte->frame = NULL;
        spte->fid = NULL;
        fte->spte = NULL;
        spte->in_physical_memory = false;
        palloc_free_page (kaddr);
        lock_release (&fte->frame_lock);
        lock_release (&spte->page_lock);
        
        //  sysexit (-1);
         
        // return false;
      } else {

         fte->spte = spte;
         spte->in_physical_memory = true;

        lock_release (&fte->frame_lock);
        lock_release (&spte->page_lock);
        //  return true;
        
      }
      return success;


   // struct thread *cur = thread_current ();
   // bool success = false;
   // if (pagedir_get_page (cur->pagedir, spte->pid) == NULL)
   // {
   //    if (pagedir_set_page (cur->pagedir, spte->pid, fte.frame, spte->writeable))
   //    {
   //       success = true;
   //    }
   // }

//    if (install_page_copy (spte->pid, fte.frame, spte->writeable))
//    {

//       printf ("successfully install\n");
//       fte.spte = spte;
//       ASSERT (&fte.spte != NULL);
//       ASSERT (&fte.spte->page_lock != NULL);
//    }
//    else
//    {
//       fte.spte = NULL;
//       palloc_free_page (kaddr);
//       sysexit (-1);
//    }
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 NOTE: if the page is not curently in memory, this case is easy, as 
    we simply call frame_handler_palloc(false, spte, true), as the
    last true field will pin frame. 
 NOTE: if the page is in memory, synchronization becomes an issue.
    In this case we have to try to acquire the 
    lock for the page if it is in physical memory. If we aquire the 
    lock, and the page is still in the frame, we are good, else,
    we have to hunt down the new frame.
 --------------------------------------------------------------------
 */


void pin_frame (struct supplementary_page_table_entry* spte) {
     lock_acquire (&frame_table->frame_table_lock);
      void* kaddr = palloc_get_page (PAL_USER);
      struct frame_table_entry* fte;
      
      if (kaddr)
      {
         spte->fid = (void*) frame_table_get_id (kaddr);
         // // Obtain a frame to store the page.
         // fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
         // fte = frame_table + get_frame_index(kaddr);
         fte = &frame_table->frame_table_entry[frame_table_get_id(kaddr)];
         lock_acquire (&fte->frame_lock);
         lock_release (&frame_table->frame_table_lock);
         // ASSERT (&fte.spte == NULL);
         // ASSERT (&fte.spte != NULL);
         // ASSERT (&fte.spte->page_lock == NULL);
      }
      else
      {
         // printf ("eviction need!\n");
         fte = frame_table_evict ();
         spte->fid = frame_table_find_id (fte);
         // frame_table->frame_table_entry[(uint32_t) spte->fid] = *fte;
         // frame_table->frame_table_entry[(uint32_t) spte->fid] = *(evict_frame());
         // // frame_table_evict ();
         // fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);

         // ASSERT (&fte.spte->page_lock != NULL);
      }

    //   spte->frame = fte;

      if (spte->location == MAP_MEMORY)
      {
         // printf ("load_page_from_map_memory\n");
         // lock_acquire (&spte->page_lock);
         load_page_from_map_memory (spte, fte);
         // lock_release (&spte->page_lock);
      }
      else if (spte->location == SWAP_BLOCK)
      {
         // printf ("load_page_from_swap_block\n");
         // lock_acquire (&spte->page_lock);
         load_page_from_swap_block (spte, fte);
         // lock_release (&spte->page_lock);
      }
      else if (spte->location == FILE_SYSTEM)
      {
         // printf ("load_page_from_file_system\n");
         // lock_acquire (&spte->page_lock);
         load_page_from_file_system (spte, fte);
         // load_file_page (spte);
         // lock_release (&spte->page_lock);
      }
      else
      {
         sysexit (-1);
      }

      // switch (spte->location) {
      //       case SWAP_BLOCK:
      //           load_swap_page(spte);
      //           break;
      //       case FILE_SYSTEM:
      //           load_file_page(spte);
      //           break;
      //       case MAP_MEMORY:
      //           load_mmaped_page(spte);
      //           break;
      //       default:
      //           // thread_current()->vital_info->exit_status = -1;
      //           // if (thread_current()->is_running_user_program) {
      //           //     printf("%s: exit(%d)\n", thread_name(), -1);
      //           // }
      //           // thread_exit();
      //           sysexit(-1);
      //           break;
      //   }
    //   struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    //   bool success = install_page_copy(spte->pid, spte->frame->frame, spte->writeable);
    bool success = install_page_copy(spte->pid, fte->frame, spte->writeable);
      if (!success)
      {
        // spte->frame = NULL;
        spte->fid = NULL;

        fte->spte = NULL;
        spte->in_physical_memory = false;
        palloc_free_page(kaddr);
        lock_release (&fte->frame_lock);
        lock_release (&spte->page_lock);
        
         sysexit (-1);
         
         

         //   if (install_page_copy (spte->pid, fte.frame, spte->writeable))
//   {
//     return ;
//     // // printf ("successfully install\n");
//     // fte.spte = spte;
//     // spte->in_physical_memory = true;
//     // ASSERT (&fte.spte != NULL);
//     // ASSERT (&fte.spte->page_lock != NULL);
//   }
//   else
//   {
//     return ;
//     // fte.spte = NULL;
//     // palloc_free_page (kaddr);
//     // sysexit (-1);
//     // return ;
//   }

        
      } else {

         fte->spte = spte;
         spte->in_physical_memory = true;

    //   lock_release (&fte->frame_lock);
      lock_release (&spte->page_lock);
         return;
        
      }

    // lock_acquire (&frame_table->frame_table_lock);
    // void* physical_memory_addr = palloc_get_page (PAL_USER);
    
    // struct frame_table_entry* frame;
    // if (physical_memory_addr != NULL) {
    //     // frame = frame_table + get_frame_index(physical_memory_addr);
    //     frame = &frame_table->frame_table_entry[frame_table_get_id(physical_memory_addr)];
    //     lock_acquire (&frame->frame_lock);
    //     lock_release (&frame_table->frame_table_lock);
    //     ASSERT(frame->spte == NULL)
    // } else {
    //     frame = evict_frame(); //aquires frame lock and releases frame_table->frame_table_lock
    // }
    
    // spte->frame = frame;
    
    // bool success = load_page_into_physical_memory(spte, is_fresh_stack_page);
    
    // if (!success) {
    //     barrier();
    //     spte->frame = NULL;
    //     frame->spte = NULL;
    //     spte->in_physical_memory = false;
    //     palloc_free_page(physical_memory_addr);
    //     lock_release (&frame->frame_lock);
    //     lock_release (&spte->page_lock);
    //     return false;
    // } else {
    //     // printf ("successfully install\n");
    //     frame->spte = spte;
    //     spte->in_physical_memory = true;
    // }
  
    // lock_release (&spte->page_lock);
    // return success;
}




void pin_page (void* virtual_address) {
    struct supplementary_page_table_entry* spte = supplementary_page_table_entry_find (virtual_address);
    // struct supplementary_page_table_entry* spte = find_spte(virtual_address, thread_current());
    lock_acquire (&spte->page_lock);
    if (spte->in_physical_memory != true)
    {
        pin_frame (spte);
        // frame_handler_palloc(false, spte, true, false);
    } else
    {
        lock_release (&spte->page_lock);
        lock_acquire (&spte->page_lock);
        // bool success = ;
        struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
        // if (frame_lock_try_aquire (spte->frame, spte))
         if (frame_lock_try_aquire (fte, spte))
        {
            lock_release (&spte->page_lock);
            return;
        }
        else
        {
            if (spte->in_physical_memory != true)
            {
                pin_frame (spte);
                // frame_handler_palloc(false, spte, true, false);
                return;
            } else {
                if (spte->in_physical_memory == true) {
                    frame_table_entry_free (spte);
                    pin_frame (spte);
                    // frame_handler_palloc(false, spte, true, false);
                    return;
                }
            }
        }
    }
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
void unpin_page (void* virtual_address)
{
    struct supplementary_page_table_entry* spte = supplementary_page_table_entry_find (virtual_address);
    // struct supplementary_page_table_entry* spte = find_spte(virtual_address, thread_current());
    lock_acquire (&spte->page_lock);
    // ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
    // lock_release (&spte->frame->frame_lock);
    struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
    lock_release (&fte->frame_lock);
    lock_release (&spte->page_lock);
}




// #include <inttypes.h>
// #include <round.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <stdbool.h>
// #include "threads/synch.h"
// #include "threads/thread.h"
// #include "threads/pte.h"
// #include "threads/palloc.h"
// #include "vm/page.h"
// #include "vm/frame.h"
// #include "vm/swap.h"
// #include "filesys/file.h"
// #include "userprog/syscall.h"
// #include "userprog/pagedir.h"



// struct supplementary_page_table_entry* supplementary_page_table_entry_find
// (void* vaddr)
// {
//   struct thread* cur = thread_current ();
//   void* pid = pg_round_down (vaddr);
//   struct list_elem *e;
//   for (e = list_begin (&cur->supplementary_page_table); e != list_end (&cur->supplementary_page_table);
//       e = list_next (e))
//   {
//     struct supplementary_page_table_entry* spte = list_entry (e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
//     if (spte->pid == pid) {
//       return spte;
//     }
//   }
//   return NULL;
// }




// /* Have some problem to include process.h.
//    So just copy install_page here. */
// /* If static, can't be called in other classes. (Why?) */
// bool
// install_page_copy (void *upage, void *kpage, bool writeable)
// {
//   struct thread *t = thread_current ();

//   /* Verify that there's not already a page at that virtual
//      address, then map our page there. */
//   return (pagedir_get_page (t->pagedir, upage) == NULL
//           && pagedir_set_page (t->pagedir, upage, kpage, writeable));
// } 

// void
// evict_page_swap (struct supplementary_page_table_entry* spte)
// {
//   struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//   uint32_t sid = swap_block_write (fte.frame);
//   spte->sid = &sid;
// }

// void 
// evict_page_file (struct supplementary_page_table_entry* spte)
// {
//   ASSERT (&spte->page_lock != NULL);
//   lock_acquire (&spte->page_lock);
//   uint32_t* pagedir = spte->pagedir;
//   // struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  
//   if (pagedir_is_dirty (spte->pagedir, spte->fid))
//   {
//     spte->location = SWAP_BLOCK;
//     evict_page_swap (spte);
//   }
//   lock_release (&spte->page_lock);
// }

// void
// evict_page_map(struct supplementary_page_table_entry* spte)
// {
//   ASSERT (&spte->page_lock != NULL);
//   lock_acquire (&spte->page_lock);
//   if (pagedir_is_dirty (spte->pagedir, spte->pid))
//   {
//     pagedir_set_dirty (spte->pagedir, spte->pid, false);
//     struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//     ASSERT (&fte.frame_lock != NULL);
//     lock_acquire (&fte.frame_lock);
//     file_write_at (spte->file, fte.frame, spte->page_read_bytes, spte->file_ofs);
//     lock_release (&fte.frame_lock);
//   }
//   lock_release (&spte->page_lock);
// }




// #include <stdbool.h>
// #include "threads/thread.h"
// #include "threads/malloc.h"  
// #include "threads/palloc.h"
// #include "threads/vaddr.h"
// #include "userprog/pagedir.h"
// #include "vm/page.h"
// #include "vm/swap.h"
// #include "vm/frame.h"
// #include <stdio.h>
// #include <stddef.h>
// #include "filesys/file.h"
// #include <string.h>
// #include "userprog/syscall.h"

// static void load_swap_page (struct supplementary_page_table_entry* spte);
// static void load_file_page (struct supplementary_page_table_entry* spte);
// static void load_mmaped_page (struct supplementary_page_table_entry* spte);
// static void evict_page_from_swap_block (struct supplementary_page_table_entry* spte);
// static void evict_page_from_file_system (struct supplementary_page_table_entry* spte);
// static void evict_page_from_map_memory (struct supplementary_page_table_entry* spte);

// static void free_hash_entry(struct hash_elem* e, void* aux UNUSED);


// static void
// assert_spte_consistency(struct supplementary_page_table_entry* spte)
// {
//     ASSERT (spte != NULL);
//     ASSERT (spte->location == SWAP_BLOCK ||
//             spte->page_read_bytes + spte->page_zero_bytes == PGSIZE);

//     ASSERT (spte->frame == NULL ||
//             spte->frame->frame >= PHYS_BASE);

// }


// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  NOTE: we aquire the lock here, to handle the case where we 
//     allocate a swap page, in which case we pass is_loaded
//     value of true. 
//  NOTE: On failure of malloc, we return null. 
//  NOTE: On failure to add the spte to the spte table, 
//     we exit the thread. 
//  --------------------------------------------------------------------
//  */
// struct supplementary_page_table_entry* create_spte_and_add_to_table(page_location location, void* page_id, bool is_writeable, bool is_loaded, struct file* file_ptr, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {

//     struct supplementary_page_table_entry* spte = malloc(sizeof(struct supplementary_page_table_entry));
//     if (spte == NULL) {
//         return NULL;
//     }
//     spte->location = location;
//     spte->owner_thread = thread_current();
//     spte->pid = page_id;
//     spte->writeable = is_writeable;
//     spte->in_physical_memory = is_loaded;
//     spte->frame = NULL;
//     spte->file = file_ptr;
//     spte->file_ofs = offset;
//     spte->page_read_bytes = read_bytes;
//     spte->page_zero_bytes = zero_bytes;
//     spte->sid = NULL; 
//     lock_init(&spte->page_lock);
//     // lock_acquire (&spte->page_lock);
//     // struct hash* target_table = &thread_current()->spte_table;
//     // struct supplementary_page_table_entry* outcome = hash_entry(hash_insert(target_table, &spte->elem), struct supplementary_page_table_entry, elem);
//     // if (outcome != NULL) {
//     //     // thread_current()->vital_info->exit_status = -1;
//     //     // if (thread_current()->is_running_user_program) {
//     //     //     printf("%s: exit(%d)\n", thread_name(), -1);
//     //     // }
//     //     // thread_exit();
//     //     sysexit(-1);
//     // }
//     supplementary_page_table_entry_insert (spte);

//     // assert_spte_consistency(spte);
//     return spte;
// }


// struct supplementary_page_table_entry* supplementary_page_table_entry_create
// (void* pid,
// bool writeable,
// page_location location,
// size_t page_read_bytes,
// size_t page_zero_bytes,
// struct file* file,
// off_t file_ofs)
// {
//   struct supplementary_page_table_entry* spte = malloc (sizeof(struct supplementary_page_table_entry));
//   lock_init (&spte->page_lock);
  
//   spte->pid = pid;
//   spte->sid = NULL; 
//   spte->fid = NULL; 

//   spte->writeable = writeable;
//   spte->location = location;

//   spte->page_read_bytes = page_read_bytes;
//   spte->page_zero_bytes = page_zero_bytes;

//   spte->file = file;
//   spte->file_ofs = file_ofs;

//   spte->pagedir = thread_current()->pagedir;

//   return spte;
// }

// // void supplementary_page_table_entry_insert (struct supplementary_page_table_entry* spte)
// // {
// //   ASSERT (&spte->page_lock != NULL);
// //   lock_acquire (&spte->page_lock);
// //   list_push_back (&thread_current ()->supplementary_page_table, &spte->supplementary_page_table_entry_elem);
// //   lock_release (&spte->page_lock);
// // }




// // void supplementary_page_table_entry_insert (struct supplementary_page_table_entry* spte)
// // {
// //   ASSERT (&spte->page_lock != NULL);
// //   lock_acquire (&spte->page_lock);
// //   struct thread* cur = thread_current ();
// //   lock_acquire (&cur->supplementary_page_table_lock);
// //   list_push_back (&thread_current ()->supplementary_page_table, &spte->supplementary_page_table_entry_elem);
// //   lock_release (&cur->supplementary_page_table_lock);
// // //   lock_release (&spte->page_lock);
// // }

// // /*
// //  --------------------------------------------------------------------
// //  IMPLIMENTATION NOTES:
// //  --------------------------------------------------------------------
// //  */
// // void free_spte(struct supplementary_page_table_entry* spte) {
// //     assert_spte_consistency(spte);
// //     free(spte);
// // }





// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: loads a page from swap to physical memory
//  --------------------------------------------------------------------
//  */
// static void load_swap_page (struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     swap_block_read(spte->frame->frame, (uint32_t) spte->sid);
//     assert_spte_consistency(spte);
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: loads a page from swap to physical memory
//  NOTE: this function is called by frame_handler_palloc, which
//     locks the frame, so we do not need to pin here.
//  --------------------------------------------------------------------
//  */
// static void load_file_page (struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     // printf ("start load_file_page\n");
//     if (spte->page_zero_bytes == PGSIZE) {
//         memset(spte->frame->frame, 0, PGSIZE);
//         return;
//     }
//     lock_acquire (&file_system_lock);
//     // printf ("start load_file_page\n");
//     uint32_t bytes_read = file_read_at (spte->file, spte->frame->frame, spte->page_read_bytes, spte->file_ofs);
//     // printf ("end load_file_page\n");
//     lock_release (&file_system_lock);
//     if (bytes_read != spte->page_read_bytes) {
//         // thread_current()->vital_info->exit_status = -1;
//         // if (thread_current()->is_running_user_program) {
//         //     printf("%s: exit(%d)\n", thread_name(), -1);
//         // }
//         // thread_exit();
//         sysexit(-1);
//     }
//     if (spte->page_read_bytes != PGSIZE) {
//         memset (spte->frame->frame + spte->page_read_bytes, 0, spte->page_zero_bytes);
//     }

//     assert_spte_consistency(spte);
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: loads a memory mapped page into memory
//  --------------------------------------------------------------------
//  */
// static void load_mmaped_page (struct supplementary_page_table_entry* spte) {
//     /* No difference between loading mmapped pages and file pages in */
//     return load_file_page (spte);
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  NOTE: we only add the mapping of virtual address to frame 
//     after the load has completed. 
//  NOTE: This function assumes that the caller has aquired the 
//     page lock, thus ensuring that eviction and loading
//     cannot be done at the same time. 
//  --------------------------------------------------------------------
//  */
// bool load_page_into_physical_memory(struct supplementary_page_table_entry* spte, bool is_fresh_stack_page) {
//     assert_spte_consistency(spte);
//     ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
//     if (is_fresh_stack_page == false) {
//         switch (spte->location) {
//             case SWAP_BLOCK:
//                 load_swap_page (spte);
//                 break;
//             case FILE_SYSTEM:
//                 load_file_page (spte);
//                 break;
//             case MAP_MEMORY:
//                 load_mmaped_page (spte);
//                 break;
//             default:
//                 // thread_current()->vital_info->exit_status = -1;
//                 // if (thread_current()->is_running_user_program) {
//                 //     printf("%s: exit(%d)\n", thread_name(), -1);
//                 // }
//                 // thread_exit();
//                 sysexit(-1);
//                 break;
//         }
//     }
//     bool success = install_page_copy(spte->pid, spte->frame->frame, spte->writeable);
//     assert_spte_consistency(spte);
//     return success;
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION:In this function, we copy a page from a physcial
//     frame to a swap slot
//  --------------------------------------------------------------------
//  */
// static void evict_page_from_swap_block (struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     uint32_t swap_index = swap_block_write(spte->frame->frame);
//     spte->sid = swap_index;
//     assert_spte_consistency(spte);
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: moves a page containing file data to a swap slot
//     if the page is dirty. Else, we do nothing.
//  NOTE: Once the page is dirty once, as discussed in OH, we treat
//     it as always dirty, which means it will forever more be a swap. 
//  --------------------------------------------------------------------
//  */
// static void evict_page_from_file_system (struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     lock_acquire (&spte->owner_thread->pagedir_lock);
//     uint32_t* pagedir = spte->owner_thread->pagedir;
//     bool dirty = pagedir_is_dirty(pagedir, spte->frame->spte->pid);
//     lock_release (&spte->owner_thread->pagedir_lock);
//     if (dirty) {
//         spte->location = SWAP_BLOCK;
//         evict_page_from_swap_block (spte);
//     }
//     assert_spte_consistency(spte);
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: moves a mmapped page from physical memory to another
//     location
//  NOTE: Reset's the dirty value if the page is currently dirty so
//     that subsequent checks will only write if dirty again. 
//  --------------------------------------------------------------------
//  */
// static void evict_page_from_map_memory (struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);

//     uint32_t* pagedir = spte->owner_thread->pagedir;
//     lock_acquire (&spte->owner_thread->pagedir_lock);
//     void *page_id = spte->pid;
//     bool dirty = pagedir_is_dirty(pagedir, page_id);
//     if (dirty) {
//         pagedir_set_dirty(pagedir, page_id, false);
//         lock_release (&spte->owner_thread->pagedir_lock);
//         lock_acquire (&file_system_lock);
//         file_write_at (spte->file, spte->frame->frame, spte->page_read_bytes,
//                        spte->file_ofs);
//         lock_release (&file_system_lock);
//     } else {
//         lock_release (&spte->owner_thread->pagedir_lock);
//     }

//     assert_spte_consistency(spte);
// }


// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: frees the resources aquired to mmap.
//  --------------------------------------------------------------------
//  */
// // struct list_elem *
// // munmap_state(struct mmap_state *mmap_s, struct thread *t)
// // {
// //     void *page;
// //     lock_acquire (&file_system_lock);
// //     int size = file_length(mmap_s->fp);
// //     lock_release (&file_system_lock);
    
// //     /* Write back dirty pages, and free all pages in use */
// //     for (page = mmap_s->vaddr; page < mmap_s->vaddr + size; page += PGSIZE)
// //     {
// //         struct supplementary_page_table_entry *entry = find_spte(page, t);
// //         ASSERT (entry != NULL);
// //         lock_acquire (&entry->page_lock);
// //         if (entry->in_physical_memory == true) {
// //             if (lock_held_by_current_thread(&entry->frame->frame_lock) == false) {
// //                 lock_acquire (&entry->frame->frame_lock);
// //             }
// //             clear_page_copy (entry->pid, entry->owner_thread);
// //             evict_page_from_map_memory (entry);
// //             entry->in_physical_memory = false;
// //             palloc_free_page (entry->frame->frame);
// //             lock_release (&entry->frame->frame_lock);
// //             entry->frame->spte = NULL;
// //             entry->frame = NULL;
// //         }
// //         lock_release (&entry->page_lock);
// //         hash_delete(&t->spte_table, &entry->elem);
// //         free(entry);
// //     }

// //     lock_acquire (&file_system_lock);
// //     file_close(mmap_s->fp);
// //     lock_release (&file_system_lock);
    
// //     struct list_elem *next = list_remove(&mmap_s->elem);
// //     free(mmap_s);
// //     return next;
// // }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION:
//  NOTE: need to break the prevous mapping from virtual address to
//     physcial frame by calling page_dir_clear_page
//  --------------------------------------------------------------------
//  */
// bool evict_page_from_physical_memory(struct supplementary_page_table_entry* spte) {
//     assert_spte_consistency(spte);
//     clear_page_copy (spte->pid, spte->owner_thread);
//     switch (spte->location) {
//         case SWAP_BLOCK:
//             evict_page_from_swap_block (spte);
//             break;
//         case FILE_SYSTEM:
//             evict_page_from_file_system (spte);
//             break;
//         case MAP_MEMORY:
//             evict_page_from_map_memory (spte);
//             break;
//         default:
//             sysexit(-1);
//             // thread_current()->vital_info->exit_status = -1;
//             // if (thread_current()->is_running_user_program) {
//             //     printf("%s: exit(%d)\n", thread_name(), -1);
//             // }
//             // thread_exit();
//             break;
//     }
//     assert_spte_consistency(spte);
//     return true;
// }


// struct supplementary_page_table_entry* supplementary_page_table_entry_find
// (void* vaddr)
// {
//   struct thread* cur = thread_current ();
//   void* pid = pg_round_down (vaddr);
//   lock_acquire (&cur->supplementary_page_table_lock);
//   struct list_elem *e;
//   for (e = list_begin (&cur->supplementary_page_table); e != list_end (&cur->supplementary_page_table);
//       e = list_next (e))
//   {
//     struct supplementary_page_table_entry* spte = list_entry (e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
//     if (spte->pid == pid) {
//         lock_release (&cur->supplementary_page_table_lock);
//       return spte;
//     }
//   }
//   lock_release (&cur->supplementary_page_table_lock);
//   return NULL;
// }

// // /*
// //  --------------------------------------------------------------------
// //  IMPLIMENTATION NOTES: declare a local spte on the stack to 
// //     search against. 
// //  NOTE: Returns null if no element could be found. 
// //  --------------------------------------------------------------------
// //  */
// // struct supplementary_page_table_entry* find_spte(void* virtual_address, struct thread *t) {
// //     void* spte_id = (void*)pg_round_down(virtual_address);
// //     struct supplementary_page_table_entry dummy;
// //     dummy.pid = spte_id;
    
// //     struct hash* table = &t->spte_table;
// //     struct hash_elem* match = hash_find(table, &dummy.elem);
// //     if (match) {
// //         return hash_entry(match, struct supplementary_page_table_entry, elem);
// //     }
// //     return NULL;
// // }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: hashes based on the spte id which is the rounded
//     down virtual address, ie the page number. 
//  --------------------------------------------------------------------
//  */
// static unsigned hash_func(const struct hash_elem* e, void* aux UNUSED) {
//     struct supplementary_page_table_entry* spte = hash_entry(e, struct supplementary_page_table_entry, elem);
//     return hash_int((uint32_t)spte->pid);
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: Compares the keys stored in elements a and b. 
//     Returns true if a is less than b, false if a is greater 
//     than or equal to b.
//  --------------------------------------------------------------------
//  */
// static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
//     struct supplementary_page_table_entry* A_spte = hash_entry(a, struct supplementary_page_table_entry, elem);
//     struct supplementary_page_table_entry* B_spte = hash_entry(b, struct supplementary_page_table_entry, elem);
//     if ((uint32_t)A_spte->pid < (uint32_t)B_spte->pid) return true;
//     return false;
// }

// /*
//  --------------------------------------------------------------------
//  DESCRIPTION: frees all resources associated with a given spte
//     entry. 
//  NOTE: need to check if the page is currently in a frame. If it is
//     we have to free those frame resources.
//  --------------------------------------------------------------------
//  */

// static void free_supplementary_page_table_entry(struct list_elem* e) {
//     struct supplementary_page_table_entry* spte = list_entry(e, struct supplementary_page_table_entry, supplementary_page_table_entry_elem);
//     lock_acquire (&spte->page_lock);
//     if (spte->in_physical_memory) {
//         clear_page_copy (spte->pid, spte->owner_thread);
//         frame_handler_palloc_free(spte);
//         spte->in_physical_memory = false;
//     }
//     lock_release (&spte->page_lock);
//     // free_spte(spte);
//     free(spte);
// }

// // /*
// //  --------------------------------------------------------------------
// //  IMPLIMENTATION NOTES: initializes the given hash table.
// //  --------------------------------------------------------------------
// //  */
// // void init_spte_table(struct hash* thread_hash_table) {
// //     bool success = hash_init(thread_hash_table, hash_func, less_func, NULL);
// //     if (!success) {
// //         // thread_current()->vital_info->exit_status = -1;
// //         // if (thread_current()->is_running_user_program) {
// //         //     printf("%s: exit(%d)\n", thread_name(), -1);
// //         // }
// //         // thread_exit();
// //         sysexit(-1);
// //     }
// // }

// // /*
// //  --------------------------------------------------------------------
// //  IMPLIMENTATION NOTES:
// //  --------------------------------------------------------------------
// //  */
// void free_supplementary_page_table(struct list* supplementary_page_table) {

//     while (!list_empty (supplementary_page_table)) 
//     {
//     struct list_elem *list_elem = list_pop_front (supplementary_page_table);
//     // struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
//     // destructor (hash_elem, h->aux);
//     free_supplementary_page_table_entry (list_elem);
//     }
//     list_init (supplementary_page_table);
// //     hash_destroy(thread_hash_table, free_hash_entry);

// //       size_t i;

// //   for (i = 0; i < h->bucket_cnt; i++) 
// //     {
// //       struct list *bucket = &h->buckets[i];

// //       if (destructor != NULL) 
// //         while (!list_empty (bucket)) 
// //           {
// //             struct list_elem *list_elem = list_pop_front (bucket);
// //             struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
// //             destructor (hash_elem, h->aux);
// //           }

// //       list_init (bucket); 
// //     }    

// //   h->elem_cnt = 0;

// //         if (destructor != NULL)
// //     hash_clear (h, destructor);
// //   free (h->buckets);
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  NOTE: Implimentation verifies that there's not already a 
//     page at that virtual address, then map our page there. 
//  --------------------------------------------------------------------
//  */
// bool install_page_copy(void *upage, void *kpage, bool writeable) {
//     struct thread *t = thread_current ();
    
//     lock_acquire (&t->pagedir_lock);
//     bool result = (pagedir_get_page (t->pagedir, upage) == NULL
//             && pagedir_set_page (t->pagedir, upage, kpage, writeable));
//     lock_release (&t->pagedir_lock);
//     return result;
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES: note, pagedir may be null of the thread
//     is exiting and has set its pagedir to null. Thus, we 
//     must check for this case.
//  --------------------------------------------------------------------
//  */
// void clear_page_copy (void* upage, struct thread* t) {
//     lock_acquire (&t->pagedir_lock);
//     if (t->pagedir != NULL) {
//         pagedir_clear_page (t->pagedir, upage);
//     }
//     lock_release (&t->pagedir_lock);
// }

// #define PUSHA_BYTE_DEPTH 32
// #define PUSH_BYTE_DEPTH 4
// #define MAX_STACK_SIZE_IN_BYTES 8392000
// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  NOTE: we need to check if the stack pointer is below the faulting 
//     address.
//  --------------------------------------------------------------------
//  */
// bool is_valid_stack_access(void* esp, void* user_virtual_address) {
//     uint32_t stack_bottom_limit = (uint32_t)(PHYS_BASE - MAX_STACK_SIZE_IN_BYTES);
//     if ((uint32_t)user_virtual_address < stack_bottom_limit) {
//         return false;
//     }
//     if ((uint32_t)user_virtual_address >= (uint32_t)PHYS_BASE) {
//         return false;
//     }
//     if ((uint32_t)user_virtual_address >= (uint32_t)esp) {
//         return true;
//     }
//     void* acceptable_depth_pushA = (void*)((char*)esp - PUSHA_BYTE_DEPTH);
//     if ((uint32_t)user_virtual_address == (uint32_t)acceptable_depth_pushA) {
//         return true;
//     }
//     void* acceptable_depth_push = (void*)((char*)esp - PUSH_BYTE_DEPTH);
//     if ((uint32_t)user_virtual_address == (uint32_t)acceptable_depth_push) {
//         return true;
//     }
//     return false;
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  NOTE: we take care of freeing the palloc'd page on error within 
//     frame_handler_palloc
//  NOTE: Create_spte_and_add aquires the page lock for us, to handle
//     this race between creation and access to swap memory before 
//     frame gets allocated.
//  --------------------------------------------------------------------
//  */
// bool grow_stack(void* page_id) {
//     struct supplementary_page_table_entry* spte = create_spte_and_add_to_table(SWAP_BLOCK, page_id, true, true, NULL, 0, 0, 0);
//     if (spte == NULL) {
//         return false;
//     }
//     bool outcome = frame_handler_palloc(true, spte, false, true);
//     return outcome;
// }




// void
// stack_growth (void *fault_addr)
// {
//    // printf ("stack_growth start\n");
//    void* spid = pg_round_down (fault_addr);
//    bool writeable = true;

//    size_t page_read_bytes = 0;
//    size_t page_zero_bytes = 0;

//    struct file *file = NULL;
//    off_t file_ofs = 0;
//    struct supplementary_page_table_entry* spte = supplementary_page_table_entry_create (
//       spid,
//       writeable,
//       SWAP_BLOCK,
//       page_read_bytes,
//       page_zero_bytes,
//       file,
//       file_ofs
//    );
//    // printf ("stack_growth reach here\n");
//    if (spte == NULL) {
//       sysexit (-1);
//       return ;
//    }
//    supplementary_page_table_entry_insert (spte);

//    // frame_handler_palloc(true, spte, false, true);
//    void* kaddr = palloc_get_page (PAL_USER | PAL_ZERO);
//    struct frame_table_entry fte;
//    if (kaddr)
//    {
//       spte->fid = (void*) frame_table_get_id (kaddr);
//       fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//       ASSERT (&fte.spte == NULL);
//       // ASSERT (&fte.spte->page_lock != NULL);
//    }
//    else
//    {
//     //   printf ("stack growth eviction need!\n");
//       frame_table->frame_table_entry[(uint32_t) spte->fid] = *(evict_frame());
//     //   frame_table_evict ();
//       fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
//       // ASSERT (&fte.spte != NULL);
//       // ASSERT (&fte.spte->page_lock != NULL);
//    }
//    memset (fte.frame, 0, PGSIZE);

//    // struct thread *cur = thread_current ();
//    // bool success = false;
//    // if (pagedir_get_page (cur->pagedir, spte->pid) == NULL)
//    // {
//    //    if (pagedir_set_page (cur->pagedir, spte->pid, fte.frame, spte->writeable))
//    //    {
//    //       success = true;
//    //    }
//    // }

//    if (install_page_copy (spte->pid, fte.frame, spte->writeable))
//    {

//     //   printf ("successfully install\n");
//       fte.spte = spte;
//       ASSERT (&fte.spte != NULL);
//       ASSERT (&fte.spte->page_lock != NULL);
//    }
//    else
//    {
//       fte.spte = NULL;
//       palloc_free_page (kaddr);
//       sysexit (-1);
//    }
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  NOTE: if the page is not curently in memory, this case is easy, as 
//     we simply call frame_handler_palloc(false, spte, true), as the
//     last true field will pin frame. 
//  NOTE: if the page is in memory, synchronization becomes an issue.
//     In this case we have to try to acquire the 
//     lock for the page if it is in physical memory. If we aquire the 
//     lock, and the page is still in the frame, we are good, else,
//     we have to hunt down the new frame.
//  --------------------------------------------------------------------
//  */
// void pin_page (void* virtual_address) {
//     struct supplementary_page_table_entry* spte = supplementary_page_table_entry_find (virtual_address);
//     // struct supplementary_page_table_entry* spte = find_spte(virtual_address, thread_current());
//     lock_acquire (&spte->page_lock);
//     if (spte->in_physical_memory != true) {
//         frame_handler_palloc(false, spte, true, false);
//     } else {
//         lock_release (&spte->page_lock);
//         lock_acquire (&spte->page_lock);
//         bool success = aquire_frame_lock(spte->frame, spte);
//         if (success) {
//             lock_release (&spte->page_lock);
//             return;
//         } else {
//             if (spte->in_physical_memory != true) {
//                 frame_handler_palloc(false, spte, true, false);
//                 return;
//             } else {
//                 if (spte->in_physical_memory == true) {
//                     frame_handler_palloc_free(spte);
//                     frame_handler_palloc(false, spte, true, false);
//                     return;
//                 }
//             }
//         }
//     }
// }

// /*
//  --------------------------------------------------------------------
//  IMPLIMENTATION NOTES:
//  --------------------------------------------------------------------
//  */
// void unpin_page (void* virtual_address) {
//     struct supplementary_page_table_entry* spte = supplementary_page_table_entry_find (virtual_address);
//     // struct supplementary_page_table_entry* spte = find_spte(virtual_address, thread_current());
//     lock_acquire (&spte->page_lock);
//     ASSERT(lock_held_by_current_thread(&spte->frame->frame_lock));
//     lock_release (&spte->frame->frame_lock);
//     lock_release (&spte->page_lock);
// }


