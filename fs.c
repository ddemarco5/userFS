/*
  gcc -Wall fs.c `pkg-config fuse --cflags --libs` -o fusefs
*/

/*
http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/unclear.html
http://fuse.sourceforge.net/doxygen/structfuse__operations.html

man errno.h
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "src/userfs.h"
#include "src/dir.h"
#include "src/inode.h"
#include "src/blocks.h"
#include "src/sb.h"
#include "src/bitmap.h"
#include "fs.h"

int min(int x, int y){
	return x < y ? x : y;
}

int max(int x, int y){
	return x > y ? x : y;
}

/* Sets stbuf's properties based on file path
   man 3 stat
   man stat.h
   
   st_mode      Filemode 0666 rw-rw-rw
   st_nlink     Number of hard links to the file (1)
   st_mtime     Modified Time
   st_ctime     Created Time
   st_size      File size in bytes
*/
static int fs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	file_struct dummyFile;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_mtime = time(NULL);
		stbuf->st_ctime = time(NULL);
	}
	else if(find_file(path, &dummyFile)){
		inode dummyInode;
		read_inode(dummyFile.inode_number, &dummyInode);
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_nlink = 1;
		stbuf->st_mtime = time(NULL);
		stbuf->st_ctime = time(NULL);
		stbuf->st_size = dummyInode.file_size_bytes;
	}
	else {
		res = -ENOENT;
	}
	
	return res;
}

/* Reads all of the files in path into buf using the filler function
   int (*fuse_fill_dir_t)(void *buffer, char* filename, NULL, int offset 0);
   	filler will add a file to the result buffer (buf)
   
*/
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	/* === Loop through all of the files in the root directory == */
	int i,j;
	for(i=0; i<MAX_FILES_PER_DIRECTORY; i++){
		if(root_dir.u_file[i].free == 0){
				char no_path[MAX_FILE_NAME_SIZE];
				for(j=0; j<strlen(root_dir.u_file[i].file_name); j++){
					no_path[j]=root_dir.u_file[i].file_name[j+1];
				}
			filler(buf, no_path, NULL, offset);
			}
	}
	return 0;
}

/* Create a empty file named path 
   
   Find a free inode
   Mark it as allocated
   Allocate file as our inode in root dir
   Writes relevent blocks
*/
static int fs_create(const char *path, mode_t mode, struct fuse_file_info * fi) {
	
	if(strlen(path) > MAX_FILE_NAME_SIZE) {
		return -ENAMETOOLONG;
	}
	
	if(is_dir_full()) {
		return -1;
	}
	
	inode writing_inode;
	
	int freeinode = free_inode();
	
	if(freeinode < 0){
		printf("Not enough inodes\n");
		return -1;
	}
	read_inode(freeinode, &writing_inode);
	allocate_inode(&writing_inode, 0, 0);
	
	write_inode(freeinode, &writing_inode);
	printf("FREEINODE %i\n", freeinode);
	dir_allocate_file(freeinode, path);
	write_dir();
	
	return 0;
}

/* Checks that a file can be opened */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
	file_struct file;
	
	if (find_file(path, &file)) {
		return 0;
	}
	return -1;
}

/* Reads the contents of file into buf
   man 3 read
   
   finds the file for path
   reads contents of file into buf starting at offset  (recommended: implement offset after you have the rest working)
   returns the number of bytes read
NOT WORKING AND I DON'T KNOW WHY'*/
static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{

	size_t len;
	(void) fi;
	
	int i;
	int read_bytes;
	int current_bytes;
	inode inode;
	
	
	file_struct file;
	//FUSE Should have called open to check that the file exists ahead of time
	assert(find_file(path, &file));
	read_bytes = 0;
	printf("SIZE: %i\n", size);
	
	
	read_inode(file.inode_number, &inode);
	
	read_bytes=0;
	
	printf("NUMBER OF BLOCKS IN READING INODE: %i\n",inode.no_blocks);
	printf("FILE SIZE IN INODE: %i\n", inode.file_size_bytes);
	printf("READ OFFSET: %i\n", offset);



	
	
	//Offset inside the free block
	int offset_in_block = offset % BLOCK_SIZE_BYTES;
	int blockindex = (offset - offset_in_block) / BLOCK_SIZE_BYTES;
	int bytes_to_read = min(BLOCK_SIZE_BYTES - offset_in_block, inode.file_size_bytes);
	
	printf("READING BLOCK: %i\n",inode.blocks[blockindex]);
	
	fprintf(stderr, "OFFSET IN BLOCK: %i\n", offset_in_block);
	fprintf(stderr, "BLOCKINDEX: %i\n", blockindex);
	fprintf(stderr, "BYTES_TO_READ: %i\n", bytes_to_read);
	
	
	read_block_offset(inode.blocks[blockindex], buf, bytes_to_read, offset_in_block);
	fprintf(stderr, "THIS IS SIZE:\n%i\n", inode.file_size_bytes);
	
	read_bytes+=inode.file_size_bytes;
	
	return bytes_to_read;
}

