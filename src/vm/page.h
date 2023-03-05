#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <list.h>
#include "threads/thread.h"
#include "filesys/off_t.h"

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
   void *pid;
   void *fid;
   void *sid;
   bool writeable;
   bool in_physical_memory;
   struct list_elem supplementary_page_table_entry_elem;
   size_t page_read_bytes;
   size_t page_zero_bytes;
   struct file *file;
   int32_t file_ofs;
   uint32_t *pagedir;
   struct lock pagedir_lock;
   struct thread *owner_thread; /* need to access pagedir */
};

struct supplementary_page_table_entry *supplementary_page_table_entry_create(void *pid,
                                                                             bool writable,
                                                                             page_location location,
                                                                             size_t page_read_bytes,
                                                                             size_t page_zero_bytes,
                                                                             struct file *file,
                                                                             int32_t file_ofs,
                                                                             bool in_physical_memory);
void free_supplementary_page_table(struct list *supplementary_page_table);
void free_supplementary_page_table_entry(struct list_elem *e);
void supplementary_page_table_entry_insert(struct supplementary_page_table_entry *e);
void evict_page_from_file_system(struct supplementary_page_table_entry *spte);
void evict_page_from_swap_block(struct supplementary_page_table_entry *spte);
void evict_page_from_map_memory(struct supplementary_page_table_entry *spte);
void pin_page(void *virtual_address);
void unpin_page(void *virtual_address);

bool install_page_copy(void *upage, void *kpage, bool writable);

void clear_page_copy(void *upage, struct thread *t);
bool stack_growth(void *fault_addr);
