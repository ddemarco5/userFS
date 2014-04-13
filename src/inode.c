#include <time.h>
#include <stdbool.h>
#include <assert.h>
#include "userfs.h"
#include "crash.h"
#include "blocks.h"
#include "inode.h"


int compute_inode_loc(int inode_number) {
	int whichInodeBlock;
	int whichInodeInBlock;

	whichInodeBlock = inode_number/INODES_PER_BLOCK;
	whichInodeInBlock = inode_number%INODES_PER_BLOCK;

	return (INODE_BLOCK + whichInodeBlock) *BLOCK_SIZE_BYTES + whichInodeInBlock*sizeof(inode);
}

int write_inode(int inode_number, inode * in) {
	int inodeLocation;
	assert(inode_number < MAX_INODES);

	inodeLocation = compute_inode_loc(inode_number);
  	in->last_modified = time(NULL);
	lseek(virtual_disk, inodeLocation, SEEK_SET);
	crash_write(virtual_disk, in, sizeof(inode));
  
	sync();

	return 1;
}


int read_inode(int inode_number, inode * in) {
	int inodeLocation;
	assert(inode_number < MAX_INODES);

	inodeLocation = compute_inode_loc(inode_number);

  
	lseek(virtual_disk, inodeLocation, SEEK_SET);
	read(virtual_disk, in, sizeof(inode));
  
	return 1;
}

/* 
   Sets an inode as allocated
*/
void allocate_inode(inode * in, int blocks, int size) {
	in->no_blocks = blocks;
	in->file_size_bytes = size;
	in->free = false;
}

/* 
   Returns the next free inode.
*/
int free_inode() {
	inode dummyNode;
	int i;
	for(i=0; i<MAX_INODES; i++){
		read_inode(i, &dummyNode);
		if(dummyNode.free)
			return i;
	}
	return -1;
}

