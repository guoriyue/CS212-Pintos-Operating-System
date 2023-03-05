#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/swap.h"
void
swap_table_init (void)
{
    swap_table.swap_block = block_get_role (BLOCK_SWAP);

    swap_table.max_swap_slots_number_per_page = (PGSIZE / BLOCK_SECTOR_SIZE);
    swap_table.swap_slots_number = block_size (swap_table.swap_block) / swap_table.max_swap_slots_number_per_page;
    swap_table.swap_slots = malloc (swap_table.swap_slots_number * sizeof(uint32_t));
    for (uint32_t i = 0; i < swap_table.swap_slots_number; i++)
    {
        swap_table.swap_slots[i] = i;
    }
    swap_table.swap_slot_current = 0;
    lock_init(&swap_table.swap_table_lock);
}

/* Write page kaddr to swap. */
uint32_t
swap_block_write (void *kaddr)
{
    uint32_t slot;
    ASSERT (&swap_table.swap_table_lock != NULL);
    lock_acquire (&swap_table.swap_table_lock);
    if (swap_table.swap_slot_current >= swap_table.swap_slots_number)
    {
        printf ("No swap slots\n");
    }
    slot = swap_table.swap_slots[swap_table.swap_slot_current];
    swap_table.swap_slot_current++;
    
    for (uint32_t i = 0; i < swap_table.max_swap_slots_number_per_page; i++)
    {
        block_sector_t sector = slot * swap_table.max_swap_slots_number_per_page + i;
        void *buffer = kaddr + i * BLOCK_SECTOR_SIZE;
        block_write (swap_table.swap_block, sector, buffer);
    }
    lock_release (&swap_table.swap_table_lock);
    return slot;
}

/* Read kaddr and free slot. */

void
swap_block_read (void *kaddr, uint32_t slot)
{
    block_sector_t sector;
    ASSERT (&swap_table.swap_table_lock != NULL);
    lock_acquire (&swap_table.swap_table_lock);
    for (uint32_t i = 0; i < swap_table.max_swap_slots_number_per_page; i++)
    {
        block_sector_t sector = slot * swap_table.max_swap_slots_number_per_page + i;
        void *buffer = kaddr + i * BLOCK_SECTOR_SIZE;
        block_read (swap_table.swap_block, sector, buffer);
    }
    
    swap_table.swap_slot_current--;
    swap_table.swap_slots[swap_table.swap_slot_current] = slot;
    lock_release (&swap_table.swap_table_lock);
}



// /* Swap table implementation */

// #include <debug.h>
// #include <stdint.h>
// #include "devices/block.h"
// #include "threads/malloc.h"
// #include "vm/swap.h"

// static void free_slot (uint32_t slot);

// void
// init_swap_table (void)
// {
//     swap_block = block_get_role(BLOCK_SWAP);
//     if (swap_block == NULL)
//         PANIC("No swap partition found!");

//     swap_slots = block_size(swap_block) / (SECTORS_PER_PAGE);
//     swap_table = malloc(swap_slots * sizeof(uint32_t));
//     if (swap_table == NULL)
//         PANIC("Couldn't allocate swap table!");

//     uint32_t slot;
//     for (slot = 0; slot < swap_slots; slot++)
//         swap_table[slot] = slot;

//     swap_top = swap_slots - 1;
//     lock_init(&swap_lock);
// }

// /* Writes the page located at kaddr to swap, returning the swap slot. */
// uint32_t
// write_to_swap (void *kaddr)
// {
//     // printf ("swap write slot\n");
//     /* Only lock the swap table to find an open swap slot, not while writing! */
//     uint32_t slot;

//     lock_acquire(&swap_lock);
//     if (swap_top < 0)
//         PANIC("Out of swap slots!");
    
//     slot = swap_table[swap_top];
//     swap_table[swap_top] = 0xBADC0DE5;  /* For debugging */
//     swap_top--;
//     lock_release(&swap_lock);

//     /* Now begin writing to disk */
//     int i;
//     void *buffer;
//     block_sector_t sector;
//     for (i = 0; i < SECTORS_PER_PAGE; i++)
//     {
//         sector = slot * SECTORS_PER_PAGE + i;
//         buffer = kaddr + i * BLOCK_SECTOR_SIZE;
//         block_write (swap_block, sector, buffer);
//     }

//     return slot;
// }

// /* Frees the particular slot */
// static void free_slot (uint32_t slot)
// {
    
//     lock_acquire(&swap_lock);
//     swap_top++;
//     ASSERT(swap_top < (int)swap_slots);
//     swap_table[swap_top] = slot;
//     lock_release(&swap_lock);
// }

// /* Reads the data located at the given swap slot into the page-sized kaddr,
//  * and frees the slot in the swap table. */
// void
// read_from_swap (void *kaddr, uint32_t slot)
// {
//     // printf ("read_from_swapt\n");
//     /* Read the swap data into memory */
//     int i;
//     void *buffer;
//     block_sector_t sector;
//     for (i = 0; i < SECTORS_PER_PAGE; i++)
//     {
//         sector = slot * SECTORS_PER_PAGE + i;
//         buffer = kaddr + i * BLOCK_SECTOR_SIZE;
//         block_read (swap_block, sector, buffer);
//     }

//     /* Tell the swap table that this slot is now free */
//     free_slot (slot);
// }
