#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module and the cache.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  cache_complete_flush(true);
  clear_read_ahead();
  free_map_close ();
}

/* Creates a file or directory (IS_DIR) named 
   NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file/folder named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char * path, off_t initial_size, bool is_dir)
{
  /* Checks if path is a valid string. */
  if(!path_validate(path))
    return false;

  /* Get tokens from given path */
  int token_count;
  char** tokens = path_get_tokens(path, &token_count);

  /* Parse the path to a directory and check if it's valid. */
  struct dir * curr_directory = tokens_get_directory(tokens, token_count-1, path_is_root(path));
  if(!curr_directory)
  {
    free_tokens(tokens, token_count);
    return false;
  }

  /* Checks if path has at least one valid directory/file token and 
     it isn't the parent directory symbol. */
  if(token_count<=0 || strcmp(tokens[token_count-1],"..")==0)
  {
    dir_close (curr_directory);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Tries to allocate a free disk block for the meta data of the 
     file. If it fails free all resources and return false. */
  block_sector_t sector = 0;
  if(!free_map_allocate (1, &sector))
  {
    dir_close (curr_directory);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Checks that sector is not zero. */
  if(sector == 0)
  {
    dir_close (curr_directory);
    free_map_release (sector, 1);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Try to create the respective inode. */
  if(!inode_create (sector, initial_size, is_dir))
  {
    dir_close (curr_directory);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Add inode/file to the directory. */
  if(!dir_add (curr_directory, tokens[token_count-1], sector))
  {
    dir_close (curr_directory);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Close directory and free resources */
  dir_close (curr_directory);
  free_tokens(tokens, token_count);

  return true;
}

/* Opens the file/folder with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file/folder named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{

  /* Check that path is not corrupted. */
  if(!path_validate(path))
    return NULL;

  /* Get tokens from given path. */
  int token_count;
  char** tokens = path_get_tokens(path, &token_count);

  /* Get directory where we want to open the file from */
  struct dir * curr_directory = tokens_get_directory(tokens, token_count-1, path_is_root(path));
  if(!curr_directory)
  {
    free_tokens(tokens, token_count);
    return NULL;
  }

  /* Check that we do not want to open '..'. If not, look up file/folder
     and write its inode into inode. If so, we return the folder
     where we wanted to open the file from. */
  struct inode *inode = NULL;
  if(token_count>0 && strcmp(tokens[token_count-1],"..")!=0)
  {
    dir_lookup (curr_directory, tokens[token_count-1], &inode);
    dir_close (curr_directory);
  }
  else
  {
    inode = dir_get_inode (curr_directory);
  }

  /* Check that inode could be found */
  if (inode == NULL)
  {
    free_tokens(tokens, token_count);
    return NULL;
  }


  /* Make sure that file/folder was not removed. Needs to be checked
     in case that the cwd was already removed. */
  if (inode_is_removed(inode))
  {
    inode_close(inode);
    free_tokens(tokens, token_count);
    return NULL;
  }

  /* Free resources */
  free_tokens(tokens, token_count);
  return file_open(inode);
}

/* Deletes the file or directory named NAME.
   Returns true if successful, false on failure.
   Fails if no file/folder named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char *path)
{

  /* Check that path is not corrupted */
  if(!path_validate(path))
    return false;

  /* Get tokens from given path */
  int token_count;
  char** tokens = path_get_tokens(path, &token_count);

  /* Checks if the path is the root path, which mustn't be removed. */
  if(path_is_root(path) && strlen(path)==1)
    return false;

  /* Get directory where we want to remove the file from */
  struct dir * curr_directory = tokens_get_directory(tokens, token_count-1, path_is_root(path));
  if(!curr_directory)
  {
    free_tokens(tokens, token_count);
    return false;
  }

  /* Make sure we do not want to remove '..' */
  if(token_count>0 && strcmp(tokens[token_count-1],"..")==0)
  {
    dir_close(curr_directory);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Tries to remove the directory/file. */
  if(!dir_remove(curr_directory, tokens[token_count-1]))
  {
    dir_close(curr_directory);
    free_tokens(tokens, token_count);
    return false;
  }

  /* Close directory and free resources */
  dir_close(curr_directory);
  free_tokens(tokens, token_count);
  return true;
}

/* Changes the current working directory of the current thread given
   the argument path. Returns true if the change was successful, false
   otherwise. */
bool
filesys_cd(const char* path)
{

  /* Check that path is not corrupted */
  if(!path_validate(path))
    return false;

  /* Get tokens from given path */
  int token_count;
  char** tokens = path_get_tokens(path, &token_count);

  /* Get directory where we want to change to cwd to */
  struct dir * new_directory = tokens_get_directory(tokens, token_count, path_is_root(path));
  if(!new_directory)
  {
    free_tokens(tokens, token_count);
    return false;
  }

  /* Close curren cwd, set the new one and free resources */
  dir_close(thread_current()->cwd);

  /* Set the current thread's cwd to the new directory */
  thread_current()->cwd = new_directory;
  free_tokens(tokens, token_count);
  return true;

}

/* Iterates over the token array TOKENS for DEPTH iterations and extracts the directory
   at this point. If IS_ROOT is true, start traversing from the root directory, otherwise
   from the thread's cwd. */
struct dir * tokens_get_directory(char ** tokens, int depth, bool is_root)
{

  struct thread * curr_thread = thread_current();
  struct dir * curr_directory;

  if(is_root || !curr_thread->cwd)
    curr_directory = dir_open_root();
  else
    curr_directory = dir_reopen(curr_thread->cwd);

  int i;
  struct inode * curr_inode;
  char * curr_token;
  struct dir * next_directory;
  for(i=0;i<depth;i++)
  {

    curr_token = tokens[i];

    if (strcmp(curr_token, ".") == 0)
      continue;

    if(strcmp(curr_token, "..") == 0)
    {
      if(!dir_parent(curr_directory, &curr_inode)) {
        dir_close(curr_directory);
        return NULL;
      }
    }
    else
    {
      if(!dir_lookup(curr_directory, curr_token, &curr_inode)) {
        dir_close(curr_directory);
        return NULL;
      }
    }

    if(!inode_is_directory(curr_inode))
    {
      inode_close(curr_inode);
      return NULL;
    }

    next_directory = dir_open(curr_inode);
    if(!next_directory)
    {
      dir_close(curr_directory);
      return NULL;
    }

    dir_close(curr_directory);
    curr_directory = next_directory;

  }

  if(inode_is_removed(dir_get_inode(curr_directory)))
  {
    dir_close(curr_directory);
    return NULL;
  }

  return curr_directory;
}

/* Iterates with strtok_r() over the PATH string and stores each single
   token into the char* array tokens, while incrementing TOKEN_COUNT for
   each found token. */
char** path_get_tokens(const char * path, int* token_count)
{

  int tc = path_count_tokens(path);
  char** tokens = (char**) malloc(tc * sizeof(char*));
  *token_count = tc;

  unsigned path_length = strlen(path) + 1;
  char copied_path[path_length];
  memcpy(copied_path, path, path_length);

  char* curr_token;
  char* save_ptr;
  unsigned curr_pos = 0;
  for (curr_token = strtok_r (copied_path, "/", &save_ptr); curr_token != NULL; curr_token = strtok_r (NULL, "/", &save_ptr))
  {
    if(strcmp(curr_token,".")==0)
      continue;
    tokens[curr_pos] = malloc(strlen(curr_token)+1);
    memcpy(tokens[curr_pos], curr_token, strlen(curr_token)+1);
    ++curr_pos;
  }

  return tokens;

}

/* Checks if PATH is an absolute path. */
bool path_is_root(const char* path)
{
  if(path[0] == 47)
    return true;
  return false;
}

/* Checks if PATH is corrupted or valid. */
bool path_validate(const char* path)
{
  if(!path)
    return false;

  if(strlen(path) == 0)
    return false;

  return true;
}

/* Iterates with strtok_r() over PATH and returns how many tokens
   were in it. */
int path_count_tokens(const char * path)
{
  unsigned path_length = strlen(path) + 1;
  char copied_path[path_length];
  memcpy(copied_path, path, path_length);

  int directories_count = 0;
  char * curr_token;
  char * save_ptr;
  for (curr_token = strtok_r (copied_path, "/", &save_ptr); curr_token != NULL; curr_token = strtok_r (NULL, "/", &save_ptr))
  {
    /* Ignore "." since it isn't important for the actual path. */
    if(strcmp(curr_token,".")==0)
      continue;
    directories_count++;
  }

  return directories_count;
}

/* Free the char* array TOKENS until a given TOKEN_COUNT. */
void free_tokens(char** tokens, int token_count)
{
  int i;
  for(i=0;i<token_count;i++)
    free(tokens[i]);
  free(tokens);
}


/* Formats the file system. */
static void
do_format (void)
{
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
}
