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
};

struct frame_table
{
   size_t frame_table_entry_number;
   struct frame_table_entry *frame_table_entry;
   uint8_t *base;
   struct lock frame_table_lock;
};

struct frame_table *frame_table;

void frame_table_init(size_t frame_table_entry_number, uint8_t *frame_table_base);
struct frame_table_entry *frame_table_evict(void);

uint32_t
frame_table_get_id(void *kaddr);
uint32_t
frame_table_find_id(struct frame_table_entry *fte);
bool frame_lock_try_aquire(struct frame_table_entry *frame, struct supplementary_page_table_entry *page_try_pin);
bool frame_table_entry_free(struct supplementary_page_table_entry *spte);

void load_page_from_file_system(struct supplementary_page_table_entry *spte, struct frame_table_entry *fte);
void load_page_from_swap_block(struct supplementary_page_table_entry *spte, struct frame_table_entry *fte);
void load_page_from_map_memory(struct supplementary_page_table_entry *spte, struct frame_table_entry *fte);
