// // #include "vm/frame.h"
// // #include <debug.h>
// // #include <inttypes.h>
// // #include <round.h>
// // #include <stdio.h>
// // #include <stdlib.h>
// // #include <string.h>
// // #include <stdbool.h>
// // #include "threads/malloc.h"
// // #include "threads/synch.h"
// // #include "threads/palloc.h"
// // #include "vm/page.h"
// // #include "userprog/syscall.h"
// // #include "userprog/pagedir.h"

// // uint32_t clock = 0;

// // // bool
// // // frame_table_empty (struct frame_table_entry_table *f, size_t start, size_t cnt) 
// // // {
// // //   size_t i;
  
// // //   ASSERT (f != NULL);
// // //   ASSERT (start <= f->frame_table_entry_number);
// // //   ASSERT (start + cnt <= f->frame_table_entry_number);

// // //   for (i = 0; i < cnt; i++)
// // //     if (f->frame_table_entry[start + i].frame)
// // //       return true;
// // //   return false;
// // // }

// // // /* Finds the first index of CNT consecutive entries in f at 
// // //    or after START that are empty. (Entries for frame table).
// // //    returns the index of the first bit in the group.
// // //    If there is no such group, returns FRAME_TABLE_ERROR. 
// // //    Implement frame table scan following bitmap bitmap_scan. */
// // // size_t
// // // frame_table_scan (struct frame_table_entry_table *f, size_t start, size_t cnt)
// // // {
// // //   ASSERT (f != NULL);
// // //   ASSERT (start <= f->frame_table_entry_number);

// // //   if (cnt <= f->frame_table_entry_number) 
// // //     {
// // //       size_t last = f->frame_table_entry_number - cnt;
// // //       size_t i;
// // //       for (i = start; i <= last; i++)
// // //       {
// // //         if (f->frame_table_entry[i].frame)
// // //         {
// // //           /* Non empty, continue scan. */
// // //         }
// // //         else
// // //         {
// // //           if (frame_table_empty (f, i, cnt))
// // //           {
// // //             /* All empty, scan success. */
// // //             return i; 
// // //           }
// // //         }
// // //       }
// // //     }
// // //   return FRAME_TABLE_ERROR;
// // // }




// // void
// // frame_table_entry_free (struct supplementary_page_table_entry* spte) {
// //   // no frame at the beginning
// //   if (spte->fid == NULL)
// //   {
// //     return ;
// //   }
// //   struct frame_table_entry_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
// //   ASSERT (&fte.frame_lock != NULL);
// //   lock_acquire (&fte.frame_lock);
// //   // the frame is still occupied
// //   if (fte.spte->pid == spte->pid)
// //   {
// //     spte->fid = NULL;
// //     fte.spte = NULL;
// //     palloc_free_page(fte.frame);
// //   }
// //   lock_release (&fte.frame_lock);
// // }


// // void clock_algorithm (void)
// // {
// //   clock += 1;
// //   clock = clock % frame_table->frame_table_entry_number;
// // }

// // struct frame_table_entry_table_entry frame_table_evict (void)
// // {
// //   // lock_acquire (&frame_table->frame_table_lock);
// //   // ASSERT (lock_held_by_current_thread (&frame_table->frame_table_lock));
// //   clock_algorithm ();
// //   struct frame_table_entry_table_entry fte;
// //   while (true)
// //   {
// //     fte = frame_table->frame_table_entry[clock];
// //     ASSERT (&fte.frame_lock != NULL);
// //     if (!lock_held_by_current_thread (&fte.frame_lock))
// //     {
// //       if (lock_try_acquire (&fte.frame_lock))
// //       {
// //         printf ("clock %d\n", clock);
// //         if (&fte.spte->page_lock == NULL)
// //         {
// //           printf ("clock add\n");
// //           clock_algorithm ();
// //           continue;
// //         }

// //         // ASSERT (&fte.spte != NULL);
// //         ASSERT (&fte.spte->page_lock != NULL);
// //         lock_acquire (&fte.spte->page_lock);
// //         uint32_t* pagedir = fte.spte->pagedir;
// //         // Returns true if page directory pd contains a page table entry for page that is marked dirty (or accessed). Otherwise, returns false.
// //         bool accessed = pagedir_is_accessed (pagedir, fte.spte->pid);
// //         // If page directory pd has a page table entry for page, then its dirty (or accessed) bit is set to value.
// //         pagedir_set_accessed (pagedir, fte.spte->pid, false);
// //         lock_release (&fte.spte->page_lock);
// //         if (accessed) {
// //           lock_release(&fte.frame_lock);
// //         } else {
// //           ASSERT (&fte.spte->page_lock != NULL);
// //           lock_acquire (&fte.spte->page_lock);

