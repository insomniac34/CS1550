#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>


//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
	((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(unsigned long))

//How many pointers in an inode?
//#define NUM_POINTERS_IN_INODE (BLOCK_SIZE - sizeof(unsigned int) - sizeof(unsigned long))
#define NUM_POINTERS_IN_INODE ((BLOCK_SIZE - sizeof(unsigned int) - sizeof(unsigned long))/sizeof(unsigned long))	

typedef char* string;
typedef int bool;
#define true 1
#define false 0

struct cs1550_directory_entry
{
	char dname[MAX_FILENAME	+ 1];	//the directory name (plus space for a nul)
	int nFiles;			//How many files are in this directory. 
					//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	} files[MAX_FILES_IN_DIR];		//There is an array of these
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

struct cs1550_disk_block
{
	//The first 4 bytes will be the value 0xF113DA7A 
	unsigned long magic_number;
	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

struct cs1550_inode //file_directory struct has nStartBlock which in turn points to inode for the file; who in turn has a shitton of "pointers" to disk block structs (not actually pointers)
{
	//The first 4 bytes will be the value 0xFFFFFFFF
	unsigned long magic_number;
	//The number of children this node has (either other inodes or data blocks)
	unsigned int children;
	//An array of disk pointers to child nodes (either other inodes or data)
	unsigned long pointers[NUM_POINTERS_IN_INODE];
};

typedef struct cs1550_inode cs1550_inode; //forward declaration of the inode struct

static void rmchr(char *str, char target) 
{
    char *src, *dst;
    for (src = dst = str; *src != '\0'; src++) 
    {
        *dst = *src;
        if (*dst != target) dst++;
    }
    *dst = '\0';
}

int verbose_mode = 0;

int main(int argc, char **argv)
{
	if (argc == 2)
	{
		if (strcmp("-v", argv[1]) == 0)
		{
			verbose_mode = 1;
		}		
	}

	printf("CRAWLER v0.1: Beginning iteration over .disk file...\n\n");
	FILE *disk_file = fopen(".disk", "r+");

	cs1550_disk_block cur_block;
	memset(&cur_block, 0, sizeof(cs1550_disk_block));

	cs1550_inode cur_inode;
	memset(&cur_inode, 0, sizeof(cs1550_inode));

	int blocks_found = 0;
	int inodes_found = 0;
	int cur_loc;
	while (fread(&cur_block, sizeof(cs1550_disk_block), 1, disk_file))
	{
		cur_loc = ftell(disk_file);
		if (cur_block.magic_number == 0xF113DA7A)
		{
			rmchr(cur_block.data, '\n');
			if (verbose_mode == 1) printf(" -> disk block found at offset %ld! Data[] contains: %s\n", cur_loc, cur_block.data);
			else printf(" -> disk block found at offset %ld!\n", cur_loc);
			blocks_found++;
		}
		else if (cur_block.magic_number == 0xFFFFFFFF)
		{
			printf(" -> inode found at offset %ld!\n", cur_loc); 
			inodes_found++;
		}
	}

	printf("\nCrawler Results:\n");
	printf("TOTAL BLOCKS FOUND: %d\n", blocks_found);
	printf("TOTAL INODES FOUND: %d\n", inodes_found);

	fclose(disk_file);
	return 0;
}


