/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2022 Angela Demke Brown
 */

/**
 * CSC369 Assignment 4 - vsfs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "vsfs.h"
#include "fs_ctx.h"
#include "options.h"
#include "util.h"
#include "bitmap.h"
#include "map.h"
#include "helper_functions.h"

//NOTE: All path arguments are absolute paths within the vsfs file system and
// start with a '/' that corresponds to the vsfs root directory.
//
// For example, if vsfs is mounted at "/tmp/my_userid", the path to a
// file at "/tmp/my_userid/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "/tmp/my_userid/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool vsfs_init(fs_ctx *fs, vsfs_opts *opts)
{
	size_t size;
	void *image;
	
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	// Map the disk image file into memory
	image = map_file(opts->img_path, VSFS_BLOCK_SIZE, &size);
	if (image == NULL) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in vsfs_init().
 */
static void vsfs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
static int path_lookup(const char *path, vsfs_ino_t *ino) {
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOSYS;
	} 

	// Complete this function and any helper functions
	if (strcmp(path, "/") == 0) {
		*ino = VSFS_ROOT_INO;
		return 0;
	}
	fs_ctx *fs = get_fs();
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	const char *path_name = path + 1;

	uint32_t valid_direct_found = get_path_inode(VSFS_NUM_DIRECT, root_inode->i_direct, path_name, ino);
	if (*ino != VSFS_INO_MAX) {
		return 0;
	}

	if (root_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
		uint32_t num_indirect_blocks = root_inode->i_blocks - valid_direct_found;
		vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + root_inode->i_indirect * VSFS_BLOCK_SIZE);
		get_path_inode(num_indirect_blocks, indirect_block_number, path_name, ino);
		if (*ino != VSFS_INO_MAX) {
			return 0;
		}
	}

	return -1;
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int vsfs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb; /* Get ptr to superblock from context */
	
	memset(st, 0, sizeof(*st));
	st->f_bsize   = VSFS_BLOCK_SIZE;   /* Filesystem block size */
	st->f_frsize  = VSFS_BLOCK_SIZE;   /* Fragment size */
	// The rest of required fields are filled based on the information 
	// stored in the superblock.
        st->f_blocks = sb->sb_num_blocks;     /* Size of fs in f_frsize units */
        st->f_bfree  = sb->sb_free_blocks;    /* Number of free blocks */
        st->f_bavail = sb->sb_free_blocks;    /* Free blocks for unpriv users */
		st->f_files  = sb->sb_num_inodes;     /* Number of inodes */
        st->f_ffree  = sb->sb_free_inodes;    /* Number of free inodes */
        st->f_favail = sb->sb_free_inodes;    /* Free inodes for unpriv users */

	st->f_namemax = VSFS_NAME_MAX;     /* Maximum filename length */

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode (for vsfs, that is the indirect block). 
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int vsfs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= VSFS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	// Lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	vsfs_ino_t inode_index_for_given_path;
	vsfs_inode *itable = fs->itable;
	if (path_lookup(path, &inode_index_for_given_path) == 0) {
		vsfs_inode *inode_for_given_path = &itable[inode_index_for_given_path];
		st->st_ino = inode_index_for_given_path;
		st->st_mode = inode_for_given_path->i_mode;
		st->st_nlink = inode_for_given_path->i_nlink;
		st->st_size = inode_for_given_path->i_size;
		st->st_blocks = (inode_for_given_path->i_blocks * VSFS_BLOCK_SIZE) / 512;
		if (inode_for_given_path->i_indirect != VSFS_BLK_UNASSIGNED) {
			st->st_blocks += VSFS_BLOCK_SIZE / 512;
		}
		st->st_mtim = inode_for_given_path->i_mtime;
		return 0;
	} 
	else {
		return -ENOENT;
	}
	return -ENOSYS;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int vsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	assert(strcmp(path, "/") == 0);
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];

	int valid_direct_found = read_directory_entries(VSFS_NUM_DIRECT, root_inode->i_direct, buf, filler);
	if (valid_direct_found == -ENOBUFS) {
		return -ENOMEM;
	}

	if (root_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
		uint32_t num_indirect_blocks = root_inode->i_blocks - valid_direct_found;
		vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + root_inode->i_indirect * VSFS_BLOCK_SIZE);
		int valid_indirect_found = read_directory_entries(num_indirect_blocks, indirect_block_number, buf, filler);
		if (valid_indirect_found == -ENOBUFS) {
			return -ENOMEM;
		}
	}
	
	return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int vsfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();
	vsfs_superblock *superblock = fs->sb;

	// Create a file at the given path with the given mode
	(void)path;
	(void)mode;

	// Check if there is space in the file system for a new file
	if (superblock->sb_free_inodes == 0 || superblock->sb_free_blocks == 0) {
		return -ENOSPC;
	}

	// First allocate space in the inode bitmap for the new file
	bitmap_t *inode_bitmap = fs->ibmap;
	uint32_t next_inode_bitmap_index;
	allocate_bitmap_index(inode_bitmap, superblock->sb_num_inodes, &next_inode_bitmap_index);
	superblock->sb_free_inodes -= 1;

	// Now create and initialize the fields of the new file as a vsfs_inode
	vsfs_inode *itable = fs->itable;
	vsfs_inode *new_file_inode = &itable[next_inode_bitmap_index];
	new_file_inode->i_mode = mode;
	new_file_inode->i_nlink = 1;
	new_file_inode->i_blocks = 0;
	new_file_inode->i_size = 0;
	memset(new_file_inode->i_direct, VSFS_BLK_UNASSIGNED, VSFS_NUM_DIRECT * sizeof(vsfs_blk_t));
	if (clock_gettime(CLOCK_REALTIME, &(new_file_inode->i_mtime)) != 0) {
		perror("clock_gettime");
		return -ENOSYS;
	}

	// Next, get the root inode's values
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	const char *path_name = path + 1;

	// Now, check if the root_inode->i_direct array has any space for new directory entries
	// use bitmap_alloc on the data bitmap to check for this instead of what im currently doing
	uint32_t direct_array_index;
	uint32_t index_within_direct_array;
	uint32_t valid_direct_found = next_available_dentry(VSFS_NUM_DIRECT, root_inode->i_direct, &direct_array_index, &index_within_direct_array);
	if (valid_direct_found < VSFS_NUM_DIRECT) {
		// If next_available_dentry was unable to find any space in the currently allocated direct blocks,
		// and there is space for more direct blocks, then we allocate a new direct block and initialize
		// the first directory entry value in this block to be the new file
		if (direct_array_index == VSFS_BLK_UNASSIGNED && index_within_direct_array == VSFS_INO_MAX) {
			return allocate_block(superblock->sb_num_blocks, root_inode->i_direct, new_file_inode, next_inode_bitmap_index, path_name);
		}
		// If there is space available in the currently allocated direct blocks, then we will simply
		// add this new file to the tail of the directory entries
		else {
			vsfs_dentry *add_to_direct_array = (vsfs_dentry *)(fs->image + root_inode->i_direct[direct_array_index] * VSFS_BLOCK_SIZE);
			return add_entry_to_block(add_to_direct_array, index_within_direct_array, new_file_inode, next_inode_bitmap_index, path_name);
		}
	}

	// If all of the direct blocks are full and the indirect block doesn't exist,
	// we will need to allocate the indirect block and put the new file's entry there
	if (valid_direct_found == VSFS_NUM_DIRECT && root_inode->i_indirect == VSFS_BLK_UNASSIGNED) {
		return allocate_first_indirect_block(new_file_inode, next_inode_bitmap_index, path_name);
	}

	// Since we did not find any available space in the root inode's direct blocks
	// and the indirect block exists, we will now check the indirect blocks for space
	if (root_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
		uint32_t num_indirect_blocks = root_inode->i_blocks - valid_direct_found;
		vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + root_inode->i_indirect * VSFS_BLOCK_SIZE);
		uint32_t indirect_array_index;
		uint32_t index_within_indirect_array;
		next_available_dentry(num_indirect_blocks, indirect_block_number, &indirect_array_index, &index_within_indirect_array);
		// If next_available_dentry was unable to find any space in the currently allocated indirect
		// blocks, then we allocate a new indirect block and initialize the first directory entry 
		// value in this block to be the new file
		if (indirect_array_index == VSFS_BLK_UNASSIGNED && index_within_indirect_array == VSFS_INO_MAX) {
			return allocate_block(superblock->sb_num_blocks, indirect_block_number, new_file_inode, next_inode_bitmap_index, path_name);
		}
		// If there is space available in the currently allocated direct blocks, then we will simply
		// add this new file to the tail of the directory entries
		else {
			vsfs_dentry *add_to_indirect_array = (vsfs_dentry *)(fs->image + indirect_block_number[indirect_array_index] * VSFS_BLOCK_SIZE);
			return add_entry_to_block(add_to_indirect_array, index_within_indirect_array, new_file_inode, next_inode_bitmap_index, path_name);
		}
	}

	return -ENOSYS;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	// Remove the file at given path

	// First get the index of the input path from the inode table
	vsfs_ino_t path_inode_index;
	int err = path_lookup(path, &path_inode_index);
	assert(!err);

	// Create some global variables and get the inode for the input path
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	vsfs_inode *path_file_inode = &itable[path_inode_index];
	const char *path_name = path + 1;

	// Check to see whether the inode for the input path is in the direct data block array
	// of the root inode
	uint32_t direct_array_index;
	uint32_t index_within_direct_array;
	uint32_t valid_direct_found = find_path_data_block(VSFS_NUM_DIRECT, root_inode->i_direct, path_name, &direct_array_index, &index_within_direct_array);
	// If it is not in the direct data block array of the root inode, we must check the indirect
	// data block array of the root inode if it exists
	if (direct_array_index == VSFS_BLK_UNASSIGNED && index_within_direct_array == VSFS_INO_MAX) {
		if (root_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
			uint32_t num_indirect_blocks = root_inode->i_blocks - valid_direct_found;
			vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + root_inode->i_indirect * VSFS_BLOCK_SIZE);
			uint32_t indirect_array_index;
			uint32_t index_within_indirect_array;
			find_path_data_block(num_indirect_blocks, indirect_block_number, path_name, &indirect_array_index, &index_within_indirect_array);
			
			// If the inode for the input path is in the indirect data block array,
			// then we can unlink it from the rest of the file system
			if (indirect_array_index != VSFS_BLK_UNASSIGNED && index_within_indirect_array != VSFS_INO_MAX) {
				vsfs_dentry *indirect_array = (vsfs_dentry *)(fs->image + indirect_block_number[indirect_array_index] * VSFS_BLOCK_SIZE);
				vsfs_dentry *path_dentry = &indirect_array[index_within_indirect_array];
				return unlink_entire_file(path_dentry, path_file_inode, indirect_array_index, path_inode_index);
			}
		}
	}
	else {
		// If the inode for the input path is in the direct data block array of the root inode,
		// we can unlink it from the rest of the file system
		vsfs_dentry *direct_array = (vsfs_dentry *)(fs->image + root_inode->i_direct[direct_array_index] * VSFS_BLOCK_SIZE);
		vsfs_dentry *path_dentry = &direct_array[index_within_direct_array];
		return unlink_entire_file(path_dentry, path_file_inode, direct_array_index, path_inode_index);
	}

	return -ENOSYS;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int vsfs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();
	vsfs_inode *ino = NULL;
	
	// Update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	
	// 0. Check if there is actually anything to be done.
	if (times[1].tv_nsec == UTIME_OMIT) {
		// Nothing to do.
		return 0;
	}

	// 1. Find the inode for the final component in path
	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	const char *path_name = path + 1;
	vsfs_ino_t inode_num;

	uint32_t valid_direct_found = get_path_inode(VSFS_NUM_DIRECT, root_inode->i_direct, path_name, &inode_num);
	if (inode_num == VSFS_INO_MAX) {
		if (root_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
			uint32_t num_indirect_blocks = root_inode->i_blocks - valid_direct_found;
			vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + root_inode->i_indirect * VSFS_BLOCK_SIZE);
			get_path_inode(num_indirect_blocks, indirect_block_number, path_name, &inode_num);
			if (inode_num == VSFS_INO_MAX) {
				return -1;
			}
		}
	}
	ino = &itable[inode_num];
	
	// 2. Update the mtime for that inode.
	//    This code is commented out to avoid failure until you have set
	//    'ino' to point to the inode structure for the inode to update.
	if (times[1].tv_nsec == UTIME_NOW) {
		if (clock_gettime(CLOCK_REALTIME, &(ino->i_mtime)) != 0) {
			// clock_gettime should not fail, unless you give it a
			// bad pointer to a timespec.
			assert(false);
		}
	} else {
		ino->i_mtime = times[1];
	}

	//return 0;
	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size. 
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int vsfs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	vsfs_ino_t path_inode_index;
	int err = path_lookup(path, &path_inode_index);
	assert(!err);

	vsfs_superblock *superblock = fs->sb;
	bitmap_t *data_bitmap = fs->dbmap;
	vsfs_inode *itable = fs->itable;
	vsfs_inode *path_file_inode = &itable[path_inode_index];

	if (path_file_inode->i_blocks == div_round_up(size, VSFS_BLOCK_SIZE)) {
		path_file_inode->i_size = size;
		return 0;
	}

	if (path_file_inode->i_blocks < div_round_up(size, VSFS_BLOCK_SIZE)) {
		if (superblock->sb_free_blocks < (div_round_up(size, VSFS_BLOCK_SIZE) - path_file_inode->i_blocks)) {
			return -ENOSPC;
		}
		else {
			uint32_t num_blocks_to_add = div_round_up(size, VSFS_BLOCK_SIZE) - path_file_inode->i_blocks;
			uint32_t valid_direct_found = 0;
			for (uint32_t direct_index = 0; direct_index < VSFS_NUM_DIRECT; ++direct_index) {
				if (path_file_inode->i_direct[direct_index] != VSFS_BLK_UNASSIGNED) {
					valid_direct_found += 1;
				}
			}

			if (valid_direct_found < VSFS_NUM_DIRECT) {
				uint32_t direct_to_add = num_blocks_to_add;
				uint32_t direct_added = 0;
				while (direct_added < direct_to_add) {
					int err = allocate_empty_file_block(VSFS_NUM_DIRECT, path_file_inode->i_direct, path_file_inode);
					if (!err) {
						direct_added += 1;
					}
					else {
						break;
					}
				}

				if (direct_added < num_blocks_to_add) {
					if (path_file_inode->i_indirect == VSFS_BLK_UNASSIGNED) {
						uint32_t next_data_bitmap_index;
						allocate_bitmap_index(data_bitmap, superblock->sb_num_blocks, &next_data_bitmap_index);
						superblock->sb_free_blocks -= 1;

						path_file_inode->i_indirect = next_data_bitmap_index;
						vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
						memset(indirect_block_number, VSFS_BLK_UNASSIGNED, VSFS_BLOCK_SIZE);
					}
					uint32_t indirect_to_add = num_blocks_to_add - direct_added;
					vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
					for (uint32_t indirect_added = 0; indirect_added < indirect_to_add; ++indirect_added) {
						allocate_empty_file_block(fs->num_blk_per_b, indirect_block_number, path_file_inode);
					}
				}
				if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
					perror("clock_gettime");
					return -ENOSYS;
				}
				path_file_inode->i_size = size;
				return 0;
			}
			else {
				if (path_file_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
					uint32_t indirect_to_add = num_blocks_to_add;
					vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
					for (uint32_t indirect_added = 0; indirect_added < indirect_to_add; ++indirect_added) {
						allocate_empty_file_block(fs->num_blk_per_b, indirect_block_number, path_file_inode);
					}
					if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
						perror("clock_gettime");
						return -ENOSYS;
					}
					path_file_inode += 1;
					path_file_inode->i_size = size;
					return 0;
				}
			}
		}
	}
	else {
		uint32_t num_blocks_to_remove = path_file_inode->i_blocks - div_round_up(size, VSFS_BLOCK_SIZE);
		if (path_file_inode->i_indirect != VSFS_BLK_UNASSIGNED) {
			vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
			uint32_t indirect_blocks_removed = remove_eof(fs->num_blk_per_b, num_blocks_to_remove, indirect_block_number, path_file_inode);

			uint32_t valid_indirect_found = 0;
			for (uint32_t indirect_index = 0; indirect_index < fs->num_blk_per_b; ++indirect_index) {
				if (indirect_block_number[indirect_index] != VSFS_BLK_UNASSIGNED) {
					valid_indirect_found += 1;
				}
			}

			if (valid_indirect_found == 0) {
				bitmap_free(data_bitmap, superblock->sb_num_blocks, path_file_inode->i_indirect);
				path_file_inode->i_indirect = VSFS_BLK_UNASSIGNED;
				superblock->sb_free_blocks += 1;
			}

			if (indirect_blocks_removed < num_blocks_to_remove) {
				uint32_t direct_to_remove = num_blocks_to_remove - indirect_blocks_removed;
				uint32_t direct_blocks_removed = remove_eof(VSFS_NUM_DIRECT, direct_to_remove, path_file_inode->i_direct, path_file_inode);
				if (indirect_blocks_removed + direct_blocks_removed == num_blocks_to_remove) {
					if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
						perror("clock_gettime");
						return -ENOSYS;
					}
					path_file_inode->i_size = size;
					return 0;
				}
			}
			else {
				if (indirect_blocks_removed == num_blocks_to_remove) {
					if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
						perror("clock_gettime");
						return -ENOSYS;
					}
					path_file_inode->i_size = size;
					return 0;
				}
			}
		}
		else {
			uint32_t direct_to_remove = num_blocks_to_remove;
			uint32_t direct_blocks_removed = remove_eof(VSFS_NUM_DIRECT, direct_to_remove, path_file_inode->i_direct, path_file_inode);
			if (direct_blocks_removed == num_blocks_to_remove) {
				if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
					perror("clock_gettime");
					return -ENOSYS;
				}
				path_file_inode->i_size = size;
				return 0;
			}
		}
	}

	return -ENOSYS;
}

