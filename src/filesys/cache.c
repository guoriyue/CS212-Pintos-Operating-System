#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "devices/timer.h"
uint32_t cache_clock_hand;

// write back to disk
void
cache_flush (void)
{
  for(int i = 0; i < CACHE_BUFFER_SIZE; i++ )
  {
    lock_acquire (&cache_buffer[i].cache_entry_lock);
    // acquire lock outside if, because we access cache_buffer in if
    if (cache_buffer[i].dirty)
    {
      if (cache_buffer[i].flushing || cache_buffer[i].loading)
      {
        // not now
        lock_release (&cache_buffer[i].cache_entry_lock);
        continue;
      }
      
      cache_buffer[i].flushing = true;
      cache_buffer[i].next_sector_id = UNUSED_ENTRY_ID;
      lock_release (&cache_buffer[i].cache_entry_lock);

      block_write (fs_device, cache_buffer[i].sector_id, cache_buffer[i].data);

      lock_acquire (&cache_buffer[i].cache_entry_lock);
      cache_buffer[i].flushing = false;
      cache_buffer[i].dirty = false;
      // signal one up
      ASSERT (lock_held_by_current_thread (&cache_buffer[i].cache_entry_lock));
      cond_signal (&cache_buffer[i].cache_ready, &cache_buffer[i].cache_entry_lock);
      lock_release (&cache_buffer[i].cache_entry_lock);
    }
  }
}



/* Initialize cache */
void
cache_init (void)
{
  // for assignment 4
  list_init(&read_ahead_list);
  lock_init(&read_ahead_list_lock);
  cond_init(&read_ahead_list_cond);
  lock_init(&global_cache_lock);

  for (uint32_t i = 0; i < CACHE_BUFFER_SIZE; i++)
  {
    cond_init(&cache_buffer[i].cache_ready);
    lock_init(&cache_buffer[i].cache_entry_lock);
    memset(cache_buffer[i].data, 0, BLOCK_SECTOR_SIZE*sizeof(uint8_t));
    cache_buffer[i].accessed = false;
    cache_buffer[i].dirty = false;
    cache_buffer[i].loading = false;
    cache_buffer[i].flushing = false;
    // -1 for unused sector_id
    cache_buffer[i].sector_id = UNUSED_ENTRY_ID;
    cache_buffer[i].next_sector_id = UNUSED_ENTRY_ID;

    cache_buffer[i].process_writing = 0;
    cache_buffer[i].process_reading = 0;
    cache_buffer[i].process_waiting_to_write = 0;
    cache_buffer[i].process_waiting_to_read = 0;
  }
  cache_clock_hand = 0;
}

/* Check cache hit. Use is_write to distinguish write hit and
read hit. */
int
cache_hit (block_sector_t sector, bool is_write)
{
  for (int i = 0; i < CACHE_BUFFER_SIZE; i++)
  {
    lock_acquire (&cache_buffer[i].cache_entry_lock);
    
    if (cache_buffer[i].sector_id == sector ||
    (cache_buffer[i].flushing && cache_buffer[i].next_sector_id == sector))
    {
      // sector already in cache
      while (cache_buffer[i].flushing)
      {
        // if flusing, wait
        // after this the cache will be in disk
        cond_wait (&cache_buffer[i].cache_ready, 
        &cache_buffer[i].cache_entry_lock);
      }
      if (cache_buffer[i].sector_id == sector)
      {
        if (!is_write)
        {
          cache_buffer[i].process_waiting_to_read++;
        }
        else
        {
          cache_buffer[i].process_waiting_to_write++;
        }
        return i;
      }
      else
      {
        // evicted
        lock_release (&cache_buffer[i].cache_entry_lock);
        return -1;
      }
    }
    lock_release (&cache_buffer[i].cache_entry_lock);
  }
  return -1;
}

/* Clock algorithm to evict cache. */
uint32_t
cache_evict (void)
{
  while (true)
  {
    lock_acquire (&cache_buffer[cache_clock_hand].cache_entry_lock);
    
    if (cache_buffer[cache_clock_hand].flushing || 
    cache_buffer[cache_clock_hand].loading || 
    cache_buffer[cache_clock_hand].process_writing ||
    cache_buffer[cache_clock_hand].process_reading || 
    cache_buffer[cache_clock_hand].process_waiting_to_write || 
    cache_buffer[cache_clock_hand].process_waiting_to_read)
    {
      // cache block is using, next
      lock_release (&cache_buffer[cache_clock_hand].cache_entry_lock);
      cache_clock_hand = (cache_clock_hand + 1) % CACHE_BUFFER_SIZE;
      continue;
    }

    if (cache_buffer[cache_clock_hand].accessed)
    {
      // recently accessed, next
      cache_buffer[cache_clock_hand].accessed = false;
      lock_release (&cache_buffer[cache_clock_hand].cache_entry_lock);
      cache_clock_hand = (cache_clock_hand + 1) % CACHE_BUFFER_SIZE;
      continue;
    }
 
    // evict
    lock_release (&global_cache_lock);
    uint32_t ret = cache_clock_hand;
    cache_clock_hand = (cache_clock_hand + 1) % CACHE_BUFFER_SIZE;
    return ret;
  }
}

