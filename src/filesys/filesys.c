#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"
/* Partition that contains the file system. */
struct block *fs_device;


static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

  /* Project 4: CWD of first thread is by default the root. */
  //thread_current ()->cur_dir = dir_open_root ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_flush ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */

/* Updated to include pathnames as name. */

bool
filesys_create (const char *name, off_t initial_size) 
{
  // For subdirectories: name can be either absolue (/a/b) or relative (a/b)
  bool success = true;
  struct dir *cur_dir;
  char *start = name;
  if (*name == '/') {
    // absolute path -> from root 
    struct dir *dir = dir_open_root ();
    cur_dir = dir;
    start++;  // disregard first slash
  }
  else
  {
    // relative path -> cwd of process 
    cur_dir = thread_current()->cur_dir;
  }
  if (cur_dir == NULL) return 0;

  char *token;
  //char *token = strtok (start, "/");  // p = first subdirectory
  bool previous_existed = false;  // keeping track of when we hit the last subdirectory 
  while (true) {
    token = strtok_r(start, " ", &start);
    struct inode *dir_inode = NULL;
    // search cur_dir for first subdir in pathname NAME
    // look inside cur_dir for file TOKEN and sets its inode at dir_inode 
    bool exists = dir_lookup (cur_dir, token, &dir_inode);
    if (exists) {
      // open next subdirectory
      //token = strtok (NULL, "/");
      if ((token == NULL) && previous_existed) return 0;  // return false if we are creating existing directory (i.e. this is last token)
      //if (token == "") continue; // case of "//"
      dir_close(cur_dir);
      cur_dir = dir_open (dir_inode);
      previous_existed = true;
    } else {
      // create subdirectory or file 
      //token = strtok (NULL, "/");
      // create file/subdirectory and its inode
      if (token == NULL) break;   // the one before was the final token
      block_sector_t inode_sector = zero_sector (); 
      if ((int) inode_sector == -1) return 0;     // invalid allocated sector
      if (!inode_create (inode_sector, initial_size)
          || !dir_add (cur_dir, token, inode_sector)) 
        {
          success = false;
          free_map_release (inode_sector, 1);
        } 
        char this[] = ".";
        char past[] = "..";
        // dir inode stores inode of newly created subdirectory
        struct inode *dir_inode = inode_open (inode_sector);
        struct dir *new_dir = dir_open (dir_inode);
        // add . and .. to newly created subdirectory
        dir_add (new_dir, this, inode_sector);
        dir_add (new_dir, past, get_dir_inode (cur_dir));
        // only entries at creation
        set_inode_length(dir_inode, initial_size);
        //struct dir *dir_temp = cur_dir;
        dir_close(new_dir);
        dir_close(cur_dir);
        // otherwise open the just created subdir and keep on going
        cur_dir = new_dir;
        previous_existed = false;
    }
  }

  //dir_close(cur_dir);
  
  return success;
  /*
  while (offset <= strlen(name)) {
    found = strchr(start + offset, slash);
    offset = found - name;
    if (offset == 1) continue;   // case of "//"
    char subdir[offset + 1];        // name of subdirectory 
    strncpy(subdir, start, offset);    // copy subdirectory into buf
    block_sector_t inode_sector = zero_sector ();   // allocate sector for this subdir
    bool success = (inode_create (inode_sector, BLOCK_SECTOR_SIZE)
                    && dir_add (cur_dir, name, inode_sector));
    if (success == 0) return -1;
    offset = found - name + 1;
    struct inode *dir_inode = inode_open (inode_sector);
    cur_dir = dir_open (dir_inode);

  } 
  */

  /*

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  */
  /*
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
  */
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
