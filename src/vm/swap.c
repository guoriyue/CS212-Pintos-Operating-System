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
void swap_table_init(void)
{
    swap_table.swap_block = block_get_role(BLOCK_SWAP);

    swap_table.max_swap_slots_number_per_page = (PGSIZE / BLOCK_SECTOR_SIZE);
    swap_table.swap_slots_number = block_size(swap_table.swap_block) 
    / swap_table.max_swap_slots_number_per_page;
    swap_table.swap_slots = malloc(swap_table.swap_slots_number * sizeof(uint32_t));
    for (uint32_t i = 0; i < swap_table.swap_slots_number; i++)
    {
        swap_table.swap_slots[i] = i;
    }
    swap_table.swap_slot_current = 0;
    lock_init(&swap_table.swap_table_lock);
}

/* Write page kaddr to swap. */
uint32_t
swap_block_write(void *kaddr)
{
    uint32_t slot;
    ASSERT(&swap_table.swap_table_lock != NULL);
    lock_acquire(&swap_table.swap_table_lock);
    if (swap_table.swap_slot_current >= swap_table.swap_slots_number)
    {
        printf("No swap slots\n");
    }
    slot = swap_table.swap_slots[swap_table.swap_slot_current];
    swap_table.swap_slot_current++;

    for (uint32_t i = 0; i < swap_table.max_swap_slots_number_per_page; i++)
    {
        block_sector_t sector = slot * swap_table.max_swap_slots_number_per_page + i;
        void *buffer = kaddr + i * BLOCK_SECTOR_SIZE;
        block_write(swap_table.swap_block, sector, buffer);
    }
    lock_release(&swap_table.swap_table_lock);
    return slot;
}

/* Read kaddr and free slot. */

void swap_block_read(void *kaddr, uint32_t slot)
{
    block_sector_t sector;
    ASSERT(&swap_table.swap_table_lock != NULL);
    lock_acquire(&swap_table.swap_table_lock);
    for (uint32_t i = 0; i < swap_table.max_swap_slots_number_per_page; i++)
    {
        block_sector_t sector = slot * swap_table.max_swap_slots_number_per_page + i;
        void *buffer = kaddr + i * BLOCK_SECTOR_SIZE;
        block_read(swap_table.swap_block, sector, buffer);
    }

    swap_table.swap_slot_current--;
    swap_table.swap_slots[swap_table.swap_slot_current] = slot;
    lock_release(&swap_table.swap_table_lock);
}