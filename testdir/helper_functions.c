/** This file contains all of the helper functions used in vsfs.c. */
#include "helper_functions.h"

/** Get file system context. */
fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}

/** 
 * Get the inode for the given path if it is in the directory array passed in
 * and return the number of valid data blocks read.
*/
uint32_t get_path_inode(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, const char *path_name, vsfs_ino_t *ino) {
	fs_ctx *fs = get_fs();
	uint32_t array_index = 0;
	uint32_t valid_blocks_found = 0;
	while (array_index < num_blocks) {
		if (directory_entry_array[array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + directory_entry_array[array_index] * VSFS_BLOCK_SIZE);
			for (uint32_t array_entry_index = 0; array_entry_index < fs->num_d_db; ++array_entry_index) {
				vsfs_dentry *curr_array_entry = &block_head[array_entry_index];
				if (curr_array_entry->ino != VSFS_INO_MAX) {
					char *curr_array_entry_name = curr_array_entry->name;
					if (strcmp(curr_array_entry_name, path_name) == 0) {				
						*ino = curr_array_entry->ino;
						return valid_blocks_found;
					}
				}
			}
			valid_blocks_found += 1;
		}
		array_index += 1;
	}
	*ino = VSFS_INO_MAX;
	return valid_blocks_found;
}

/** 
 * Read all of the directory entries in the directory array passed in and
 * return the number of valid data blocks read.
 */
int read_directory_entries(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, void *buf, fuse_fill_dir_t filler) {
	fs_ctx *fs = get_fs();
	uint32_t array_index = 0;
	int valid_blocks_found = 0;
	while (array_index < num_blocks) {
		if (directory_entry_array[array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + directory_entry_array[array_index] * VSFS_BLOCK_SIZE);
			for (uint32_t array_entry_index = 0; array_entry_index < fs->num_d_db; ++array_entry_index) {
				vsfs_dentry *curr_array_entry = &block_head[array_entry_index];
				if (curr_array_entry->ino != VSFS_INO_MAX) {
					uint32_t err = filler(buf, curr_array_entry->name, NULL, 0);
					assert(!err);
					if (err) {
						return -ENOBUFS;
					}
				}
			}
			valid_blocks_found += 1;
		}
		array_index += 1;
	}
	return valid_blocks_found;
}

/** Find and return the next available block number in the root directory. */
vsfs_blk_t next_available_dentry(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, uint32_t *directory_array_index_output, uint32_t *index_within_array) {
	fs_ctx *fs = get_fs();
	uint32_t array_index = 0;
	int valid_blocks_found = 0;
	while (array_index < num_blocks) {
		if (directory_entry_array[array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + directory_entry_array[array_index] * VSFS_BLOCK_SIZE);
			for (uint32_t array_entry_index = 0; array_entry_index < fs->num_d_db; ++array_entry_index) {
				vsfs_dentry *curr_array_entry = &block_head[array_entry_index];
				if (curr_array_entry->ino == VSFS_INO_MAX) {
					*directory_array_index_output = array_index;
					*index_within_array = array_entry_index;
					return valid_blocks_found;
				}
			}
			valid_blocks_found += 1;
		}
		array_index += 1;
	}
	*directory_array_index_output = VSFS_BLK_UNASSIGNED;
	*index_within_array = VSFS_INO_MAX;
	return valid_blocks_found;
}

/** 
 * Check if the input bitmap has any available space, and if so, set found_index to the next available
 * block index from the input bitmap.
 */
void allocate_bitmap_index(bitmap_t *bitmap, uint32_t size, uint32_t *found_index) {
    int err = bitmap_alloc(bitmap, size, found_index);
    assert(!err);
}

/** 
 * Determine if there is any space in the input directory entry array, and if so, set block_index to
 * the next available block index from the directory entry array.
 */
int find_available_entry(uint32_t num_blocks, vsfs_blk_t *dentry_array, uint32_t *block_index) {
    uint32_t available_block_index = VSFS_BLK_UNASSIGNED;
    uint32_t index = 0;
    while (index < num_blocks) {
        if (dentry_array[index] == VSFS_BLK_UNASSIGNED) {
            available_block_index = index;
            break;
        }
        index += 1;
    }
	*block_index = available_block_index;
	if (available_block_index == VSFS_BLK_UNASSIGNED && index == num_blocks) {
		return -1;
	}
	return 0;
}

/** Add the input directory entry to the input directory entry array. */
int add_entry_to_block(vsfs_dentry *add_to_array, uint32_t dentry_array_index, vsfs_inode *new_file_inode, uint32_t inode_index, const char *path_name) {
	fs_ctx *fs = get_fs();
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];

    vsfs_dentry *new_file_dentry = &add_to_array[dentry_array_index];
    new_file_dentry->ino = inode_index;
    strcpy(new_file_dentry->name, path_name);
	root_inode->i_mtime = new_file_inode->i_mtime;
    return 0;
}

/** 
 * Allocate a new direct/indirect block and initialize the directory entry in the first 
 * position in the newly allocated data block with the input file.
 */
