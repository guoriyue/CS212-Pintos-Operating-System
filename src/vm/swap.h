#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <list.h>
#include "devices/block.h"
#include "threads/synch.h"
struct swap_table
{
    struct lock swap_table_lock;
    struct block *swap_block;
    uint32_t *swap_slots;
    // the number of swap slots in swap_table
    uint32_t swap_slots_number;
    uint32_t max_swap_slots_number_per_page;
    uint32_t swap_slot_current;
};

struct swap_table swap_table;

void
swap_table_init (void);
uint32_t
swap_block_write (void *kaddr);
void
swap_block_read (void *kaddr, uint32_t slot);



// /* Swap table datastructure */

// #ifndef  VM_SWAP_H
// #define  VM_SWAP_H

// #include <stdint.h>
// #include "devices/block.h"
// #include "threads/synch.h"
// #include "threads/vaddr.h"

// #define SECTORS_PER_PAGE    (PGSIZE / BLOCK_SECTOR_SIZE)

// struct block *swap_block; /* The swap device */

// /* The global swap table has precisely one job: It keeps track of which swap
//  * slots are open. It does so with a stack that contains all open swap slots.
//  */

// uint32_t *swap_table;  /* Array of open indices */
// uint32_t swap_slots;   /* Size of the array */
// int swap_top;          /* Index of the top open swap slot */
// struct lock swap_lock; /* Lock for the swap table */



// void init_swap_table (void);
// uint32_t write_to_swap (void *);
// void read_from_swap (void *, uint32_t);
// void free_swap (uint32_t);

// #endif /* vm/swap.h */