/* Writes contents of buf to file
   man 3 write
   
   find file for path
   writes contents of buf into file starting at offset  (recommended: implement offset after you have the rest working)
      figure out how many blocks you need
      for each required block
      	find a free block
      	update inode to have new block
		write buf to blocks
	write relevent blocks
*/
static int fs_write(const char * path, const char * buf, size_t buff_size, off_t offset, struct fuse_file_info * fi) {
	inode inode;
	int i;
	DISK_LBA current_block;	
	
	
	file_struct file;
	assert(find_file(path, &file));
	
	printf("FILE.INODE_NUMBER %i\n", file.inode_number);
	printf("OFFSET: %i\n", offset);
	read_inode(file.inode_number, &inode);
	
	int new_size = inode.file_size_bytes + buff_size;
	int new_blockno = floor(new_size/BLOCK_SIZE_BYTES) + 1;
	
	if (new_blockno - inode.no_blocks > u_quota()) {
		return -ENOSPC;
	}
	
	if (!valid_file_size(new_blockno)) {
		return -EFBIG;
	}
	
	printf("BUFF_SIZE: %i ------- BLOCK_SIZE_BYTES: %i\n", buff_size, BLOCK_SIZE_BYTES);
	//update inode block list
	printf("INODE NUMBER OF BLOCKS: %i\n", inode.no_blocks);
	
		
	assert(inode.no_blocks != MAX_BLOCKS_PER_FILE);
	
	
	int current_block_max = inode.no_blocks * BLOCK_SIZE_BYTES;
	int blockindex = 0;
	
	//Offset inside the free block
	int offset_in_block = offset % BLOCK_SIZE_BYTES;
	
	if (offset < current_block_max-1) {
		//we are inside the inode's range
		blockindex = (offset - offset_in_block) / BLOCK_SIZE_BYTES;
	} else {
		//beyond end of inode
		inode.no_blocks++;
		blockindex = inode.no_blocks - 1;
		int freeblock = find_free_block();
		if(freeblock != -1)
			inode.blocks[blockindex] = find_free_block();
		else {
			printf("Error in find_free_block.\n");
			return -1;
			}
	}
	int freeblock = inode.blocks[blockindex];
	
	int bytes_to_write = min(BLOCK_SIZE_BYTES - offset_in_block, buff_size);
	
	fprintf(stderr, "OFFSET IN BLOCK: %i\n", offset_in_block);
	fprintf(stderr, "BLOCKINDEX: %i\n", blockindex);
	fprintf(stderr, "BYTES_TO_WRITE: %i\n", bytes_to_write);
	fprintf(stderr, "WRITING TO BLOCK: %i\n",freeblock);
	
	write_block_offset(freeblock, buf, bytes_to_write, offset_in_block);
	
	inode.file_size_bytes = max(offset + bytes_to_write, inode.file_size_bytes);
	
	write_inode(file.inode_number, &inode);
	
	//write blocks
	allocate_block(freeblock);
	write_dir();
	write_bitmap();
	
	return bytes_to_write;
}