int allocate_block(uint32_t num_blocks, vsfs_blk_t *dentry_array, vsfs_inode *new_file_inode, uint32_t inode_index, const char *path_name) {
	fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	bitmap_t *data_bitmap = fs->dbmap;

	uint32_t next_data_bitmap_index;
	allocate_bitmap_index(data_bitmap, superblock->sb_num_blocks, &next_data_bitmap_index);
	superblock->sb_free_blocks -= 1;

	uint32_t next_avail_index = VSFS_BLK_UNASSIGNED;
	int err = find_available_entry(num_blocks, dentry_array, &next_avail_index);

	if (err != -1) {
		dentry_array[next_avail_index] = next_data_bitmap_index;
		vsfs_dentry *add_to_array = (vsfs_dentry *)(fs->image + dentry_array[next_avail_index] * VSFS_BLOCK_SIZE);
		add_entry_to_block(add_to_array, 0, new_file_inode, inode_index, path_name);
		root_inode->i_blocks += 1;
		root_inode->i_size += VSFS_BLOCK_SIZE;
		for (uint32_t i = 1; i < fs->num_d_db; ++i) {
			add_to_array[i].ino = VSFS_INO_MAX;
		}
		return 0;
	}
	return -1;
}

/** 
 * Allocate the first indirect block needed by the file system and initialize the directory
 * entry in the first position in the newly allocated data block with the input file.
 */
int allocate_first_indirect_block(vsfs_inode *new_file_inode, uint32_t inode_index, const char *path_name) {
    fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	bitmap_t *data_bitmap = fs->dbmap;

	uint32_t next_data_bitmap_index;
	allocate_bitmap_index(data_bitmap, superblock->sb_num_blocks, &next_data_bitmap_index);
	superblock->sb_free_blocks -= 1;

	root_inode->i_indirect = next_data_bitmap_index;
	vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + root_inode->i_indirect * VSFS_BLOCK_SIZE);
	memset(indirect_block_number, VSFS_BLK_UNASSIGNED, fs->num_blk_per_b);

	return allocate_block(superblock->sb_num_blocks, indirect_block_number, new_file_inode, inode_index, path_name);
}

/** Finds and returns the block number, index of the input file name, and the number of valid blocks found if it exists. */
uint32_t find_path_data_block(uint32_t num_blocks, vsfs_blk_t *directory_entry_array, const char *path_name, uint32_t *dentry_array_index, uint32_t *index_within_array) {
	fs_ctx *fs = get_fs();
	uint32_t array_index = 0;
	uint32_t valid_blocks_found = 0;
	while (array_index < num_blocks) {
		if (directory_entry_array[array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + directory_entry_array[array_index] * VSFS_BLOCK_SIZE);
			for (uint32_t array_entry_index = 0; array_entry_index < fs->num_d_db; ++array_entry_index) {
				vsfs_dentry *curr_array_entry = &block_head[array_entry_index];
				if (curr_array_entry->ino != VSFS_INO_MAX) {
					char *curr_array_entry_name = curr_array_entry->name;
					if (strcmp(curr_array_entry_name, path_name) == 0) {				
						*dentry_array_index = array_index;
						*index_within_array = array_entry_index;
						return valid_blocks_found;
					}
				}
			}
			valid_blocks_found += 1;
		}
		array_index += 1;
	}
	*dentry_array_index = VSFS_BLK_UNASSIGNED;
	*index_within_array = VSFS_INO_MAX;
	return valid_blocks_found;
}

/** Checks how many directory entries are in the input array */
uint32_t get_num_dentries_in_block(vsfs_blk_t *dentry_array, uint32_t num_blocks) {
	fs_ctx *fs = get_fs();
	uint32_t num_dentries = 0;
	uint32_t dentry_array_index = 0;

	while (dentry_array_index < num_blocks) {
		if (dentry_array[dentry_array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + dentry_array[dentry_array_index] * VSFS_BLOCK_SIZE);
			for (uint32_t i = 0; i < num_blocks; ++i) {
				vsfs_dentry *curr_dentry = &block_head[i];
				if (curr_dentry->ino != VSFS_INO_MAX) {
					num_dentries += 1;
				}
			}
		}
		dentry_array_index += 1;
	}

	return num_dentries;
}

/** Unlinks the data blocks from an inode, both direct and indirect */
uint32_t unlink_data_blocks(uint32_t num_blocks, vsfs_blk_t *dentry_array) {
	fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;
	bitmap_t *data_bitmap = fs->dbmap;

	uint32_t path_array_index = 0;
	uint32_t blocks_freed = 0;
	while (path_array_index < num_blocks) {
		if (dentry_array[path_array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + dentry_array[path_array_index] * VSFS_BLOCK_SIZE);
			for (uint32_t path_array_entry_index = 0; path_array_entry_index < fs->num_d_db; ++path_array_entry_index) {
				vsfs_dentry *curr_array_entry = &block_head[path_array_entry_index];
				if (curr_array_entry->ino != VSFS_INO_MAX) {
					curr_array_entry->ino = VSFS_INO_MAX;
				}
			}
			blocks_freed += 1;
			bitmap_free(data_bitmap, superblock->sb_num_blocks, dentry_array[path_array_index]);
			dentry_array[path_array_index] = VSFS_BLK_UNASSIGNED;
			superblock->sb_free_blocks += 1;
		}
		path_array_index += 1;
	}
	return blocks_freed;
}