struct cache_entry *
cache_get_entry (block_sector_t sector_id)
{
  uint32_t evict_idx = cache_evict();
  if (cache_buffer[evict_idx].dirty)
  {
    cache_buffer[evict_idx].flushing = true;
    cache_buffer[evict_idx].next_sector_id = sector_id;
    lock_release (&cache_buffer[evict_idx].cache_entry_lock);
    block_write (fs_device, cache_buffer[evict_idx].sector_id, 
    cache_buffer[evict_idx].data);
    cache_buffer[evict_idx].dirty = false;
    lock_acquire (&cache_buffer[evict_idx].cache_entry_lock);
  }
  cache_buffer[evict_idx].accessed = false;
  cache_buffer[evict_idx].dirty = false;
  cache_buffer[evict_idx].loading = false;
  cache_buffer[evict_idx].flushing = false;

  cache_buffer[evict_idx].sector_id = sector_id;
  cache_buffer[evict_idx].next_sector_id = UNUSED_ENTRY_ID;
  
  cond_signal (&cache_buffer[evict_idx].cache_ready, 
  &cache_buffer[evict_idx].cache_entry_lock);
  return &cache_buffer[evict_idx];
}

void
cache_read (block_sector_t sector, void *buffer,
off_t start, off_t length)
{
  lock_acquire(&global_cache_lock);
  int cache_id_hit = cache_hit(sector, false);
  struct cache_entry *ce;
  if(cache_id_hit != -1)
  {
    ce = &cache_buffer[cache_id_hit];
    lock_release(&global_cache_lock);
  }
  else
  {
    ce = cache_get_entry(sector);
    ce->loading = true;
    lock_release(&ce->cache_entry_lock);

    block_read (fs_device, sector, ce->data);

    lock_acquire(&ce->cache_entry_lock);
    ce->loading = false;
    cond_signal(&ce->cache_ready, &ce->cache_entry_lock);
    ce->process_waiting_to_read++;
  }
   
  while (ce->loading || ce->flushing || 
  ce->process_waiting_to_write || ce->process_writing)
  {
    // wait until able to read
    cond_wait(&ce->cache_ready, &ce->cache_entry_lock);
  }
  ce->process_waiting_to_read--;
  ce->process_reading++;

  lock_release (&ce->cache_entry_lock);

  memcpy (buffer, ce->data + start, length);

  lock_acquire (&ce->cache_entry_lock);
  ce->process_reading--;
  cond_signal (&ce->cache_ready, &ce->cache_entry_lock);
  
  ce->accessed = true;
  lock_release (&ce->cache_entry_lock);
}

void
cache_write (block_sector_t sector, const void *buffer,
off_t start, off_t length)
{
  lock_acquire(&global_cache_lock);
  int cache_id_hit = cache_hit (sector, true);
  struct cache_entry *ce;
  if(cache_id_hit != -1)
  {
    // hit
    ce = &cache_buffer[cache_id_hit];
    lock_release(&global_cache_lock);
  }
  else
  {
    ce = cache_get_entry(sector);
    ce->process_waiting_to_write++;
  }

  while (ce->loading || ce->flushing || 
  ce->process_reading || ce->process_writing)
  {
    // wait until able to write
    cond_wait(&ce->cache_ready, &ce->cache_entry_lock);
  }

  ce->process_waiting_to_write--;
  ce->process_writing++;
  lock_release (&ce->cache_entry_lock);

  memcpy (ce->data + start, buffer, length);

  lock_acquire (&ce->cache_entry_lock);
  ce->process_writing--;

  // signal up another one
  cond_signal (&ce->cache_ready, &ce->cache_entry_lock);

  ce->accessed = true;
  ce->dirty = true;
  lock_release (&ce->cache_entry_lock);
}


// prefetch deamon process
void
cache_read_ahead_function (void * aux UNUSED)
{
  while (true)
  {
    lock_acquire (&read_ahead_list_lock);
    while (list_empty(&read_ahead_list))
    {
      cond_wait (&read_ahead_list_cond, &read_ahead_list_lock);
    }
    struct list_elem * e = list_pop_front (&read_ahead_list);
    struct read_ahead_pair * request = list_entry (e, struct read_ahead_pair, elem);
    
    void * tmp_buffer = (void *) malloc(BLOCK_SECTOR_SIZE);
    cache_read (request->sector, tmp_buffer, 0, BLOCK_SECTOR_SIZE);
    free (request);
    free (tmp_buffer);
    lock_release (&read_ahead_list_lock);
  }
}

// cache_flush_function
void
cache_flush_function (void * aux UNUSED)
{
  while (true)
  {
    timer_sleep (3000);
    cache_flush ();
  }
}