/* Trims file to offset length
   figure out which blocks to free
   free relevent blocks
   update inode
*/
static int fs_truncate(const char * path, off_t offset) {
	inode inode;
	int i;
	int fh;
	int endblock;
	DISK_LBA current_block;
	file_struct file;
	assert(find_file(path, &file));
	
	read_inode(file.inode_number, &inode);
	int blocknumber = (inode.file_size_bytes / BLOCK_SIZE_BYTES)+1;
	printf("BLOCKNUMBER %i\n", blocknumber);
	endblock = inode.no_blocks;
	inode.no_blocks = blocknumber;
	for(i=blocknumber; i<endblock; i++){
		free_block(inode.blocks[i-1]);
	}
	write_inode(file.inode_number, &inode);
	write_bitmap();
	return 0;
}

/* Remove file 
   Save relevent blocks
*/
int fs_unlink(const char * path) {
	file_struct file;
	if (find_file(path, &file)) {
		dir_remove_file(file);
		write_dir();
		return 0;
	}
	return -ENOENT;
}

//Extra credit
static int fs_chown(const char * path, uid_t uid, gid_t gid) {
	return 0;
}

//Extra credit
static int fs_chmod(const char * path, mode_t mode) {
	return 0;
}

//Extra credit
static int fs_utimens(const char * path, const struct timespec tv[2] ) {
	return 0;
}

static int fs_rename(const char * oldpath, const char * newpath) {
	file_struct file;
	
	if(strlen(newpath) > MAX_FILE_NAME_SIZE) {
		return -ENAMETOOLONG;
	}
	
	if (find_file(oldpath, &file)) {
		dir_rename_file(oldpath, newpath);
		return 0;
	}
	return -ENOENT;
}

//Creates a structure to tell fuse about the operations we have implemented
static struct fuse_operations fs_oper = {
	.getattr	= fs_getattr,
	.readdir	= fs_readdir,
	.open		= fs_open,
	.read		= fs_read,
	.create	= fs_create,
	.chown	= fs_chown,
	.chmod	= fs_chmod,
	.utimens	= fs_utimens,
	.truncate	= fs_truncate,
	.write	= fs_write,
	.unlink	= fs_unlink,
	.rename	= fs_rename,
};

int u_quota() {
	int freeCount=0;
	int i;
	
	assert(BIT_MAP_SIZE > sb.disk_size_blocks);
	
	for (i=0; i < sb.disk_size_blocks; i++ )
	{
		if (bit_map[i]==0)
		{
			freeCount++;
		}
	}
	return freeCount;
}

int main(int argc, char **argv)
{
	int ret;
	char ** fuse_argv;
	int fuse_argc = 0;
	
	bool do_format = false;
	int size_format = 0;
	
	int argi;
	char * arg;
	char * disk = NULL;
	
	bool disable_crash = false;
	
	//Copy prog name
	fuse_argv = malloc(sizeof(char *) * argc);
	fuse_argv[0] = argv[0];
	fuse_argc++;
	
	for (argi = 1; argi < argc; argi++) {
		arg = argv[argi];
		
		if (strcmp(arg, "--help") == 0) {
			printf("Usage:\n");
			printf("\t--disk [diskfile]\n");
			printf("\t--format [size]\n");
			printf("\t--no-crash\n");
			printf("\t--help\n");
			return 0;
		} else if (strcmp(arg, "--disk") == 0) {
			argi++;
			disk = argv[argi];
		} else if (strcmp(arg, "--format") == 0) {
			do_format = true;
			argi++;
			size_format = atoi(argv[argi]);
		} else if (strcmp(arg, "--no-crash") == 0) {
			disable_crash = true;
		} else {
			fuse_argv[fuse_argc] = arg;
			fuse_argc++;
		}
	}
	
	if (disk == NULL) {
		fprintf(stderr, "Must specify disk\n");
		return -1;
	}
	
	if (do_format) {
		fprintf(stderr, "Formatting %s (size %i)\n", disk, size_format);
		u_format(size_format, disk);
		return 0;
	}
	
	recover_file_system(disk);
	//We are not clean
	sb.clean_shutdown = 0;
	write_block(SUPERBLOCK_BLOCK, &sb, sizeof(superblock));
	sync();
	
	if (!disable_crash) {
		init_crasher();
	}
	
	ret = fuse_main(fuse_argc, fuse_argv, &fs_oper, NULL);
	//We are unmounted. clean shutdown
	fprintf(stderr, "Clean shutdown\n");
	u_clean_shutdown();
	
	free(fuse_argv);
	return ret;
}