/** Unlinks the entire input file */
int unlink_entire_file(vsfs_dentry *path_dentry, vsfs_inode *path_file_inode, uint32_t array_index, uint32_t path_inode_index) {
	fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;
	bitmap_t *inode_bitmap = fs->ibmap;
	bitmap_t *data_bitmap = fs->dbmap;
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];

	path_dentry->ino = VSFS_INO_MAX;
	path_file_inode->i_nlink -= 1;
	bitmap_free(inode_bitmap, div_round_up(VSFS_BLOCK_SIZE, sizeof(vsfs_inode)), path_inode_index);
	superblock->sb_free_inodes += 1;

	if (path_file_inode->i_blocks > 0) {
		uint32_t direct_blocks_freed = unlink_data_blocks(VSFS_NUM_DIRECT, path_file_inode->i_direct);

		if (direct_blocks_freed < path_file_inode->i_blocks && path_file_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
			uint32_t num_indirect_blocks = path_file_inode->i_blocks - direct_blocks_freed;
			vsfs_blk_t *path_indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
			unlink_data_blocks(num_indirect_blocks, path_indirect_block_number);
			
			bitmap_free(data_bitmap, superblock->sb_num_blocks, path_file_inode->i_indirect);
			*path_indirect_block_number = VSFS_BLK_UNASSIGNED;
			superblock->sb_free_blocks += 1;
		}
	}

	if (get_num_dentries_in_block(&(root_inode->i_direct[array_index]), VSFS_NUM_DIRECT) == 0) {
		bitmap_free(data_bitmap, superblock->sb_num_blocks, root_inode->i_direct[array_index]);
		root_inode->i_direct[array_index] = VSFS_BLK_UNASSIGNED;
		root_inode->i_blocks -= 1;
		root_inode->i_size -= VSFS_BLOCK_SIZE;
		superblock->sb_free_blocks += 1;
	}

	if (clock_gettime(CLOCK_REALTIME, &(root_inode->i_mtime)) != 0) {
		perror("clock_gettime");
		return -1;
	}
	return 0;
}

int allocate_empty_file_block(uint32_t num_blocks, vsfs_blk_t *dentry_array, vsfs_inode *path_file_inode) {

	fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;
	bitmap_t *data_bitmap = fs->dbmap;
	
	uint32_t next_avail_index = VSFS_BLK_UNASSIGNED;
	int err = find_available_entry(num_blocks, dentry_array, &next_avail_index);

	if (!err) {
		uint32_t next_data_bitmap_index;
		allocate_bitmap_index(data_bitmap, superblock->sb_num_blocks, &next_data_bitmap_index);
		superblock->sb_free_blocks -= 1;

		dentry_array[next_avail_index] = next_data_bitmap_index;
		vsfs_blk_t *add_to_array = (vsfs_blk_t *)(fs->image + dentry_array[next_avail_index] * VSFS_BLOCK_SIZE);
		memset(add_to_array, VSFS_BLK_UNASSIGNED, VSFS_BLOCK_SIZE);
		path_file_inode->i_blocks += 1;
		path_file_inode->i_size += VSFS_BLOCK_SIZE;
		return 0;
	}
	return -1;
}

uint32_t last_block_in_file(uint32_t num_blocks, vsfs_blk_t *dentry_array) {
	for (uint32_t path_array_index = num_blocks - 1; path_array_index > 0; --path_array_index) {
		if (dentry_array[path_array_index] != VSFS_BLK_UNASSIGNED) {
			return path_array_index;
		}
	}
	return VSFS_INO_MAX;
}

uint32_t remove_eof(uint32_t array_size, uint32_t num_blocks_to_remove, vsfs_blk_t *dentry_array, vsfs_inode *path_file_inode) {
	fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;
	bitmap_t *data_bitmap = fs->dbmap;

	uint32_t path_array_index = last_block_in_file(array_size, dentry_array);
	assert(path_array_index != VSFS_INO_MAX);
	uint32_t blocks_freed = 0;
	while (path_array_index < array_size) {
		if (dentry_array[path_array_index] != VSFS_BLK_UNASSIGNED) {
			vsfs_dentry *block_head = (vsfs_dentry *)(fs->image + dentry_array[path_array_index] * VSFS_BLOCK_SIZE);
			memset(block_head, VSFS_BLK_UNASSIGNED, VSFS_BLOCK_SIZE);
			blocks_freed += 1;
			bitmap_free(data_bitmap, superblock->sb_num_blocks, dentry_array[path_array_index]);
			dentry_array[path_array_index] = VSFS_BLK_UNASSIGNED;
			superblock->sb_free_blocks += 1;

			path_file_inode->i_blocks -= 1;
			path_file_inode->i_size -= VSFS_BLOCK_SIZE;

			if (blocks_freed == num_blocks_to_remove) {
				return blocks_freed;
			}
		}
		path_array_index -= 1;
	}
	return blocks_freed;
}