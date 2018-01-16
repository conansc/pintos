#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/directory.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char * path, off_t initial_size, bool is_dir);
struct file *filesys_open (const char *path);
bool filesys_remove (const char *path);
bool filesys_cd(const char* path);
struct dir * tokens_get_directory(char ** tokens, int depth, bool is_root);
char** path_get_tokens(const char * path, int* token_count);
bool path_is_root(const char* path);
bool path_validate(const char* path);
int path_count_tokens(const char * path);
void free_tokens(char** tokens, int token_count);


#endif /* filesys/filesys.h */