// //           // lock_acquire(&fte.spte->page_lock);
// //           pagedir_clear_page (fte.spte->pagedir, fte.spte->pid);
// //           // lock_release(&fte.spte->page_lock);

// //           if (fte.spte->location == MAP_MEMORY)
// //           {
// //             evict_page_map (fte.spte);
// //           }
// //           else if (fte.spte->location == FILE_SYSTEM)
// //           {
// //             evict_page_file (fte.spte);
// //           }
// //           else if (fte.spte->location == SWAP_BLOCK)
// //           {
// //             evict_page_swap (fte.spte);
// //           }
// //           else
// //           {
// //             sysexit (-1);
// //           }
// //           lock_release (&fte.spte->page_lock);
          
// //           fte.spte = NULL;
// //           return fte;
// //         }
// //       }
// //     }
// //     clock_algorithm ();
// //   }
// //   printf ("reach here\n");
// //   // return NULL;
// // }



#include "vm/frame.h"
#include "userprog/pagedir.h"
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
#include "vm/page.h"

// /* Globals */
// static struct frame_table_entry* frame_table;
// static size_t frame_table->frame_table_entry_number;
// static void* frame_table->base;
// struct lock frame_table->frame_table_lock;
static uint32_t clock_hand = 0;

static void clock_algorithm (void);
// static void pass_frame(struct frame_table_entry* frame);
// static void prepare_frame_to_return(struct frame_table_entry* frame);
// static bool check_frame_contents(struct frame_table_entry* frame);


/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
// void init_frame_table(size_t num_frames, uint8_t* frame_base) {
//     frame_table->frame_table_entry_number = num_frames;
//     frame_table->base = frame_base;
//     lock_init(&frame_table->frame_table_lock);
//     frame_table = malloc(sizeof(struct frame_table_entry) * num_frames);
//     if(frame_table == NULL)
//     {
//         PANIC("could not allocate frame table");
//     }
//     struct frame_table_entry basic_frame;
//     basic_frame.spte = NULL;
//     basic_frame.frame = NULL;
//     uint32_t i;
//     for(i = 0; i < num_frames; i++)
//     {
//         memcpy((frame_table + i), &basic_frame, sizeof(struct frame_table_entry));
//         lock_init(&(frame_table[i].frame_lock));
//         frame_table[i].frame = (void*)((uint32_t)frame_table->base + (i << 12));
//     }
// }

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
    frame_table->frame_table_entry[i].spte = NULL;
  }
  lock_init (&(frame_table->frame_table_lock));
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: simply advances the clock_hand one frame forward.
 NOTE: the frame_table->frame_table_lock must be held by the current thread.
 --------------------------------------------------------------------
 */
static void clock_algorithm () {
    ASSERT(lock_held_by_current_thread(&frame_table->frame_table_lock));
    clock_hand++;
    if (clock_hand >= frame_table->frame_table_entry_number) {
        clock_hand = 0;
    }
}

uint32_t
frame_table_get_id (void* kaddr)
{
  return ((uint32_t) kaddr - (uint32_t) frame_table->base) >> 12;
}

uint32_t
frame_table_find_id (struct frame_table_entry* frame)
{
    for (uint32_t i = 0; i < frame_table->frame_table_entry_number; i++)
    {
        if (frame_table->frame_table_entry[i].frame == frame->frame) {
            return i;
        }
    }
    // if (clock_hand >= frame_table->frame_table_entry_number) {
    //     clock_hand = 0;
    // }
}

bool frame_lock_try_aquire (struct frame_table_entry* frame, struct supplementary_page_table_entry* page_try_pin) {
    
    if (frame == NULL) {
        page_try_pin->in_physical_memory = false;
        return false;
    }
    if (lock_held_by_current_thread(&frame->frame_lock)) {
        return true;
    }
    lock_acquire (&frame->frame_lock);
    if (frame->spte == page_try_pin) {
        return true;
    }
    lock_release (&frame->frame_lock);
    return false;
}


