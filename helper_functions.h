/** This file contains all of the helper function definitions used in vsfs.c. */

#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <fuse.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "fs_ctx.h"
#include "options.h"
#include "util.h"
#include "bitmap.h"
#include "map.h"

fs_ctx *get_fs(void);

uint32_t get_path_inode(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, const char *path_name, vsfs_ino_t *ino);

int read_directory_entries(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, void *buf, fuse_fill_dir_t filler);

vsfs_blk_t next_available_dentry(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, uint32_t *directory_array_index_output, uint32_t *index_within_array);

void allocate_bitmap_index(bitmap_t *bitmap, uint32_t size, uint32_t *found_index);

int find_available_entry(uint32_t num_blocks, vsfs_blk_t *dentry_array, uint32_t *block_index);

int add_entry_to_block(vsfs_dentry *add_to_array, uint32_t dentry_array_index, vsfs_inode *new_file_inode, uint32_t inode_index, const char *path_name);

int allocate_block(uint32_t num_blocks, vsfs_blk_t *dentry_array, vsfs_inode *new_file_inode, uint32_t inode_index, const char *path_name);

int allocate_first_indirect_block(vsfs_inode *new_file_inode, uint32_t inode_index, const char *path_name);

uint32_t find_path_data_block(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, const char *path_name, uint32_t *dentry_array_index, uint32_t *index_within_array);

uint32_t get_num_dentries_in_block(vsfs_blk_t *dentry_array, uint32_t num_blocks);

uint32_t unlink_data_blocks(uint32_t num_blocks, vsfs_blk_t *dentry_array);

int unlink_entire_file(vsfs_dentry *path_dentry, vsfs_inode *path_file_inode, uint32_t array_index, uint32_t path_inode_index);

int allocate_empty_file_block(uint32_t num_blocks, vsfs_blk_t *dentry_array, vsfs_inode *path_file_inode);

vsfs_blk_t last_block_in_file(uint32_t num_blocks, vsfs_blk_t *dentry_array);

uint32_t remove_eof(uint32_t array_size, uint32_t num_blocks_to_remove, vsfs_blk_t *dentry_array, vsfs_inode *path_file_inode);