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
 * CSC369 Assignment 4 - vsfs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include "vsfs.h"
#include "bitmap.h"
#include "map.h"

/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into vsfs file system. The file must exist and\n\
its size must be a multiple of vsfs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing vsfs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, VSFS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into vsfs. */
static bool vsfs_is_present(void *image)
{
	// Check if the image already contains a valid vsfs superblock.
	// This may be overly trusting. You can add additional sanity checks.
	
	vsfs_superblock *sb = (vsfs_superblock *)image;
	if (sb->sb_magic == VSFS_MAGIC) {
		return true;
	} else {
		return false;
	}
}


/**
 * Format the image into vsfs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param fd     open file descriptor for the disk image file
 * @param buf    scratch buffer of at least VSFS_BLOCK_SIZE bytes
 * @param size   image file size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size. 
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//Initialize the superblock and create an empty root directory
	//NOTE: the mode of the root directory inode should be set
	//      to S_IFDIR | 0777

	if (vsfs_is_present(image)) {
		return false;
	}

	vsfs_superblock *sb = (vsfs_superblock *)image;       // ptr to superblock in mmap'd disk image
	bitmap_t        *ibmap;    // ptr to inode bitmap in mmap'd disk image
	bitmap_t        *dbmap;    // ptr to data block bitmap in mmap'd image
	vsfs_inode      *itable;   // ptr to inode table in mmap'd image

	vsfs_inode  *root_ino;     // ptr to root inode (in inode table)
	vsfs_dentry *root_entries; // ptr to root dir data block in mmap'd image
	
	vsfs_blk_t nblks = size / VSFS_BLOCK_SIZE;
	sb->sb_num_blocks = nblks;
	sb->sb_free_blocks = nblks;
	uint32_t   inodes_per_block = VSFS_BLOCK_SIZE / sizeof(vsfs_inode);
	bool       ret = false;
	
	if (opts->n_inodes >= VSFS_INO_MAX) {
		return false;
	}

	if (nblks > VSFS_BLK_MAX || nblks < VSFS_BLK_MIN) {
		return false;
	}

	// Initialize inode bitmap in memory (write to disk happens at munmap).
	// First set all bits to 1, then use bitmap_init to clear the bits
	// for the given number of inodes in the file system.
	
	ibmap = (bitmap_t *)(image + VSFS_IMAP_BLKNUM * VSFS_BLOCK_SIZE);	
	memset(ibmap, 0xff, VSFS_BLOCK_SIZE);
	bitmap_init(ibmap, opts->n_inodes);
	
	// Initialize data bitmap in memory (write to disk happens at munmap).
	// First set all bits to 1, then use bitmap_init to clear the bits
	// for the given number of blocks in the file system.
	
	dbmap = (bitmap_t *)(image + VSFS_DMAP_BLKNUM * VSFS_BLOCK_SIZE);
	memset(dbmap, 0xff, VSFS_BLOCK_SIZE);
	bitmap_init(dbmap, nblks);

	// Mark first 3 blocks (superblock, inode bitmap, data bitmap) allocated.
	bitmap_set(dbmap, nblks, VSFS_SB_BLKNUM, true); // superblock
	bitmap_set(dbmap, nblks, VSFS_IMAP_BLKNUM, true); // inode bitmap block
	bitmap_set(dbmap, nblks, VSFS_DMAP_BLKNUM, true); // data bitmap block
	sb->sb_free_blocks -= 3;
	
	// Calculate size of inode table and mark inode table blocks allocated.
	uint32_t num_inode_table_blocks = div_round_up(opts->n_inodes, inodes_per_block);
	sb->sb_num_inodes = inodes_per_block * num_inode_table_blocks;
	sb->sb_free_inodes = sb->sb_num_inodes;
	uint32_t first_itable_block_index;
	int err = bitmap_alloc(dbmap, nblks, &first_itable_block_index);
	assert(!err);
	for (uint32_t n = first_itable_block_index; n < num_inode_table_blocks + first_itable_block_index; ++n) {
		bitmap_set(dbmap, nblks, n, true);
	}
	sb->sb_free_blocks -= num_inode_table_blocks;
	sb->sb_data_region = first_itable_block_index + num_inode_table_blocks;

	// Initialize the root directory.
	// 1. Mark root directory inode allocated in inode bitmap
	uint32_t next_ibm_index;
	err = bitmap_alloc(ibmap, inodes_per_block, &next_ibm_index);
	assert(!err);
	sb->sb_free_inodes -= 1;

	// 2. Initialize fields of root dir inode (the mtime is done for you)
	itable = (vsfs_inode *)(image + VSFS_ITBL_BLKNUM * VSFS_BLOCK_SIZE);	
	root_ino = &itable[VSFS_ROOT_INO];
	root_ino->i_mode = S_IFDIR | 0777;
	root_ino->i_nlink = 2;
	root_ino->i_blocks = 1;
	root_ino->i_size = root_ino->i_blocks * VSFS_BLOCK_SIZE;
	memset(root_ino->i_direct, VSFS_BLK_UNASSIGNED,  VSFS_NUM_DIRECT * sizeof(vsfs_blk_t));
	if (clock_gettime(CLOCK_REALTIME, &(root_ino->i_mtime)) != 0) {
		perror("clock_gettime");
		goto out;
	}
	
	// 3. Allocate a data block for root directory; record it in root inode
	uint32_t root_db_index;
	err = bitmap_alloc(dbmap, nblks, &root_db_index);
	assert(!err);
	root_ino->i_direct[0] = root_db_index;
	sb->sb_free_blocks -= 1;
	
	// 4. Create '.' and '..' entries in root dir data block.
	root_entries = (vsfs_dentry *)(image + root_db_index * VSFS_BLOCK_SIZE);
	root_entries[0].ino = VSFS_ROOT_INO;
	strcpy(root_entries[0].name, ".");

	root_entries[1].ino = VSFS_ROOT_INO;
	strcpy(root_entries[1].name, "..");	
	
	// 5. Initialize other dir entries in block to invalid / unused state
	//    Since 0 is a valid inode, use VSFS_INO_MAX to indicate invalid.
	for (uint32_t i = 2; i < div_round_up(VSFS_BLOCK_SIZE, sizeof(vsfs_dentry)); ++i) {
		root_entries[i].ino = VSFS_INO_MAX;
	}
	
	// Initialize fields of superblock after everything else succeeds.
	// Set start of data region to first block after inode table.
	sb->sb_magic = VSFS_MAGIC;
	sb->sb_size = size;
	
	ret = true;
 out:
	return ret;
}


int main(int argc, char *argv[])
{
	int ret; // return value; 0 on success, 1 on failure
	size_t fsize; // size of disk image file 
	void *image;  // pointer to mmap'd disk image file
	mkfs_opts opts = {0}; // options; defaults are all 0
	
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map disk image file into memory
	image = map_file(opts.img_path, VSFS_BLOCK_SIZE, &fsize);
	if (image == NULL) {
		return 1;
	}

	// Check if overwriting existing file system
	if (!opts.force && vsfs_is_present((vsfs_superblock *)image)) {
		fprintf(stderr, "Image already contains vsfs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) {
		// Fill buffer with zeros
		memset(image, 0, fsize);
	}
	
	if (!mkfs(image, fsize, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
	
end:
	munmap(image, fsize);
	return ret;
}
