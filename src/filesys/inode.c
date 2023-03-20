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
#define SECTOR_IDXS 125
/* Number of block numbers stored in a singly indirect block. */
#define SECTOR_INDEX 128
/* Direct blocks. */
#define L0_CACHE_CAPACITY    ((SECTOR_IDXS - 2) * BLOCK_SECTOR_SIZE)
/* Indirect blocks. */
#define L1_CACHE_CAPACITY    (SECTOR_INDEX * BLOCK_SECTOR_SIZE)
/* Doubly indirect blocks. */
#define L2_CACHE_CAPACITY    (SECTOR_INDEX * SECTOR_INDEX * BLOCK_SECTOR_SIZE)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;                     /* First data sector. */
    off_t length;                             /* File size in bytes. */
    unsigned magic;                           /* Magic number. */
    /* Block at index SECTOR_IDXS - 2 is the singly indirect block. 
      Block at index SECTOR_IDXS - 1 is the doubly indirect block. */
    block_sector_t sector_idxs[SECTOR_IDXS];  
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
    struct lock lock_inode;             /* Inode lock */
    block_sector_t inode_disk_sector;   /* Sector that contains inode_disk for inode. */
    bool is_dir;                        /* File is a directory. */
  };


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  ASSERT (inode_disk != NULL);
  /* Direct blocks. */
  if (pos < L0_CACHE_CAPACITY)
  {
    return inode_disk->sector_idxs[pos / BLOCK_SECTOR_SIZE];
  }
  else
  /* Indirect blocks. */
  {
    /* Singly indirect block. */
    if (pos < L0_CACHE_CAPACITY + L1_CACHE_CAPACITY) {
      // Indirect block at index SECTOR_IDXS - 2
      block_sector_t sector = inode_disk->sector_idxs[SECTOR_IDXS - 2];
      struct inode_disk *disk_inode;
      disk_inode = malloc (sizeof *disk_inode);
      if (disk_inode == NULL)
        return NULL;

      cache_read (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
      block_sector_t return_sector = 
            disk_inode->sector_idxs[(pos - L0_CACHE_CAPACITY) / BLOCK_SECTOR_SIZE];
      free (disk_inode);
      return return_sector;
    } 
    /* Doubly indirect block. */
    else {
      // Indirect block at index SECTOR_IDXS - 1
      block_sector_t sector = inode_disk->sector_idxs[SECTOR_IDXS - 1];
      struct inode_disk *disk_inode_doubly;
      disk_inode_doubly = malloc (sizeof *disk_inode_doubly);
      if (disk_inode_doubly == NULL)
        return NULL;

      cache_read (sector, disk_inode_doubly, 0, BLOCK_SECTOR_SIZE);
      off_t doubly_index = (pos - L0_CACHE_CAPACITY - L1_CACHE_CAPACITY) / L1_CACHE_CAPACITY;
      block_sector_t singly_sector = disk_inode_doubly->sector_idxs[doubly_index];

      struct inode_disk *disk_inode;
      disk_inode = malloc (sizeof *disk_inode);
      if (disk_inode == NULL)
        return NULL;

      cache_read (singly_sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
      off_t remaining = pos - L0_CACHE_CAPACITY - L1_CACHE_CAPACITY - 
                              (doubly_index - 1) * L1_CACHE_CAPACITY;
      block_sector_t return_sector = 
            disk_inode->sector_idxs[remaining / BLOCK_SECTOR_SIZE];

      free (disk_inode_doubly);
      free(disk_inode);
      return return_sector;
    }
  }
}

/*
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.sector_idxs[0] + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}
*/

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
//static block_sector_t
block_sector_t
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
      disk_inode->length = 0;

      off_t size_need_to_allocate = length - disk_inode->length;
      off_t size_left_in_block = ROUND_UP (disk_inode->length, BLOCK_SECTOR_SIZE) - disk_inode->length;
      if (size_need_to_allocate <= size_left_in_block)
      {
        // length of file smaller than BLOCK_SECTOR_SIZE
        disk_inode->length = length;
      }
      else
      {
        size_t num_sectors = bytes_to_sectors(length);  // num of sectors needed for LENGTH bytes
        // direct allocate zero sectors
        off_t offset = disk_inode->length;    // start at offset 0
        
        while (offset < length)
        // for (int i = 0; i < num_sectors; i++) {}    -> allocate num_sectors number of sectors for data
        {
          // allocates one block with all zeros at sector number allocated_sector
          block_sector_t allocated_sector = zero_sector ();
          if ((int) allocated_sector == -1) // invalid sector 
            { 
              break;
            }
          //printf("allocated_sector: %d\n", allocated_sector);
          //if ((int) allocated_sector == -1) // invalid sector 
          //{ 
            //break;
          //}
          offset += BLOCK_SECTOR_SIZE;    // don't need this if using for loop
          if (offset <= L0_CACHE_CAPACITY)  
          {
            //printf("Direct block\n");
            // sector_id start from 0, need -1
            off_t sector_id = (offset - 1) / BLOCK_SECTOR_SIZE;
            //printf("sector_id direct: %d\n", sector_id);
            //printf("allocated_sector: %d\n", allocated_sector);
            disk_inode->sector_idxs[sector_id] = allocated_sector;
          }
          else
          {
            /* Singly indirect block. */
            if (offset <= L0_CACHE_CAPACITY + L1_CACHE_CAPACITY) {
              block_sector_t allocated_sector_singly = -1;
              if (disk_inode->sector_idxs[SECTOR_IDXS - 2] != 0) {
                allocated_sector_singly = disk_inode->sector_idxs[SECTOR_IDXS - 2];
              } else {
                allocated_sector_singly = zero_sector ();
                disk_inode->sector_idxs[SECTOR_IDXS - 2] = allocated_sector_singly;
              }
              //disk_inode->sector_idxs[SECTOR_IDXS - 2] = allocated_sector_singly;
              struct inode_disk *disk_inode_singly;
              disk_inode_singly = malloc (sizeof *disk_inode_singly);
              if (disk_inode_singly == NULL)
                return 0;

              cache_read (allocated_sector_singly, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              off_t sector_id = (offset - L0_CACHE_CAPACITY - 1) / BLOCK_SECTOR_SIZE;
              //printf("sector_id singly: %d\n", sector_id);
              if ((int) allocated_sector == -1) // invalid sector 
              { 
                break;
              }
              disk_inode_singly->sector_idxs[sector_id] = allocated_sector;
              cache_write (allocated_sector_singly, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              free (disk_inode_singly);
            } 
            /* Doubly indirect block. */
            else {  
              //printf("Doubly indirect block\n");
              block_sector_t doubly_sector = disk_inode->sector_idxs[SECTOR_IDXS - 1];
              struct inode_disk *disk_inode_doubly;
              disk_inode_doubly = malloc (sizeof *disk_inode_doubly);
              if (disk_inode_doubly == NULL)
                return 0;

              cache_read (doubly_sector, disk_inode_doubly, 0, BLOCK_SECTOR_SIZE);
              off_t doubly_index = (offset - L0_CACHE_CAPACITY - L1_CACHE_CAPACITY - 1) / L1_CACHE_CAPACITY;
              block_sector_t singly_sector = disk_inode_doubly->sector_idxs[doubly_index];

              struct inode_disk *disk_inode_singly;
              disk_inode_singly = malloc (sizeof *disk_inode_singly);
              if (disk_inode_singly == NULL)
                return NULL;

              cache_read (singly_sector, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              off_t remaining = offset - L0_CACHE_CAPACITY - L1_CACHE_CAPACITY - 
                                      (doubly_index - 1) * L1_CACHE_CAPACITY - 1;
              off_t sector_id = remaining / BLOCK_SECTOR_SIZE;
              //printf("allocated_sector: %d\n", allocated_sector);
              if ((int) allocated_sector == -1) // invalid sector 
              { 
                break;
              }
              disk_inode_singly->sector_idxs[sector_id] = allocated_sector;
              cache_write (singly_sector, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              free (disk_inode_doubly);
              free(disk_inode_singly);
            }
          }
        }
        disk_inode->length = length;
      }

      ASSERT (disk_inode->length >= length);
      ASSERT (disk_inode->length - length < BLOCK_SECTOR_SIZE);
      disk_inode->magic = INODE_MAGIC;
      disk_inode->start = sector; // sector that stores disk inode itself
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
    if (sector == ROOT_DIR_SECTOR) {
    }
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
      //printf("size: %d\n", size);
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      //printf("read sector_idx: %d\n", sector_idx);
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
        //printf("prefetch offset %d \n", offset + BLOCK_SECTOR_SIZE);
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
                off_t offset, bool is_bitmap) 
{
  if (inode->is_dir == true) return -1;
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  struct inode_disk *disk_inode;
  disk_inode = malloc (sizeof *disk_inode);
  if (disk_inode == NULL)
    return 0;
  cache_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

  if (inode->deny_write_cnt)
    return 0;

  off_t initial_length = disk_inode->length; // current size of file in inode
  
  if (offset + size > initial_length)
    {
      disk_inode->length = offset + size; 
    }
  //lock_acquire (&inode->lock_inode);
  // Write at location past length of file 
  //bool updated_length = false;
  //if (offset + size > disk_inode->length)
  //{
    // EXTENSION + update length -> no need to return 
    //updated_length = true;
    //disk_inode->length = offset + size; 
    //lock_release (&inode->lock_inode);
    //free (disk_inode);
    //return 0;
  //}
  //else
  //{
    //lock_release (&inode->lock_inode);
  //}
  //lock_acquire (&inode->lock_inode);
  while (size > 0) 
    {
      bool updated_length = false;
      if (offset + size > initial_length)
      {
        updated_length = true;
      }

      /* Sector to write, starting byte offset within sector. */
      //block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      off_t sector_id = (offset) / BLOCK_SECTOR_SIZE; // sector index to be written to
      //printf("sector_id; %d\n", sector_id);
      off_t cur_max_sector_id = (initial_length - 1) / BLOCK_SECTOR_SIZE;  // cur max sector index
      /* Writing to uninitialized sector. */
      if (((sector_id > cur_max_sector_id) && updated_length 
                && !is_bitmap) || ((initial_length == 0))) {
        //lock_release (&inode->lock_inode);
        block_sector_t allocated_sector = zero_sector ();
        //printf("allocated sector: %d\n", allocated_sector);
        //lock_acquire (&inode->lock_inode);
        if ((int) allocated_sector == -1) // invalid sector 
          { 
            lock_release (&inode->lock_inode);
            return 0;
          }
        if (offset < L0_CACHE_CAPACITY)  
          {
            // sector_id start from 0, need -1
            //off_t sector_id = (offset - 1) / BLOCK_SECTOR_SIZE;
            disk_inode->sector_idxs[sector_id] = allocated_sector;
            //printf("sector num at sector_id %d: %d\n", sector_id, disk_inode->sector_idxs[sector_id]);
          }
        else {
          // spill into indirect blocks
          /* Singly indirect block. */
            if (offset < L0_CACHE_CAPACITY + L1_CACHE_CAPACITY) {
              block_sector_t allocated_sector_singly = -1;
              if (disk_inode->sector_idxs[SECTOR_IDXS - 2] != 0) {
                allocated_sector_singly = disk_inode->sector_idxs[SECTOR_IDXS - 2];
              } else {
                allocated_sector_singly = zero_sector ();
                disk_inode->sector_idxs[SECTOR_IDXS - 2] = allocated_sector_singly;
              }
              //disk_inode->sector_idxs[SECTOR_IDXS - 2] = allocated_sector_singly;
              struct inode_disk *disk_inode_singly;
              disk_inode_singly = malloc (sizeof *disk_inode_singly);
              if (disk_inode_singly == NULL)
                return 0;

              cache_read (allocated_sector_singly, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              off_t sector_id = (offset - L0_CACHE_CAPACITY - 1) / BLOCK_SECTOR_SIZE;
              //printf("sector_id singly: %d\n", sector_id);
              if ((int) allocated_sector == -1) // invalid sector 
              { 
                break;
              }
              //printf("sector_id: %d\n", sector_id);
              //sector_id = sector_id - (SECTOR_IDXS - 2);
              //printf("sector_id singly: %d\n", sector_id);
              disk_inode_singly->sector_idxs[sector_id] = allocated_sector;
              //cache_write (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
              cache_write (allocated_sector_singly, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              free (disk_inode_singly);
            } 
            /* Doubly indirect block. */
            else {  
              block_sector_t doubly_sector = disk_inode->sector_idxs[SECTOR_IDXS - 1];
              struct inode_disk *disk_inode_doubly;
              disk_inode_doubly = malloc (sizeof *disk_inode_doubly);
              if (disk_inode_doubly == NULL)
                return 0;

              cache_read (doubly_sector, disk_inode_doubly, 0, BLOCK_SECTOR_SIZE);
              off_t doubly_index = (offset - L0_CACHE_CAPACITY - L1_CACHE_CAPACITY - 1) / L1_CACHE_CAPACITY;
              block_sector_t singly_sector = disk_inode_doubly->sector_idxs[doubly_index];

              struct inode_disk *disk_inode_singly;
              disk_inode_singly = malloc (sizeof *disk_inode_singly);
              if (disk_inode_singly == NULL)
                return NULL;

              cache_read (singly_sector, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              off_t remaining = offset - L0_CACHE_CAPACITY - L1_CACHE_CAPACITY - 
                                      (doubly_index - 1) * L1_CACHE_CAPACITY - 1;
              off_t sector_id = remaining / BLOCK_SECTOR_SIZE;
              block_sector_t allocated_sector = zero_sector ();
              if ((int) allocated_sector == -1) // invalid sector 
              { 
                break;
              }
              disk_inode_singly->sector_idxs[sector_id] = allocated_sector;
              cache_write (singly_sector, disk_inode_singly, 0, BLOCK_SECTOR_SIZE);
              free (disk_inode_doubly);
              free(disk_inode_singly);
            } 
        }
      }
      
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      //printf("sector_idx; %d\n", sector_idx);
      //printf("2: sector num at sector_id %d: %d\n", sector_id, disk_inode->sector_idxs[sector_id]);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
        break;
      }
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      {
        // write full sector
        cache_write (sector_idx, buffer + bytes_written, 0, BLOCK_SECTOR_SIZE);
      }
      else
      {
        // write partial sector 
        cache_write (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //lock_release (&inode->lock_inode);
  // Write updated disk inode back to cache 
  cache_write (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

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

/* Returns true if the file stored at this inode is a directory. */
bool 
inode_is_dir(struct inode *inode)
{
  return inode->is_dir;
}

void
inode_set_dir_status(struct inode *inode, bool dir_status)
{
  inode->is_dir = dir_status;
}

void 
set_inode_length (struct inode *inode, off_t length) {
  struct inode_disk *disk_inode;
  disk_inode = malloc (sizeof *disk_inode);
  if (disk_inode == NULL)
    return NULL;

  cache_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  disk_inode->length = length;
  cache_write (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free (disk_inode);
}