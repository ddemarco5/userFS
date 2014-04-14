#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "userfs.h"
#include "blocks.h"
#include "file.h"
#include "dir.h"
#include "inode.h"

void init_dir() {
	int i;
	root_dir.no_files = 0;
	for (i=0; i< MAX_FILES_PER_DIRECTORY; i++) {
		root_dir.u_file[i].free = 5;
	}
}

void write_dir() {
	write_block(DIRECTORY_BLOCK, &root_dir, sizeof(dir_struct));
}

/* 
	Finds a free file and allocates it
	sets the free file's inode and name
*/
void dir_allocate_file(int inode, const char * name) {
	int i;
	for(i=0; i<MAX_FILES_PER_DIRECTORY; i++){
		if(root_dir.u_file[i].free){
			root_dir.u_file[i].inode_number = inode;
			strcpy(root_dir.u_file[i].file_name, name);
			root_dir.u_file[i].free = false;
			root_dir.no_files++;
			break;
		}
	}
}

bool is_dir_full() {
	if(root_dir.no_files>MAX_FILES_PER_DIRECTORY)
		return true;
	else return false;
}

/* 
   Finds the file specified by name
   sets the file parameter to the file that was found
*/
bool find_file(const char * name, file_struct * file) {
	int i;
	for(i=0; i<MAX_FILES_PER_DIRECTORY; i++){
		if(!root_dir.u_file[i].free && !strcmp(root_dir.u_file[i].file_name, name)){
			*file = root_dir.u_file[i];
			//memcpy(file, &root_dir.u_file[i], sizeof(file_struct));
			//printf("FIND_FILE INODE: %i\n", file->inode_number);
			return true;
		}
	}
	return false;
}

/* 
   Free file's blocks
   Free file's inode
   Free file
*/
void dir_remove_file(file_struct file) {
	int i;
	inode inode;
	read_inode(file.inode_number, &inode);
	//free blocks
	for(i=0;i<inode.no_blocks;i++){
		free_block(inode.blocks[i]);
	}
	//free the inode
	inode.free=true;
	write_inode(file.inode_number, &inode);
	//free file
	for(i=0; i<MAX_FILES_PER_DIRECTORY; i++){
		if(root_dir.u_file[i].inode_number == file.inode_number){
			root_dir.no_files--;
			root_dir.u_file[i].free = true;
		}
	}
		
}

void dir_rename_file(const char * old, const char * new) {
	int i;
	for(i=0; i<MAX_FILES_PER_DIRECTORY; i++){
		if(strcmp(root_dir.u_file[i].file_name, old)==0)
			strcpy(root_dir.u_file[i].file_name, new);
	}
}
