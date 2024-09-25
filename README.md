# FUSE File System Implementation

## Overview
This project implements a version of the Very Simple File System (VSFS) using the **FUSE (Filesystem in Userspace)** library. The goal of the project was to provide functionality for creating, reading, writing, deleting, and resizing files on a disk image. This assignment showcases a strong understanding of file system architecture, low-level systems programming, and the FUSE API.

## Features
- **File System Creation:** Supports the creation of an empty VSFS disk image.
- **Basic File Operations:** Implements file creation, deletion, reading, writing, and resizing.
- **Directory Operations:** Lists files and retrieves status information about the file system.
- **Error Handling:** Includes robust error handling to maintain system stability and data consistency.

## Technologies Used
- **C Programming Language:** Main implementation language.
- **FUSE Library:** Used to create the file system in user space.
- **Linux Environment:** Developed and tested on a Linux system.

## Directory Structure
/project-directory 
│── mkfs.c # Code for formatting an empty disk into a VSFS file system 
│── vsfs.c # Main file containing FUSE callbacks for file system operations 
│── vsfs.h # Header file containing data structures and constants for VSFS 
│── map.c # Helper functions for mapping the disk image file into memory 
│── map.h # Header file for the map.c functions 
│── fs_ctx.c # Contains runtime state management of the mounted file system 
│── fs_ctx.h # Header file for fs_ctx.c 
│── util.h # Utility functions to assist with operations such as bit manipulation 
│── bitmap.c # Functions to manage bitmaps for block and inode allocation 
│── bitmap.h # Header file for bitmap.c 
│── Makefile # Compilation instructions 
│── README.md # This documentation file

## How to Compile
To compile the project, use the provided `Makefile`. Run the following command in the terminal:
```bash
make
```

This command will generate two executables:

mkfs.vsfs: Used for formatting an empty disk image.
vsfs: Used to run and mount the VSFS using FUSE.

## How to Use

### 1. Creating a Disk Image
To create an empty disk image, use the truncate command and mkfs.vsfs executable:

```
bash
Copy code
truncate -s <size> <image_file> 
./mkfs.vsfs -i <number_of_inodes> <image_file>
```
Replace <size> with the desired size of the image (e.g., 64K or 1M).
Replace <number_of_inodes> with the total number of inodes for the file system.

### 2. Mounting the File System
To mount the VSFS on a specific mount point:
```
bash
Copy code
mkdir /tmp/<username>
./vsfs <image_file> /tmp/<username> -f
```
Replace <image_file> with the name of your disk image.
Replace <username> with your username or a directory of your choice.

### 3. Using the File System
After mounting, you can use standard file operations like ls, cp, rm, etc., to interact with the mounted file system.

### 4. Unmounting the File System
To unmount the file system, use:
```
bash
Copy code
fusermount -u /tmp/<username>
Testing
```
To verify the implementation, you can run operations like:

`ls -l` to list files and directories.
`touch` to create files.
`echo "data" > file.txt` to write data into a file.
`cat file.txt` to read file contents.
`rm file.txt` to delete a file.
Provided Consistency Checkers
Two consistency checkers (fsck.mkfs and fsck.vsfs) are provided in the course repository to test the correctness of your file system:

fsck.mkfs: Checks that mkfs.vsfs correctly formats the disk.
fsck.vsfs: Checks for data consistency and ensures that file system operations do not corrupt the disk.
Challenges and Learning Outcomes

File System Architecture: Gained hands-on experience with implementing a file system's core functionalities, including block allocation, inode management, and data handling.
Error Handling: Implemented robust error handling to ensure system stability and data integrity.
FUSE Library: Learned how to interact with the FUSE library to implement a file system in user space.

## References

libfuse GitHub Repository

FUSE API Documentation

OSTEP Textbook: Referenced for understanding the VSFS structure.

## Author

Aryaman Arora, University of Toronto, CSC369 - Fall 2023
