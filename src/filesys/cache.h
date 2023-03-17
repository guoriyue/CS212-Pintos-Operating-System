#include "off_t.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"

#define CACHE_BUFFER_SIZE 64
#define UNUSED_ENTRY_ID -1

struct cache_entry
{
  // cache status
  bool accessed;
  bool dirty;
  bool loading;
  bool flushing;

  // process status
  int process_writing;
  int process_reading;
  int process_waiting_to_write;
  int process_waiting_to_read;

  block_sector_t sector_id;
  // to be load if flushing
  block_sector_t next_sector_id;
  
  // can be read or write
  struct condition cache_ready;
  // fine grained lock
  struct lock cache_entry_lock;
  // data of this sector
  uint8_t data[BLOCK_SECTOR_SIZE];
};

// cache buffer
struct cache_entry cache_buffer[CACHE_BUFFER_SIZE];

struct lock global_cache_lock;

struct list read_ahead_list;
struct lock read_ahead_list_lock;
struct condition read_ahead_list_cond;

struct read_ahead_pair
{
  block_sector_t sector;
  struct list_elem elem;
};

void cache_init (void);


void cache_read (block_sector_t sector, void *buffer,
                                off_t start, off_t length);

void cache_write_partial (block_sector_t sector, const void *buffer,
                                off_t start, off_t length);

void cache_flush (void);

struct cache_entry *
cache_get_entry (block_sector_t sector_id);

int
cache_hit (block_sector_t sector, bool write_flag);

uint32_t
cache_evict (void);

void
cache_flush_function (void * aux UNUSED);

void
cache_read_ahead_function (void * aux UNUSED);