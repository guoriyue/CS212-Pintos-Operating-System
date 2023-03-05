#define FRAME_TABLE_ERROR __SIZE_MAX__
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "threads/synch.h"
/* Frame table entry */
struct frame_table_entry
{
  void *frame;
  struct lock frame_lock;
  struct supplementary_page_table_entry *spte;
   //  struct supplementary_page_table_entry* resident_page;
   //  void* physical_mem_frame_base;
   //  struct lock frame_lock;
};

struct frame_table
{
  size_t frame_table_entry_number;
  struct frame_table_entry *frame_table_entry;
  uint8_t* base;
  struct lock frame_table_lock;
};

struct frame_table *frame_table;

void
frame_table_init (size_t frame_table_entry_number, uint8_t* frame_table_base);
struct frame_table_entry* evict_frame(void);
// // void frame_table_init (size_t frame_table_entry_number, uint8_t* frame_table_base);
// // size_t frame_table_scan (struct frame_table *f, size_t start, size_t cnt);
// // bool frame_table_empty (struct frame_table *f, size_t start, size_t cnt);
uint32_t
frame_table_get_id (void* kaddr);
// // struct frame_table_entry frame_table_evict (void);
// // void
// // frame_table_entry_free (struct supplementary_page_table_entry* spte);
// // void clock_algorithm (void);



// #ifndef __VM_FRAME_H
// #define __VM_FRAME_H

// #include <stdbool.h>
// #include <stdint.h>
// #include "threads/thread.h"

//any time we palloc, we create an spte, any time we page fault, we check the spte's for the one to bring in.

/*
 --------------------------------------------------------------------
 DESCRIPTION: The struct containing information to manage
    a frame of physical memory
 NOTE: Resident_page is a pointer to the spte for the page
    that currently occupies a frame.
 NOTE: frame_base is the pointer that is returned by a call
    to palloc. It is the base address for a page of physical
    memory.
 NOTE: frame_lock locks the given frame for sycnchronization 
    purposes
 --------------------------------------------------------------------
 */
// struct frame {
//     struct supplementary_page_table_entry* resident_page;
//     void* physical_mem_frame_base;
//     struct lock frame_lock;
// };


/*
 --------------------------------------------------------------------
 DESCRIPTION: initializes the frame table.
 --------------------------------------------------------------------
 */
// void init_frame_table(size_t num_frames, uint8_t* frame_base);


/*
 --------------------------------------------------------------------
 DESCRIPTION: this function allocates a new page of memory to the 
    user. 
 NOTE: simply a wrapper around palloc. Calls palloc to get a 
    page of memory. If palloc returns null, we evict a page by 
    consulting our frame_table. 
 NOTE: the spte is created before the call to this function. 
    The spte contains all of the information about the page.
 NOTE: this function does not modify the data in the page. It 
    simply returns the base address of a physical page of 
    memory. 
 NOTE: does not release the lock if should_pin is true. This 
    is only the case when we pin a frame from a system call. 
 --------------------------------------------------------------------
 */
bool frame_handler_palloc(bool zeros, struct supplementary_page_table_entry* spte, bool should_pin, bool is_fresh_stack_page);

/*
 --------------------------------------------------------------------
 DESCRIPTION: Wrapper around the call to palloc_free. In addition
    to calling palloc_free(physical_memory_address), also takes care of
    frame cleanup...
 --------------------------------------------------------------------
 */
bool frame_handler_palloc_free(struct supplementary_page_table_entry* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: this function tries to acquire the lock on the given
    frame. Once the lock is aquired, we check if the resident_page 
    matches spte, If so, than the stpe is the owner, and has just
    aquired its own lock. If not, we release the lock and return false.
 NOTE: CHECK FOR DEADLOCK HERE!!
 NOTE: if we aquire lock and we are owner, return true, else, release
    lock and return false.
 NOTE: in the case where we return false, we simply call 
    frame_handler_palloc with pin value set to true.
 SYNCHRONIZATION TO CEHCK FOR: we have to check for the case
    in which the page trying to acquire the lock on its
    frame gets evicted while in this call. In that case, the page
    trying to aquire the lock will no longer be the owner of the frame
    and so we do not want to pin this page.
 --------------------------------------------------------------------
 */
bool aquire_frame_lock(struct frame_table_entry* frame, struct supplementary_page_table_entry* page_trying_to_pin);


// #endif /* __VM_FRAME_H */