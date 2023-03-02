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

