#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include "sb.h"
#include "inode.h"
#include "userfs.h"
#include "blocks.h"
#include "bitmap.h"
#include "dir.h"


/*
 * Formats the virtual disk. Saves the superblock
 * bit map and the single level directory.
 */
int u_format(int diskSizeBytes, char* file_name)
{
	int i;
	int minimumBlocks;
	inode curr_inode;

	/* create the virtual disk */
	if ((virtual_disk = open(file_name, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR)) < 0)
	{
		fprintf(stderr, "Unable to create virtual disk file: %s\n", file_name);
		return 0;
	}


	fprintf(stderr, "Formatting userfs of size %d bytes with %d block size in file %s\n",
		diskSizeBytes, BLOCK_SIZE_BYTES, file_name);

	minimumBlocks = 3+ NUM_INODE_BLOCKS+1;
	if (diskSizeBytes/BLOCK_SIZE_BYTES < minimumBlocks){
		fprintf(stderr, "Minimum size virtual disk is %d bytes %d blocks\n",
			BLOCK_SIZE_BYTES*minimumBlocks, minimumBlocks);
		fprintf(stderr, "Requested virtual disk size %d bytes results in %d bytes %d blocks of usable space\n",
			diskSizeBytes, BLOCK_SIZE_BYTES*minimumBlocks, minimumBlocks);
		return 0;
	}


	/*************************  BIT MAP **************************/

	assert(sizeof(BIT_FIELD)* BIT_MAP_SIZE <= BLOCK_SIZE_BYTES);
	fprintf(stderr, "%d blocks %d bytes reserved for bitmap (%lu bytes required)\n", 
		1, BLOCK_SIZE_BYTES, sizeof(BIT_FIELD)* BIT_MAP_SIZE );
	fprintf(stderr, "\tImplies Max size of disk is %lu blocks or %lu bytes\n",
		BIT_MAP_SIZE*BITS_PER_FIELD, BIT_MAP_SIZE*BLOCK_SIZE_BYTES*BITS_PER_FIELD);
  
	if (diskSizeBytes >= BIT_MAP_SIZE* BLOCK_SIZE_BYTES){
		fprintf(stderr, "Unable to format a userfs of size %d bytes\n",
			diskSizeBytes);
		return 0;
	}

	init_bit_map();
  
	/* first three blocks will be taken with the 
	   superblock, bitmap and directory */
	allocate_block(BIT_MAP_BLOCK);
	allocate_block(SUPERBLOCK_BLOCK);
	allocate_block(DIRECTORY_BLOCK);
	/* next NUM_INODE_BLOCKS will contain inodes */
	for (i=3; i< 3+NUM_INODE_BLOCKS; i++){
		allocate_block(i);
	}
  
	write_bitmap();
	
	/***********************  DIRECTORY  ***********************/
	assert(sizeof(dir_struct) <= BLOCK_SIZE_BYTES);

	fprintf(stderr, "%d blocks %d bytes reserved for the userfs directory (%lu bytes required)\n", 
		1, BLOCK_SIZE_BYTES, sizeof(dir_struct));
	fprintf(stderr, "\tMax files per directory: %d\n",
		MAX_FILES_PER_DIRECTORY);
	fprintf(stderr,"Directory entries limit filesize to %d characters\n",
		MAX_FILE_NAME_SIZE);

	init_dir();
	write_dir();

	/***********************  INODES ***********************/
	fprintf(stderr, "userfs will contain %lu inodes (directory limited to %d)\n",
		MAX_INODES, MAX_FILES_PER_DIRECTORY);
	fprintf(stderr,"Inodes limit filesize to %d blocks or %d bytes\n",
		MAX_BLOCKS_PER_FILE, 
		MAX_BLOCKS_PER_FILE* BLOCK_SIZE_BYTES);

	curr_inode.free = 1;
	for (i=0; i< MAX_INODES; i++){
		write_inode(i, &curr_inode);
	}

	/***********************  SUPERBLOCK ***********************/
	assert(sizeof(superblock) <= BLOCK_SIZE_BYTES);
	fprintf(stderr, "%d blocks %d bytes reserved for superblock (%lu bytes required)\n", 
		1, BLOCK_SIZE_BYTES, sizeof(superblock));
	init_superblock(diskSizeBytes);
	fprintf(stderr, "userfs will contain %d total blocks: %d free for data\n",
		sb.disk_size_blocks, sb.num_free_blocks);
	fprintf(stderr, "userfs contains %lu free inodes\n", MAX_INODES);
	
	write_block(SUPERBLOCK_BLOCK, &sb, sizeof(superblock));
	sync();


	/* when format complete there better be at 
	   least one free data block */
	assert( u_quota() >= 1);
	fprintf(stderr,"Format complete!\n");
	
	close(virtual_disk);
	return 1;
}