void load_page_from_map_memory (struct supplementary_page_table_entry* spte, struct frame_table_entry* fte)
{
  // void* kaddr = palloc_get_page (PAL_USER);
  // spte->fid = frame_table_get_id (kaddr);
  // struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
  load_page_from_file_system (spte, fte);
}

void load_page_from_swap_block (struct supplementary_page_table_entry* spte, struct frame_table_entry* fte)
{
//       void* kaddr = palloc_get_page (PAL_USER);
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
//     // printf ("eviction need!\n");
//     frame_table->frame_table_entry[(uint32_t) spte->fid] = evict_frame();
//     // frame_table_evict ();
//     fte = frame_table->frame_table_entry[(uint32_t) spte->fid];

//     // ASSERT (&fte.spte->page_lock != NULL);
//   }
// struct frame_table_entry* fte = &(frame_table->frame_table_entry[(uint32_t) spte->fid]);
swap_block_read(fte->frame, (uint32_t) spte->sid);
//   swap_block_read(spte->frame->frame, (uint32_t) spte->sid);
//   void* kaddr = palloc_get_page (PAL_USER);
//   spte->fid = (void*) frame_table_get_id (kaddr);
//   struct frame_table_entry fte = frame_table->frame_table_entry[(uint32_t) spte->fid];
}

