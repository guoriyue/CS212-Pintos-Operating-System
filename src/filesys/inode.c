#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <hash.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define UNUSED_IDX 125
// should be 123 if we have indirect and double indirect
#define SECTOR_INDEX 128
#define L0_CACHE_CAPACITY    (UNUSED_IDX * BLOCK_SECTOR_SIZE)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    // struct inode_disk data;             /* Inode content. */
    struct lock lock_inode;             /* Inode lock */
    block_sector_t inode_disk_sector;
  };


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  ASSERT (inode_disk != NULL);
  if (pos < L0_CACHE_CAPACITY)
  {
    return inode_disk->unused[pos / BLOCK_SECTOR_SIZE];
  }
  else
  {
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
  cache_init ();
}




/* Allocate a new zero sector */
static block_sector_t
zero_sector (void)
{
  block_sector_t sector;
  // Allocates 1 consecutive sectors from the free map and stores the first into sector.
  if (!free_map_allocate (1, &sector))
    return -1;
  char *zeros = malloc (BLOCK_SECTOR_SIZE * sizeof (char));
  if (zeros == NULL)
    return -1;
  memset (zeros, 0, BLOCK_SECTOR_SIZE);
  cache_write (sector, zeros, 0, BLOCK_SECTOR_SIZE);
  free (zeros);  
  return sector;
}

struct inode *
open_inode_find (block_sector_t sector)
{
  struct list_elem *e;
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      struct inode *inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
      {
        return inode;
      }
    }
  return NULL;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      // disk_inode->length start 0
      disk_inode->length = 0;

      off_t size_need_to_allocate = length - disk_inode->length;
      off_t size_left_in_block = ROUND_UP (disk_inode->length, BLOCK_SECTOR_SIZE) - disk_inode->length;
      if (size_need_to_allocate <= size_left_in_block)
      {
        // simply allocate
        disk_inode->length = length;
      }
      else
      {
        // direct allocate zero sectors
        off_t offset = disk_inode->length;
        
        while (offset < length)
        {
          block_sector_t allocated_sector = zero_sector ();
          if ((int) allocated_sector == -1)
          { 
            break;
          }
          offset += BLOCK_SECTOR_SIZE;
          if (offset <= L0_CACHE_CAPACITY)
          {
            // sector_id start from 0, need -1
            off_t sector_id = (offset - 1) / BLOCK_SECTOR_SIZE;
            disk_inode->unused[sector_id] = allocated_sector;
          }
          else
          {
            // exceed L0 cache capacity, need extension
            break;
          }
          // update length
          disk_inode->length = length;
        }
      }

      ASSERT (disk_inode->length >= length);
      ASSERT (disk_inode->length-length < BLOCK_SECTOR_SIZE);
      disk_inode->magic = INODE_MAGIC;
      disk_inode->start = sector;
      cache_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
      success = true;

      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire (&open_inodes_lock);
  inode = open_inode_find (sector);
  if (inode != NULL)
  {
    lock_release (&open_inodes_lock);
    inode_reopen (inode);
    return inode;
  }
  
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release (&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock_inode);
  

  struct inode_disk *disk_inode;
  disk_inode = malloc (sizeof *disk_inode);
  if (disk_inode == NULL)
    return NULL;

  cache_read (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free (disk_inode);
  return inode;
}



/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire (&inode->lock_inode);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire (&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release (&open_inodes_lock);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct inode_disk *disk_inode;
          disk_inode = malloc (sizeof *disk_inode);
          if (disk_inode == NULL)
          {
            return;
          }
          cache_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          off_t file_end = ROUND_UP (inode_length (inode), BLOCK_SECTOR_SIZE);

          for (int i = 0; i < file_end; i += BLOCK_SECTOR_SIZE)
          {
            block_sector_t sector = byte_to_sector (disk_inode, i);
            free_map_release (sector, 1);
          }

          free (disk_inode);
          free_map_release (inode->sector, 1);
        }
      lock_release (&inode->lock_inode);
      free (inode);
      return;
    }
  lock_release (&inode->lock_inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire (&inode->lock_inode);
  inode->removed = true;
  lock_release (&inode->lock_inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if (offset >= inode_length (inode))
  {
    return 0;
  }

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  
  struct inode_disk *disk_inode;
  disk_inode = malloc (sizeof *disk_inode);
  if (disk_inode == NULL)
  {
    return 0;
  }
  cache_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // cache read full sector
          cache_read (sector_idx, buffer + bytes_read, 0, BLOCK_SECTOR_SIZE);
        }
      else 
        {
          cache_read (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;

      off_t offset_of_read_ahead = offset;
      if (size == 0) offset_of_read_ahead = offset + BLOCK_SECTOR_SIZE;
      if (offset + BLOCK_SECTOR_SIZE < inode_length (inode))
      {
        // prefetch
        block_sector_t read_ahead_block =
        byte_to_sector (disk_inode, offset + BLOCK_SECTOR_SIZE);
        lock_acquire(&read_ahead_list_lock);
        struct read_ahead_pair * request = (struct read_ahead_pair *) malloc(sizeof(struct read_ahead_pair));
        if (request != NULL) {
          request->sector = read_ahead_block;
          list_push_back (&read_ahead_list, &request->elem);
          cond_signal (&read_ahead_list_cond, &read_ahead_list_lock);
          lock_release (&read_ahead_list_lock);
        }
      }
    }
  
  free (disk_inode);

  return bytes_read;
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct inode_disk *disk_inode;
  disk_inode = malloc (sizeof *disk_inode);

  if (disk_inode == NULL)
    return 0;

  cache_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

  if (inode->deny_write_cnt)
    return 0;

  
  lock_acquire (&inode->lock_inode);

  if (offset + size > disk_inode->length)
  {
    // need extension
    lock_release (&inode->lock_inode);
    free (disk_inode);
    return 0;
  }
  else
  {
    lock_release (&inode->lock_inode);
  }

  lock_acquire (&inode->lock_inode);
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;

      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      {
        // write full sector.
        cache_write (sector_idx, buffer + bytes_written, 
        0, BLOCK_SECTOR_SIZE);
      }
      else
      {
        cache_write (sector_idx, buffer + bytes_written, 
        sector_ofs, chunk_size);
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
      
    }
  lock_release (&inode->lock_inode);
  
  cache_write (inode->sector,disk_inode, 0, BLOCK_SECTOR_SIZE);
  free (disk_inode);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire (&inode->lock_inode);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release (&inode->lock_inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire (&inode->lock_inode);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release (&inode->lock_inode);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *inode_dsk;
  inode_dsk = malloc (sizeof *inode_dsk);
  cache_read (inode->sector, inode_dsk, 0, BLOCK_SECTOR_SIZE);
  return inode_dsk->length;
}