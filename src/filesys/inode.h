#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

struct bitmap;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
/* Amount of the direct block pointers */
#define DIRECT_BLOCKS_COUNT 12
/* Computes the amount of bytes that we have to fill into the inode_disk so
   that it has the exact size of 512 bytes. */
#define FILL_QUANTITY (124 - (DIRECT_BLOCKS_COUNT + 1 + 1))
/* Amount of blocks per indirect blocks, which is in our case 128, because
   BLOCK_SECTOR_SIZE is set to 512. */
#define BLOCKS_PER_INDIRECT_BLOCK (BLOCK_SECTOR_SIZE / 4)
/* The maximum file size that we can store into an inode. */
#define MAX_FILE_SIZE (BLOCK_SECTOR_SIZE * (DIRECT_BLOCKS_COUNT + BLOCKS_PER_INDIRECT_BLOCK + BLOCKS_PER_INDIRECT_BLOCK * BLOCKS_PER_INDIRECT_BLOCK))


/* Struct that stores the different block pointers */
struct inode_blocks
  {
    block_sector_t direct_block_sectors[DIRECT_BLOCKS_COUNT]; /* Pointers to direct blocks */
    block_sector_t indirect_block_sector; /* Pointers to indirect blocks */
    block_sector_t doubly_indirect_block_sector; /* Pointers to doubly-indirect blocks */
  };

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extend_lock;            /* Lock used for locking during extension. */
    struct lock lock;					/* Lock used for locking access to members. */
    off_t read_length;					/* Length of actual written data */
    block_sector_t parent;              /* Stores the parent of this folder(set if folder) */
    off_t length;                       /* File size in bytes. */
	  bool is_dir;						/* Stores if this inode is a directory */
    struct inode_blocks blocks;			/* Inode_blocks struct which holds the blocks of this inode */
  };

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
	  block_sector_t parent;          	/* Stores the parent of this folder(set if folder) */
    off_t length;                       /* File size in bytes. */
	  bool is_dir;						/* Stores if this inode is a directory */
    struct inode_blocks blocks;			/* Inode_blocks struct which holds the blocks of this inode */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[FILL_QUANTITY];     /* Fill struct until it has the size BLOCK_SECTOR_SIZE */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
block_sector_t get_block_for_index(struct inode_blocks *, unsigned);
bool disk_inode_allocate(struct inode_disk *, unsigned);
bool inode_blocks_extend(struct inode_blocks *, unsigned, unsigned);
bool indirect_block_extend(block_sector_t, int * );
bool doubly_indirect_block_extend(block_sector_t, int *);
bool inode_blocks_deallocate(struct inode_blocks *, int);
void indirect_block_deallocate(block_sector_t, int *);
void doubly_indirect_block_deallocate(block_sector_t, int *);
void cache_remove_sector(block_sector_t);
block_sector_t inode_get_parent(struct inode *);
bool inode_add_parent(block_sector_t, block_sector_t);
bool inode_is_directory(struct inode *);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (struct inode *);
off_t inode_read_length (struct inode *);
block_sector_t inode_get_inumber (struct inode *);
int inode_decrement_open_cnt(struct inode *);
int inode_open_cnt(struct inode *);
bool inode_is_removed(struct inode *);

#endif /* filesys/inode.h */
