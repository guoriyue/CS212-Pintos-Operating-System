#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <list.h>
/* Need typedef and repeat name. */
typedef enum page_location
{
    MAP_MEMORY,
    // in memory mapped
    SWAP_BLOCK,
    // in swap block
    FILE_SYSTEM,
    // in file system
} page_location;

struct supplementary_page_table_entry
{
    struct lock page_lock;
    page_location location;
    void* pid;
    void* fid;
    bool writable;
    struct list_elem supplementary_page_table_entry_elem;
    // uint32_t *pte;

    size_t page_read_bytes;
    size_t page_zero_bytes;

    struct file *file;
    off_t file_ofs;
};

struct supplementary_page_table_entry* supplementary_page_table_entry_create
(void* pid,
bool writable,
page_location location,
size_t page_read_bytes,
size_t page_zero_bytes,
struct file* file,
off_t file_ofs);

void supplementary_page_table_entry_insert (struct supplementary_page_table_entry* e);
void load_page_from_swap_block (struct supplementary_page_table_entry* spte);
void load_page_from_map_memory (struct supplementary_page_table_entry* spte);
void load_page_from_file_system (struct supplementary_page_table_entry* spte);
struct supplementary_page_table_entry* supplementary_page_table_entry_find
(void* vaddr);

bool
install_page_copy (void *upage, void *kpage, bool writable);