/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int vsfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	// Read data from the file at given offset into the buffer

	vsfs_ino_t path_inode_index;
	int err = path_lookup(path, &path_inode_index);
	assert(!err);

	vsfs_inode *itable = fs->itable;
	vsfs_inode *path_file_inode = &itable[path_inode_index];
	uint32_t offset_block = div_round_up(offset, VSFS_BLOCK_SIZE);
	size_t size_read = size;

	if ((long int)path_file_inode->i_size < offset) {
		return 0;
	}

	if (path_file_inode->i_size < offset + size) {
		size_read = path_file_inode->i_size - offset;
	}

	if (offset_block < VSFS_NUM_DIRECT) {
		char *read_portion = (char *)(fs->image + path_file_inode->i_direct[offset_block] * VSFS_BLOCK_SIZE + (offset % VSFS_BLOCK_SIZE));
		memcpy(buf, read_portion, size_read);
		return (int)size_read;
	}
	else {
		vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
		offset_block -= VSFS_NUM_DIRECT;
		char *read_portion = (char *)(fs->image + indirect_block_number[offset_block] * VSFS_BLOCK_SIZE + (offset % VSFS_BLOCK_SIZE));
		memcpy(buf, read_portion, size_read);
		return (int)size_read;
	}

	return -ENOSYS;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size 
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int vsfs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	// Write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	
	vsfs_ino_t path_inode_index;
	int err = path_lookup(path, &path_inode_index);
	assert(!err);

	vsfs_inode *itable = fs->itable;
	vsfs_inode *root_inode = &itable[VSFS_ROOT_INO];
	vsfs_inode *path_file_inode = &itable[path_inode_index];
	uint32_t offset_block = div_round_up(offset, VSFS_BLOCK_SIZE);
	size_t size_write = size;

	if (path_file_inode->i_size < offset + size) {
		off_t new_size = path_file_inode->i_size + offset + size;
		int err = vsfs_truncate(path, new_size);
		if (err != 0) {
			return err;
		}

		if (offset_block < VSFS_NUM_DIRECT) {
			vsfs_dentry *write_location = (vsfs_dentry *)(fs->image + path_file_inode->i_direct[offset_block] * VSFS_BLOCK_SIZE + (offset % VSFS_BLOCK_SIZE));
			if (write_location->ino != VSFS_BLK_UNASSIGNED) {
				memset(write_location, VSFS_BLK_UNASSIGNED, strlen(write_location->name)); // maybe do memset(write_location, VSFS_BLK_UNASSIGNED, strlen(write_location->name));
			}
			write_location->ino = path_inode_index;
			memcpy(write_location, buf, size_write);
			if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
				perror("clock_gettime");
				return -ENOSYS;
			}
			root_inode->i_mtime = path_file_inode->i_mtime;
			return (int)size_write;
		}
		else {
			vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE + (offset % VSFS_BLOCK_SIZE));
			offset_block -= VSFS_NUM_DIRECT;
			vsfs_dentry *write_location = (vsfs_dentry *)(fs->image + indirect_block_number[offset_block] * VSFS_BLOCK_SIZE);
			if (write_location->ino != VSFS_BLK_UNASSIGNED) {
				memset(write_location, VSFS_BLK_UNASSIGNED, strlen(write_location->name));
			}
			memcpy(write_location, buf, size_write);
			if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
				perror("clock_gettime");
				return -ENOSYS;
			}
			root_inode->i_mtime = path_file_inode->i_mtime;
			return (int)size_write;
		}
	}

	if (offset_block < VSFS_NUM_DIRECT) {
		vsfs_dentry *write_location = (vsfs_dentry *)(fs->image + path_file_inode->i_direct[offset_block] * VSFS_BLOCK_SIZE + (offset % VSFS_BLOCK_SIZE));
		if (write_location->ino != VSFS_BLK_UNASSIGNED) {
			memset(write_location, VSFS_BLK_UNASSIGNED, strlen(write_location->name));
		}
		memcpy(write_location, buf, size_write);
		if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
			perror("clock_gettime");
			return -ENOSYS;
		}
		root_inode->i_mtime = path_file_inode->i_mtime;
		return (int)size_write;
	}
	else {
		vsfs_blk_t *indirect_block_number = (vsfs_blk_t *)(fs->image + path_file_inode->i_indirect * VSFS_BLOCK_SIZE);
		offset_block -= VSFS_NUM_DIRECT;
		vsfs_dentry *write_location = (vsfs_dentry *)(fs->image + indirect_block_number[offset_block] * VSFS_BLOCK_SIZE + (offset % VSFS_BLOCK_SIZE));
		if (write_location->ino != VSFS_BLK_UNASSIGNED) {
			memset(write_location, VSFS_BLK_UNASSIGNED, strlen(write_location->name));
		}
		memcpy(write_location, buf, size_write);
		if (clock_gettime(CLOCK_REALTIME, &(path_file_inode->i_mtime)) != 0) {
			perror("clock_gettime");
			return -ENOSYS;
		}
		root_inode->i_mtime = path_file_inode->i_mtime;
		return (int)size_write;
	}

	return -1;
}


static struct fuse_operations vsfs_ops = {
	.destroy  = vsfs_destroy,
	.statfs   = vsfs_statfs,
	.getattr  = vsfs_getattr,
	.readdir  = vsfs_readdir,
	.create   = vsfs_create,
	.unlink   = vsfs_unlink,
	.utimens  = vsfs_utimens,
	.truncate = vsfs_truncate,
	.read     = vsfs_read,
	.write    = vsfs_write,
};

int main(int argc, char *argv[])
{
	vsfs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!vsfs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!vsfs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &vsfs_ops, &fs);
}