//This is where you recover your filesystem from an unclean shutdown
int u_fsck() {
	int i;
	
	bool allocated_inodes[MAX_INODES];
	bool allocated_blocks[sb.num_free_blocks];
	
	//initialize the allocated_inodes array.
	for(i=0; i<MAX_INODES; i++){ 
		allocated_inodes[i]=false;
	}
	//initialize the allocated_blocks array.
	for(i=0; i<sb.disk_size_blocks; i++){
		allocated_blocks[i]=false;
	}
	
	
	for(i=0;i<root_dir.no_files;i++){
		inode inode_to_check;
		read_inode(root_dir.u_file[i].inode_number, &inode_to_check);
		if(inode_to_check.free){
			fprintf(stderr, "File '%s' has lost it's inode. Deleting.\n'", root_dir.u_file[i].file_name);
			dir_remove_file(root_dir.u_file[i]);
		}
		else{
			allocated_inodes[root_dir.u_file[i].inode_number] = true;
			int j;
			for(j=0;j<inode_to_check.no_blocks; j++){
				allocated_blocks[inode_to_check.blocks[j]] = true;
			}
		}
	}
	//free up everything that isn't marked as allocated to remove orphaned blocks and inodes
	for(i=0;i<MAX_INODES;i++){
		if(!allocated_inodes[i]){
			inode inodetofree;
			read_inode(i, &inodetofree);
			inodetofree.free = true;
			write_inode(i, &inodetofree);
			fprintf(stderr, "Freed Inode %i\n", i);
		}
		else fprintf(stderr, "Inode %i is allocated\n", i);
	}
	
	for(i=0; i<sb.num_free_blocks; i++){
		if(!allocated_blocks[i+8]){
			free_block(i+8);
			fprintf(stderr, "Freed Block %i\n", i+8);
		}
		else fprintf(stderr, "Block %i is allocated\n", i+8);
	}
	
	write_bitmap();
	
	return 1;
}

/*
 * Attempts to recover a file system given the virtual disk name
 */
int recover_file_system(char *file_name)
{

	if ((virtual_disk = open(file_name, O_RDWR)) < 0)
	{
		printf("virtual disk open error\n");
		return 0;
	}

	read_block(SUPERBLOCK_BLOCK, &sb, sizeof(superblock));
	fprintf(stderr, "SUPERBLOCK: %i\n", sb.clean_shutdown);
	read_block(BIT_MAP_BLOCK, bit_map, sizeof(BIT_FIELD)*BIT_MAP_SIZE);
	read_block(DIRECTORY_BLOCK, &root_dir, sizeof(dir_struct));

	if (!superblockMatchesCode()){
		fprintf(stderr,"Unable to recover: userfs appears to have been formatted with another code version\n");
		return 0;
	}
	if (!sb.clean_shutdown)
	{
		/* Try to recover your file system */
		fprintf(stderr, "u_fsck in progress......\n");
		if (u_fsck()){
			fprintf(stderr, "Recovery complete\n");
			return 1;
		}else {
			fprintf(stderr, "Recovery failed\n");
			return 0;
		}
	}
	else{
		fprintf(stderr, "Clean shutdown detected\n");
		return 1;
	}
}

int u_clean_shutdown()
{
	/* write code for cleanly shutting down the file system
	   return 1 for success, 0 for failure */
	sb.num_free_blocks = u_quota();
	
	sb.clean_shutdown = 1;

	lseek(virtual_disk, BLOCK_SIZE_BYTES* SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));
	sync();

	close(virtual_disk);
	/* is this all that needs to be done on clean shutdown? */
	return !sb.clean_shutdown;
}
