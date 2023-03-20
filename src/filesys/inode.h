#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset, bool is_bitmap);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
void inode_unlock(struct inode *inode);
void inode_lock(struct inode *inode);
void dir_unlock(struct inode *inode);
void dir_lock(struct inode *inode);
bool inode_is_dir(struct inode *inode);
int inode_open_cnt(struct inode *inode);
struct inode *open_inode_find (block_sector_t sector);
void inode_set_dir_status(struct inode *inode, bool dir_status);
block_sector_t zero_sector (void);
void set_inode_length (struct inode *inode, off_t length);
#endif /* filesys/inode.h */