void load_page_from_file_system (struct supplementary_page_table_entry* spte, struct frame_table_entry* fte)
{


  /* All zero page. */
  if (spte->page_zero_bytes == PGSIZE)
  {
    memset (fte->frame, 0, PGSIZE);
    // if (install_page_copy (spte->pid, fte.frame, spte->writeable))
    // {
    //     return ;
    // }
    return ;
  }

  
  // Fetch data into the frame.
  lock_acquire (&file_system_lock);
  uint32_t read_bytes = file_read_at (spte->file, fte->frame, spte->page_read_bytes, spte->file_ofs);
  lock_release (&file_system_lock);

  if (spte->page_read_bytes != PGSIZE)
  {
    memset (fte->frame + spte->page_read_bytes, 0, PGSIZE - spte->page_read_bytes);
  }
  if (read_bytes != spte->page_read_bytes)
  {
    // palloc_free_page (kaddr);
    sysexit (-1);
  }

  return ;
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
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: passes the frame and moves on
 --------------------------------------------------------------------
 */
// static void pass_frame(struct frame_table_entry* frame) {
//     lock_acquire(&frame_table->frame_table_lock);
//     lock_release(&frame->frame_lock);
//     clock_algorithm ();
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: prepares the current frame to return
 --------------------------------------------------------------------
 */
// static void prepare_frame_to_return(struct frame_table_entry* frame) {
//     lock_acquire(&frame->spte->page_lock);
//     evict_page_from_physical_memory(frame->spte);
//     frame->spte->frame = NULL;
//     frame->spte->in_physical_memory = false;
//     lock_release(&frame->spte->page_lock);
//     frame->spte = NULL;
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: checks the frame contents for state confirmation
    to handle thread exit.
 --------------------------------------------------------------------
 */
// static bool check_frame_contents(struct frame_table_entry* frame) {
//     if (frame->spte == NULL) {
//         pass_frame(frame);
//         return true;
//     } else {
//         if (frame->spte->owner_thread->pagedir == NULL) {
//             pass_frame(frame);
//             return true;
//         }
//     }
//     return false;
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: evict frame. This is a private function of the frame 
    file. In this function, we check the table of frames, and when
    we find one suitable for eviction, we write the contents of the
    frame to associated memory location, and then return the frame.
 NOTE: when checking the access bits, we need to make sure that if
    multiple virtual addresses refer to same frame, that they all
    see the update. Curently do so by only checking the kernel address.
 NOTE: we have to call advance clock_hand at the beginning to ensure 
    we move on from the prevous frame we evicted last. This will cause
    first sweep of eviction to begin at frame index 1, but that is
    ok, as subsequent frame sweeps will be in proper cycle.
 NOTE: Aquires the frame lock for the frame returned, and also
    release the frame_table->frame_table_lock. 
 --------------------------------------------------------------------
 */
// static 
struct frame_table_entry* frame_table_evict(void) {
    ASSERT(lock_held_by_current_thread(&frame_table->frame_table_lock));
    clock_algorithm ();
    struct frame_table_entry* fte = NULL;
    while (true) {
        fte = &(frame_table->frame_table_entry[clock_hand]);
        // bool aquired;
        // if (lock_held_by_current_thread(&frame->frame_lock)) {
        //     aquired = false;
        // } else {
        //     aquired = lock_try_acquire(&frame->frame_lock);
        // }
        // if (aquired) {
        if (!lock_held_by_current_thread (&fte->frame_lock))
        {
            if (lock_try_acquire (&fte->frame_lock))
            {
                // printf ("clock %d\n", clock_hand);
            
                // lock_release(&frame_table->frame_table_lock);
                    if (fte->spte == NULL || fte->spte->owner_thread->pagedir == NULL) {
                        // lock_acquire(&frame_table->frame_table_lock);
                        lock_release(&fte->frame_lock);
                        clock_algorithm ();

                        // pass_frame(fte);

                        // return true;
                        continue;
                    } 
                    // else if (fte->spte->owner_thread->pagedir == NULL)
                    //     {
                    //     pass_frame(fte);
                    //     // return true;
                    //     continue;
                    // }
                //     }
                //     return false;

                // if (check_frame_contents(fte)) {
                //     // printf ("clock add\n");
                //     continue;
                // }
                lock_release(&frame_table->frame_table_lock);

                uint32_t* pagedir = fte->spte->owner_thread->pagedir;

                lock_acquire(&fte->spte->owner_thread->pagedir_lock);
                bool accessed = pagedir_is_accessed (pagedir, fte->spte->pid);
                pagedir_set_accessed (pagedir, fte->spte->pid, false);
                lock_release (&fte->spte->owner_thread->pagedir_lock);
                if (accessed)
                {
                    lock_release(&fte->frame_lock);
                }
                else
                {
                    // prepare_frame_to_return(fte);
                    lock_acquire(&fte->spte->page_lock);
                    // evict_page_from_physical_memory(fte->spte);

                    clear_page_copy (fte->spte->pid, fte->spte->owner_thread);
                    // clear_page_copy (fte->spte);

                    if (fte->spte->location == MAP_MEMORY)
                    {
                        // printf ("load_page_from_map_memory\n");
                        // lock_acquire (&spte->page_lock);
                        // load_page_from_map_memory (spte, fte);
                        evict_page_from_map_memory (fte->spte);
                        // lock_release (&spte->page_lock);
                    }
                    else if (fte->spte->location == SWAP_BLOCK)
                    {
                        // printf ("load_page_from_swap_block\n");
                        // lock_acquire (&spte->page_lock);
                        // load_page_from_swap_block (spte, fte);
                        evict_page_from_swap_block (fte->spte);
                        // lock_release (&spte->page_lock);
                    }
                    else if (fte->spte->location == FILE_SYSTEM)
                    {
                        // printf ("load_page_from_file_system\n");
                        // lock_acquire (&spte->page_lock);
                        // load_page_from_file_system (spte, fte);
                        evict_page_from_file_system (fte->spte);
                        // load_file_page (spte);
                        // lock_release (&spte->page_lock);
                    }
                    else
                    {
                        sysexit (-1);
                    }


                    // switch (fte->spte->location) {
                    //     case SWAP_BLOCK:
                    //         evict_swap_page (fte->spte);
                    //         break;
                    //     case FILE_SYSTEM:
                    //         evict_file_page (fte->spte);
                    //         break;
                    //     case MAP_MEMORY:
                    //         evict_mmaped_page (fte->spte);
                    //         break;
                    //     default:
                    //         sysexit(-1);
                    //         // thread_current()->vital_info->exit_status = -1;
                    //         // if (thread_current()->is_running_user_program) {
                    //         //     printf("%s: exit(%d)\n", thread_name(), -1);
                    //         // }
                    //         // thread_exit();
                    //         break;
                    // }


                    // fte->spte->frame = NULL;
                    fte->spte->fid = NULL;
                    fte->spte->in_physical_memory = false;
                    lock_release(&fte->spte->page_lock);
                    fte->spte = NULL;

                    return fte;
                }
                lock_acquire(&frame_table->frame_table_lock);
            }
        }
        clock_algorithm ();
    }
    return NULL;
}


// struct frame_table_entry_table_entry frame_table_evict (void)
// {
//   // lock_acquire (&frame_table->frame_table_lock);
//   // ASSERT (lock_held_by_current_thread (&frame_table->frame_table_lock));
//   clock_algorithm ();
//   struct frame_table_entry_table_entry fte;
//   while (true)
//   {
//     fte = frame_table->frame_table_entry[clock];
//     ASSERT (&fte.frame_lock != NULL);
//     if (!lock_held_by_current_thread (&fte.frame_lock))
//     {
//       if (lock_try_acquire (&fte.frame_lock))
//       {
//         printf ("clock %d\n", clock);
//         if (&fte.spte->page_lock == NULL)
//         {
//           printf ("clock add\n");
//           clock_algorithm ();
//           continue;
//         }

//         // ASSERT (&fte.spte != NULL);
//         ASSERT (&fte.spte->page_lock != NULL);
//         lock_acquire (&fte.spte->page_lock);
//         uint32_t* pagedir = fte.spte->pagedir;
//         // Returns true if page directory pd contains a page table entry for page that is marked dirty (or accessed). Otherwise, returns false.
//         bool accessed = pagedir_is_accessed (pagedir, fte.spte->pid);
//         // If page directory pd has a page table entry for page, then its dirty (or accessed) bit is set to value.
//         pagedir_set_accessed (pagedir, fte.spte->pid, false);
//         lock_release (&fte.spte->page_lock);
//         if (accessed) {
//           lock_release(&fte.frame_lock);
//         } else {
//           ASSERT (&fte.spte->page_lock != NULL);
//           lock_acquire (&fte.spte->page_lock);

//           // lock_acquire(&fte.spte->page_lock);
//           pagedir_clear_page (fte.spte->pagedir, fte.spte->pid);
//           // lock_release(&fte.spte->page_lock);

//           if (fte.spte->location == MAP_MEMORY)
//           {
//             evict_page_map (fte.spte);
//           }
//           else if (fte.spte->location == FILE_SYSTEM)
//           {
//             evict_page_file (fte.spte);
//           }
//           else if (fte.spte->location == SWAP_BLOCK)
//           {
//             evict_page_swap (fte.spte);
//           }
//           else
//           {
//             sysexit (-1);
//           }
//           lock_release (&fte.spte->page_lock);
          
//           fte.spte = NULL;
//           return fte;
//         }
//       }
//     }
//     clock_algorithm ();
//   }
//   printf ("reach here\n");
//   // return NULL;
// }


// struct frame_table_entry_table_entry frame_table_evict (void)
// {
//   // lock_acquire (&frame_table->frame_table_lock);
//   // ASSERT (lock_held_by_current_thread (&frame_table->frame_table_lock));
//   clock_algorithm ();
//   struct frame_table_entry_table_entry fte;
//   while (true)
//   {
//     fte = frame_table->frame_table_entry[clock];
//     ASSERT (&fte.frame_lock != NULL);
//     if (!lock_held_by_current_thread (&fte.frame_lock))
//     {
//       if (lock_try_acquire (&fte.frame_lock))
//       {
//         printf ("clock %d\n", clock);
//         if (&fte.spte->page_lock == NULL)
//         {
//           printf ("clock add\n");
//           clock_algorithm ();
//           continue;
//         }

//         // ASSERT (&fte.spte != NULL);
//         ASSERT (&fte.spte->page_lock != NULL);
//         lock_acquire (&fte.spte->page_lock);
//         uint32_t* pagedir = fte.spte->pagedir;
//         // Returns true if page directory pd contains a page table entry for page that is marked dirty (or accessed). Otherwise, returns false.
//         bool accessed = pagedir_is_accessed (pagedir, fte.spte->pid);
//         // If page directory pd has a page table entry for page, then its dirty (or accessed) bit is set to value.
//         pagedir_set_accessed (pagedir, fte.spte->pid, false);
//         lock_release (&fte.spte->page_lock);
//         if (accessed) {
//           lock_release(&fte.frame_lock);
//         } else {
//           ASSERT (&fte.spte->page_lock != NULL);
//           lock_acquire (&fte.spte->page_lock);

//           // lock_acquire(&fte.spte->page_lock);
//           pagedir_clear_page (fte.spte->pagedir, fte.spte->pid);
//           // lock_release(&fte.spte->page_lock);

//           if (fte.spte->location == MAP_MEMORY)
//           {
//             evict_page_map (fte.spte);
//           }
//           else if (fte.spte->location == FILE_SYSTEM)
//           {
//             evict_page_file (fte.spte);
//           }
//           else if (fte.spte->location == SWAP_BLOCK)
//           {
//             evict_page_swap (fte.spte);
//           }
//           else
//           {
//             sysexit (-1);
//           }
//           lock_release (&fte.spte->page_lock);
          
//           fte.spte = NULL;
//           return fte;
//         }
//       }
//     }
//     clock_algorithm ();
//   }
//   printf ("reach here\n");
//   // return NULL;
// }

/*
 --------------------------------------------------------------------
 DESCRIPTION: given a physical memory address that is returned
    by palloc, returns the index into the frame table for the 
    corresponding frame struct.
 --------------------------------------------------------------------
 */
// static inline uint32_t get_frame_index(void* physical_memory_addr) {
//     ASSERT (frame_table->base != NULL);
//     ASSERT ((uint32_t)frame_table->base <= (uint32_t)physical_memory_addr);
//     uint32_t index = ((uint32_t)physical_memory_addr - (uint32_t)frame_table->base) >> 12;
//     ASSERT (index < frame_table->frame_table_entry_number);
//     return index;
// }

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES: 
 NOTE: Update so that this takes a boolean and releases the lock
    only if boolean is true
 NOTE: if is_fresh_stack_page is true, then we do not load from
    swao, because it is our first swap page.
 --------------------------------------------------------------------
 */
// bool frame_handler_palloc(bool zeros, struct supplementary_page_table_entry* spte, bool should_pin, bool is_fresh_stack_page) {
//     lock_acquire(&frame_table->frame_table_lock);
//     void* physical_memory_addr = palloc_get_page (PAL_USER | (zeros ? PAL_ZERO : 0));
    
//     struct frame_table_entry* frame;
//     if (physical_memory_addr != NULL) {
//         // frame = frame_table + get_frame_index(physical_memory_addr);
//         frame = &frame_table->frame_table_entry[frame_table_get_id(physical_memory_addr)];
//         lock_acquire(&frame->frame_lock);
//         lock_release(&frame_table->frame_table_lock);
//         ASSERT(frame->spte == NULL)
//     } else {
//         frame = evict_frame(); //aquires frame lock and releases frame_table->frame_table_lock
//     }
    
//     if (zeros) memset(frame->frame, 0, PGSIZE);
//     spte->frame = frame;
    
//     bool success = load_page_into_physical_memory(spte, is_fresh_stack_page);
    
//     if (!success) {
//         barrier();
//         spte->frame = NULL;
//         frame->spte = NULL;
//         spte->in_physical_memory = false;
//         palloc_free_page(physical_memory_addr);
//         lock_release(&frame->frame_lock);
//         lock_release(&spte->page_lock);
//         return false;
//     } else {
//         // printf ("successfully install\n");
//         frame->spte = spte;
//         spte->in_physical_memory = true;
//     }
//     if (should_pin == false) {
//         lock_release(&frame->frame_lock);
//     }
//     lock_release(&spte->page_lock);
//     return success;
// }

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */
bool frame_table_entry_free (struct supplementary_page_table_entry* spte) {
    // if (spte->frame == NULL) {
    //     return true;
    // }
    if (spte->fid == NULL) {
        return true;
    }
    // struct frame_table_entry* frame = frame_table + frame_table_get_id(spte->frame->frame);
    // struct frame_table_entry* frame = &frame_table->frame_table_entry[frame_table_get_id(spte->frame->frame)];
    struct frame_table_entry* frame = &frame_table->frame_table_entry[ (uint32_t) spte->fid];
    lock_acquire (&frame->frame_lock);
    if (frame->spte == spte) {
        //in this case, we are still the owner of the frame, so we can free the page
        palloc_free_page (frame->frame);
        // spte->frame = NULL;
        spte->fid = NULL;
        spte->in_physical_memory = false;
        frame->spte = NULL;
    }
    //If we are not the current owner, than some other thread swooped in and is using
    //the page, so we do not want to free it, thus do nothing.
    lock_release (&frame->frame_lock);
    return true;
}

/*
 --------------------------------------------------------------------
 IMPLIMENTATION NOTES:
 --------------------------------------------------------------------
 */



