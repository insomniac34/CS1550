/*
	Tyler Raborn
	CS-1550 Project 4

*/

#define	FUSE_USE_VERSION 26
#define DEBUG_ENABLE 1

#include <stdlib.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>	
#include <math.h>

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

#define FILE_DIRECTORY_SIZE (sizeof(char)*MAX_FILENAME+1) + (sizeof(char)*MAX_EXTENSION+1) + (sizeof(size_t)) + (sizeof(long))

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

static char* substr(const char* str, size_t begin, size_t len) 
{ 
	if (str == 0 || strlen(str) == 0 || strlen(str) < begin || strlen(str) < (begin+len)) 
	{
		printf("Substr returning NULL!\n");
		return NULL; 
	}

	//from the linux programmer's manual: The strndup() function is similar, but only copies at most n characters.  If s is longer than n, only n characters are copied, and a terminating null byte ('\0') is added.

	return strndup(str + begin, len); 
} 

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

/*
 *	Accepts a directory path name in the format </directory/file.ext> and returns a string containing <directory>
 */
static char *extract_dir_name(const char *path_name)
{
	#if DEBUG_ENABLE
		printf("extracting dir name from path: %s\n", path_name);
	#endif

	//int loc = 0;
	char cur_char;
	char *ret = NULL;

	int i;
	int j = 1;
	int dir_found = 0;
	int len = strlen(path_name);
	for (i = 0; i < len; i++)
	{
		cur_char = path_name[i];
		if (cur_char == '/')
		{
			dir_found = 1;
			int second_slash_found = 0;
			while (j < len)
			{
				if (path_name[j+i] == '/')
				{
					second_slash_found = 1;
					break;
				}

				j++;
			}

			if (second_slash_found == 0)
			{
				#if DEBUG_ENABLE
					puts("WARNING! RETURNING NULL!!");
					//puts("File did not have second slash");
				#endif

				return NULL;
			}

			break; //i is now equal to location of slash
		}
	}

	if (dir_found == 1)
	{
		ret = substr(path_name, (size_t)i, (j+i)+1);
	}

	return ret;
}

/*
 * Returns the number of occurences of string *target* in string *str*
 */
static int chrcnt(const char *str, const char target)
{
	int char_count = 0;
	int len = strlen(str);

	int i;
	char current_char;
	for (i = 0; i < len; i++)
	{
		current_char = str[i];
		if (current_char == target)
		{
			char_count++;
		}
	}

	return char_count;
}

static bool is_a_directory(const char *path_name) //extract directory name, verify against master dir list
{
	bool dir_found = false;

	char struct_buffer[sizeof(cs1550_directory_entry)];
	memset(struct_buffer, 0, sizeof(cs1550_directory_entry));

	FILE *directories_file = fopen(".directories", "a+"); //open for read ops
	while (fread(struct_buffer, sizeof(cs1550_directory_entry), 1, directories_file))
	{
		cs1550_directory_entry *current_dir = (cs1550_directory_entry*)struct_buffer;
		if (strcmp(path_name, current_dir->dname) == 0)
		{
			dir_found = true;
			break;
		}
	}

	fclose(directories_file); 
	return dir_found;
}

static bool is_a_file(const char *filename, const char *dir, const char *ext, int *fsize) //returns true if <file_path> references a valid file.
{
	bool file_found = false;
	printf("\nEntering IS_A_FILE() with filename = %s and directory name = %s!\n", filename, dir);

	cs1550_directory_entry entry;
	memset(&entry, 0, sizeof(cs1550_directory_entry));

	FILE *directories_file = fopen(".directories", "r+");

	while (fread(&entry, sizeof(cs1550_directory_entry), 1, directories_file))
	{

		if (strcmp(entry.dname, dir) == 0)
		{
			printf("IS_A_FILE(): Found target dir %s! Number of files contained is %d\n", entry.dname, entry.nFiles);	
			int i;
			for (i = 0; i < entry.nFiles; i++)
			{
				printf("IS_A_FILE(): inside of file array loop iteration %d. Comparing fname %s and passed-in filename %s as well as fext %s and passed-in extension %s\n", i, entry.files[i].fname, filename, entry.files[i].fext, ext);

				if ((strcmp(entry.files[i].fname, filename) == 0) && (strcmp(entry.files[i].fext, ext) == 0))
				{
					printf("IS_A_FILE(): FOUND TARGET FILE %s with extension %s and size %d!\n", filename, ext, (int)entry.files[i].fsize);
					printf("IS_A_FILE(): CURRENT POSITION OF FILE POINTER FOR READING DIR INFO IS: %d\n", ftell(directories_file));
					file_found = true;
					*fsize = entry.files[i].fsize;
					break;
				}				
			}
			if (file_found) break;
		}
	}

	fclose(directories_file);
	return file_found;
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	bool is_root_dir = false;

	printf("ENTERING GETATTR(), path = %s\n", path);

	//char *directory = extract_dir_name(path);

	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];

	memset(directory, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(filename, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(extension, 0, sizeof(char)*(MAX_EXTENSION+1));

	int splice_result = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	if (path[0] == '/' && chrcnt(path, '/') == 1)
	{
		if (strlen(path)==1) is_root_dir = true; 
		else 
		{
			printf("GETATTR(): Removing slash from path %s\n", path);	
			rmchr(path, '/');
		}
	}	

	memset(stbuf, 0, sizeof(struct stat));
   
	//is path the root dir?
	//if (strcmp(path, "/") == 0) 
	if (is_root_dir == true)
	{
		//ROOT DIR
		printf("GETATTR: %s is the root directory!\n", path);
		stbuf->st_mode = S_IFDIR | 0755; //logical OR with 0755 to produce octal file permission val
		stbuf->st_nlink = 2;
	}
	else
	{
		if (is_a_directory(path)==true)
		{
			//REGULAR DIR
			printf("GETATTR: %s is a directory!\n", path);
			stbuf->st_mode = S_IFDIR | 0755; //logical OR with 0755 to produce octal file permission val
			stbuf->st_nlink = 2;	
			res = 0;		
		}
		else
		{
			size_t filesize = 0;
			printf("GETATTR(): is %s a file?\n", path);
			if (is_a_file(filename, directory, extension, &filesize)==true)
			{
				printf("GETATTR(): YES! Returned value in filesize is %d\n", (int)filesize);
				if (splice_result == 2)
				{
					printf("%s is a file with no extension!", path);
					//FILE WITH NO EXTENSION
					//regular file, probably want to be read and write
					stbuf->st_mode = S_IFREG | 0666; 
					stbuf->st_nlink = 1; //file links
					stbuf->st_size = filesize; //file size - make sure you replace with real size!
					res = 0; // no error
				}
				else
				{
					printf("%s is a file with extension!\n", path);
					//FILE WITH EXTENSION
					//regular file, probably want to be read and write
					stbuf->st_mode = S_IFREG | 0666; 
					stbuf->st_nlink = 1; //file links
					stbuf->st_size = filesize; //file size - make sure you replace with real size!
					res = 0; // no error
				}
			}
			else
			{
				printf("%s RETURNING ERROR: NO ENTRY!!\n", path);
				res = -ENOENT;
			}
		}
	} 



	/*
	else if (is_a_directory(path) == true) //if its not the root dir, and it is contained within the primary directories file, then its a dir, but a nonroot dir
	{
		printf("GETATTR: %s is a directory!\n", path);
		stbuf->st_mode = S_IFDIR | 0755; //generate file permission val
		stbuf->st_nlink = 2;
		res = 0; //no error
	}
	else if (is_a_file(path) == true) //if pathname is not a dir but still exists, it must be a file
	{		
		printf("GETATTR: %s is a file!\n", path);
		stbuf->st_mode = S_IFREG | 0666; //generate file permission val
		stbuf->st_nlink = 1; //file links
		stbuf->st_size = strlen(path); //file size - make sure you replace with real size!
		res = 0; // no error
	}
	else //Else return that the path doesn't exist
	{
		puts("GETATTR RETURNING ERROR NO ENTRY!!");
		res = -ENOENT; 		
	}
	*/

	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	bool dir_found = false;

	FILE *directories_file = fopen(".directories", "a+");
	char struct_buffer[sizeof(cs1550_directory_entry)];
	memset(struct_buffer, 0, sizeof(cs1550_directory_entry));

	#if DEBUG_ENABLE
		printf("READDIR: Calling readdir() with path = %s\n", path);
	#endif

	if (strcmp(path, "/") == 0) //path is root directory, print out each directory
	{
		#if DEBUG_ENABLE
			printf("listing of root directory requested...\n");
		#endif

		while(fread(struct_buffer, sizeof(cs1550_directory_entry), 1, directories_file)) //read a struct's worth of data into the buffer
		{
			#if DEBUG_ENABLE
				printf("Iterating over .directories file...\n");
			#endif

			cs1550_directory_entry *current_dir = (cs1550_directory_entry*)struct_buffer; //convert data in buffer into directory struct	
			printf("%s\n", current_dir->dname);

			filler(buf, current_dir->dname, NULL, 0);

			dir_found = true;
		}

		if (feof(directories_file)) //succesful read
		{
			fclose(directories_file); 
		}
		else //unsuccesful read
		{
			perror("ERROR: DIRECTORIES FILE READ INTERRUPED!");
			exit(-1);
		}		
	}
	else
	{
		if (path[0] == '/') //remove initial slash
		{
			rmchr(path, '/');
		}

		printf("READDIR: FILE LISTING REQUESTED!\n");
		while(fread(struct_buffer, sizeof(cs1550_directory_entry), 1, directories_file)) //read a struct's worth of data into the buffer
		{
			cs1550_directory_entry *current_dir = (cs1550_directory_entry*)struct_buffer; //convert data in buffer into directory struct	
			int number_of_files = current_dir->nFiles;
			if (strcmp(path, current_dir->dname) == 0) //iterate over the current directory's list of files, outputting each one
			{
				printf("READDIR(): Target path %s found! Iterating over %d files...\n", current_dir->dname, current_dir->nFiles);
				int i;

				for (i = 0; i < number_of_files; i++)
				{
					printf("READDIR(): file %s detected.\n", current_dir->files[i].fname);
					if (strlen(current_dir->files[i].fext) == 0) filler(buf, current_dir->files[i].fname, NULL, 0);
					else filler(buf, strncat(strncat(current_dir->files[i].fname, ".", 1), current_dir->files[i].fext, MAX_EXTENSION), NULL, 0);
				}					
		
				dir_found = true;
				break;
			}
		}

		fclose(directories_file);

		if (dir_found == false)
		{
			puts("RETURNING ERROR NO ENTRY!!");
			return -ENOENT;
		}
		else //GREAT SUCCESS!
		{
			return 0; 
		}
	}

	
	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)	//to make a directory we first need to check that a directory can be made in the location specified by <path>, and if so, add it to the .directories file...
{
	(void) path;
	(void) mode;

	if (chrcnt(path, '/') > 1)
	{
		printf("MKDIR: %s is being created beyond the root directory! Returning EPERM!\n");
		return -EPERM;
	}	

	printf("entering cs1550_mkdir() with path = %s\n", path);
	if ((strlen(path)-1) > MAX_FILENAME) //needs to be -1 in order to account for initial forward slash
	{
		printf("DIRECTORY NAME LENGTH (%d) IS TOO LONG!\n", (int)strlen(path)-1);
		return -ENAMETOOLONG;
	}	

	if (path[0] == '/')
	{
		//remove / from front of path
		rmchr(path, '/');
	}

	if (is_a_directory(path) == true)
	{
		printf("MKDIR: %s is a directory! Returning EEXISTS!\n", path);
		return -EEXIST;
	}

	cs1550_directory_entry new_entry;
	memset(&new_entry, 0, sizeof(cs1550_directory_entry));

	strncpy(new_entry.dname, path, MAX_FILENAME+1);
	new_entry.nFiles = 0;

	FILE *directories_file = fopen(".directories", "a"); //open .directories for appending ops
	fwrite(&new_entry, sizeof(cs1550_directory_entry), 1, directories_file); //write struct to file
	fclose(directories_file);

	return 0;
}

/* 
 * Removes a directory.

RETURNS:
	-ENOTEMPTY if the directory is not empty
	-ENOENT if the directory is not found
	-ENOTDIR if the path is not a directory
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;

	if (chrcnt(path, '/') > 1)
	{
		printf("Deletion target is NOT a directory! Returning ENOTDIR!\n");
		return -ENOTDIR;
	}	

	if (path[0] == '/')
	{
		rmchr(path, '/');
	}

	printf("CALLING RMDIR on path %s!\n", path);

	if (is_a_directory(path) == false)
	{
		printf("%s is NOT a directory! Returning ENOENT!\n");
		return -ENOENT;
	}

	cs1550_directory_entry struct_buffer;
	memset(&struct_buffer, 0, sizeof(cs1550_directory_entry)); //initialize memory chunk to 0's

	FILE *directories_file = fopen(".directories", "r");
	FILE *temp_file = fopen(".temp", "a");

	if (temp_file == NULL)
	{
		printf("NULL RETURNED BY FOPEN! EXITING!");
		exit(-1);
	}

	if (directories_file == NULL)
	{
		printf("NULL RETURNED BY FOPEN! EXITING!");
		exit(-1);
	}	

	while(fread(&struct_buffer, sizeof(cs1550_directory_entry), 1, directories_file)) //read a struct's worth of data into the buffer
	{
		if (strcmp(struct_buffer.dname, path) == 0) //WE HAVE LOCATED THE TARGET DIRECTORY: to delete it, we must simply exclude it from being copied to temp...
		{
			if (struct_buffer.nFiles > 0) //make sure dir is NOT empty
			{
				fclose(directories_file);
				fclose(temp_file);
				system("rm .temp");
				return -ENOTEMPTY;
			}
			printf("\nRMDIR: Omitting removed directory in copy operation because %s equals %s\n", struct_buffer.dname, path);
		}
		else
		{
			fwrite(&struct_buffer, sizeof(cs1550_directory_entry), 1, temp_file);
		}
	}

	fclose(directories_file); //programatically close file
	fclose(temp_file);

	system("cp .temp .directories");
	system("rm .temp");

    return 0;
}

/*
	struct cs1550_disk_block
	{
		//The first 4 bytes will be the value 0xF113DA7A 
		unsigned long magic_number;
		//And all the rest of the space in the block can be used for actual data
		//storage.
		char data[MAX_DATA_IN_BLOCK];
	};
*/

#define BITMAP_SEEK_DISTANCE 5241344
static long next_available_disk_block()
{
	printf("Entering next_available_disk_block()...\n");

	//iterate to location of bitmap on disk (that would be the LAST block) so we can get current status of disk...10240 512-byte blocks are contained within the 5mb disk file
	FILE *disk_file = fopen(".disk", "rb+");
	fseek(disk_file, (10237*512), SEEK_SET); //seek to last block

	//now, the file position indicator is set to the last block...lets read our bitmap...IN THE CASE where this is the first value written to disk, simply extracting the struct as normal, as all of its elements are set to 0
	cs1550_disk_block bitmap;
	memset(&bitmap, 0, sizeof(cs1550_disk_block));
	fread(&bitmap, sizeof(cs1550_disk_block), 1, disk_file);

	int pos = 0;
	int offset = 0;
	int free_loc;
	char current_char = bitmap.data[pos];

	while (true)
	{
		if ((current_char & 255) == 255)
		{
			printf("Bit block is full! Moving on.\n");
			offset+=8;
			pos+=1;
			current_char = bitmap.data[pos];
		}
		else //at least 1 free bit
		{
			free_loc = 0;
			char temp = 1;
			while ((temp & current_char) == temp)
			{
				printf("AND'ing %u with %u. Loop continues if %u == %u\n", (unsigned int)temp, (unsigned int) current_char, (temp & current_char), temp);
				temp = temp << 1;
				free_loc++;
			}
			printf("Loop complete. OR'ing %u with %u\n", (unsigned int)bitmap.data[pos], (unsigned int)temp);
			bitmap.data[pos] = bitmap.data[pos] | temp;
			offset+=(8-free_loc);
			break;
		}
	}

	printf("data in bitmap at location %d is: %u\n", 0, (unsigned int)bitmap.data[0]);	
	printf("Writing char %u to disk!\n", (unsigned int)bitmap.data[pos]);

	fseek(disk_file, (10237*512), SEEK_SET); 
	fwrite(&bitmap, sizeof(cs1550_disk_block), 1, disk_file);
	fclose(disk_file);

	printf("next_available_disk_block(): retval is %d\n", offset);
	return (long)offset;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	(void) path;

	printf("\nMKNOD: Entering cs1550_mknod() with path = %s\n", path);

	long inode_block = next_available_disk_block();

	printf("MKNOD: disk block is block # %u\n", inode_block);
	printf("\n");

	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];

	memset(directory, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(filename, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(extension, 0, sizeof(char)*(MAX_EXTENSION+1));

	int splice_result = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	cs1550_directory_entry cur_entry;
	memset(&cur_entry, 0, sizeof(cs1550_directory_entry));

	FILE *directories_file = fopen(".directories", "r+");
	FILE *temp_file = fopen(".temp", "w+");

	while (fread(&cur_entry, sizeof(cs1550_directory_entry), 1, directories_file))
	{
		if (strcmp(directory, cur_entry.dname) == 0) //we have found the file's home directory...
		{
			int new_file_loc = cur_entry.nFiles;

			//so update the directory entry in the .directories file
			strncpy(cur_entry.files[new_file_loc].fname, filename, MAX_FILENAME+1);
			strncpy(cur_entry.files[new_file_loc].fext, extension, MAX_EXTENSION+1);
			cur_entry.files[new_file_loc].fsize = 0;
			cur_entry.files[new_file_loc].nStartBlock = inode_block;
			printf("MKNOD: Writing cur_entry with fname %s and extension %s to disk!\n", cur_entry.files[new_file_loc].fname, cur_entry.files[new_file_loc].fext);
			cur_entry.nFiles++; //increment # of files
		}


		fwrite(&cur_entry, sizeof(cs1550_directory_entry), 1, temp_file);		
	}

	fclose(directories_file);
	fclose(temp_file);

	system("cp .temp .directories");
	system("rm .temp");

	//create an inode and write it to the .disk file
	cs1550_inode new_inode;
	memset(&new_inode, 0, sizeof(cs1550_inode));

	new_inode.magic_number = 0xFFFFFFFF;
	new_inode.pointers[0] = 0x00000000;
	new_inode.children = 0;	

	FILE *disk_file = fopen(".disk", "rb+");
	fseek(disk_file, (512*inode_block), SEEK_SET);
	fwrite(&new_inode, sizeof(cs1550_inode), 1, disk_file);
	fclose(disk_file);
	
	return 0; //success
}

static void bitmap_free(cs1550_inode *target_inode)
{
	FILE *disk_file = fopen(".disk", "r+");
	fseek(disk_file, BITMAP_SEEK_DISTANCE, SEEK_SET); //move file pointer to bitmap

	cs1550_disk_block bitmap_block;
	memset(&bitmap_block, 0, sizeof(cs1550_disk_block));

	long cur_offset;
	int num_chars; //how many chars deep the number is...
	int num_bits;
	int i;
	for (i = 0; i < target_inode->children; i++)
	{
		//for each child, flip its respective bit to 0...
		cur_offset = target_inode->pointers[i];
		num_chars = cur_offset / 8;
		num_bits = cur_offset - num_chars;
		//need to use cur_offset to somehow get the equivalent number of bits but 000'd out to the right of the first one. this is the bit we need to free...

		fread(&bitmap_block, sizeof(cs1550_disk_block), 1, disk_file);
		fseek(disk_file, (-512), SEEK_CUR);

		int i = 0;
		char current_char = bitmap_block.data[i];
		while(true)
		{
			if (num_chars == i)
			{
				//we are at current location. NOW iterate over # of bits.
				char temp = 1;
				int j;
				for (j = 0; j < num_bits; j++)
				{
					temp << 1; //left shift temp
				}

				temp = !temp; //not out temp to yield all 1's and a 0 (ex: 11101111)...we can use this value to ONLY 0-out the target bit
				current_char = current_char & temp;
				break;
			}

			i++;
			current_char = bitmap_block.data[i];
		}
	}

	fclose(disk_file);
}

/*
 * Deletes a file
 */
/*
	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	}
*/
static int cs1550_unlink(const char *path)
{
    (void) path;

    printf("UNLINK(): Calling cs1550_unlink() with path = %s\n", path);

	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];

	memset(directory, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(filename, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(extension, 0, sizeof(char)*(MAX_EXTENSION+1));

	int splice_result = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);    

    //step 1: remove entry from directory struct:
    cs1550_directory_entry entry;
    memset(&entry, 0, sizeof(cs1550_directory_entry));

    FILE *directories_file = fopen(".directories", "r+");
    FILE *temp_file = fopen(".temp", "a+");

    cs1550_inode target_inode; //to hold location of root inode for deleted file...
    memset(&target_inode, 0, sizeof(cs1550_inode));

    while(fread(&entry, sizeof(cs1550_directory_entry), 1, directories_file))
    {
    	if (strcmp(entry.dname, directory) == 0) //target directory found. We must modify the files array of the directory struct prior to writing it back to disk
    	{
    		printf("UNLINK(): Target directory found!\n");
    		int i, file_counter;
    		file_counter = 0;
    		for (i = 0; i < entry.nFiles; i++) //iterate over files array until we encounter the target filename
    		{
    			if (strcmp(entry.files[i].fname, filename) == 0) //target file found
    			{
    				printf("UNLINK(): target file %s found! Omitting it to ensure deletion.\n", entry.files[i].fname);
    				FILE *disk_file = fopen(".disk", "r+");
    				fseek(disk_file, entry.files[i].nStartBlock*sizeof(cs1550_disk_block), SEEK_SET);
    				fread(&target_inode, sizeof(cs1550_inode), 1, disk_file);
    				fclose(disk_file);
    			}
    			else //copy over data 
    			{
    				strncpy(entry.files[file_counter].fname, entry.files[i].fname, MAX_FILENAME+1);
    				strncpy(entry.files[file_counter].fext, entry.files[i].fext, MAX_EXTENSION+1);
    				entry.files[file_counter].fsize = entry.files[i].fsize;
    				entry.files[file_counter].nStartBlock = entry.files[i].nStartBlock;
    				file_counter++;
    			}
    		}
    		entry.nFiles--;
    		//memset(&(entry.files[MAX_FILES_IN_DIR-1]), 0, FILE_DIRECTORY_SIZE);
    	}
    }
    fwrite(&entry, sizeof(cs1550_directory_entry), 1, temp_file); //write directories to temp file BUT exclude target directory

    //step 2: modify bitmap to indicate that all previously allocated blocks are now overwriteable:
    bitmap_free(&target_inode);

    fclose(directories_file);
    fclose(temp_file);

    system("cp .temp .directories");
    system("rm .temp");

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	printf("READ(): Entering cs1550_read() with path=%s\n", path);

	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];

	memset(directory, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(filename, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(extension, 0, sizeof(char)*(MAX_EXTENSION+1));

	int splice_result = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);	

	if (is_a_directory(path)==true)
	{
		return -EISDIR;
	}

	FILE *directories_file = fopen(".directories", "r+");
	FILE *disk_file = fopen(".disk", "r+");

	cs1550_directory_entry cur_entry;
	memset(&cur_entry, 0, sizeof(cs1550_directory_entry));

	long inode_block;
	while(fread(&cur_entry, sizeof(cs1550_directory_entry), 1, directories_file))
	{
		if (strcmp(cur_entry.dname, directory) == 0) //if target dir found...
		{
			printf("READ(): TARGET DIR FOUND!\n");
			int i;
			for (i = 0; i < cur_entry.nFiles; i++)
			{
				if ((strcmp(cur_entry.files[i].fname, filename) == 0) && (strcmp(cur_entry.files[i].fext, extension) == 0))
				{
					if (offset > cur_entry.files[i].fsize)
					{
						return -EFBIG;
					}

					printf("READ(): FILE FOUND! inode block is at location %d\n", cur_entry.files[i].nStartBlock);
					inode_block = cur_entry.files[i].nStartBlock; //we now have the location of the file's inode...		
					break;
				}
			}
			break;
		}
	}	

	cs1550_inode cur_inode;
	memset(&cur_inode, 0, sizeof(cs1550_inode));
	fseek(disk_file, sizeof(cs1550_disk_block)*inode_block, SEEK_SET);
	fread(&cur_inode, sizeof(cs1550_inode), 1, disk_file);

	printf("READ(): Cur_inode has %d children\n", cur_inode.children);
	//printf("READ(): current inode has ");

	int read_start = offset / MAX_DATA_IN_BLOCK;
	int byte_offset = offset % MAX_DATA_IN_BLOCK;

	cs1550_disk_block cur_block;
	memset(&cur_block, 0, sizeof(cs1550_disk_block));
	printf("READ(): fseeking to pointer to disk block: %d\n", cur_inode.pointers[0]);
	fseek(disk_file, sizeof(cs1550_disk_block)*cur_inode.pointers[0], SEEK_SET);
	fread(&cur_block, sizeof(cs1550_disk_block), 1, disk_file);	
	
	printf("READ(): cur_block contains: %s\n", cur_block.data);

	bool data_to_read = true;

	int i, j;
	i=0;
	j = read_start;
	int bytes_read = 0;
	/*
	while (data_to_read)
	{
		memcpy(buf+(i*MAX_DATA_IN_BLOCK), cur_block.data, MAX_DATA_IN_BLOCK);

		i++;
		j++;

		bytes_read += MAX_DATA_IN_BLOCK;
		printf("READ(): Checking if %d is >= to %d\n", bytes_read, size);
		if (bytes_read >= size-MAX_DATA_IN_BLOCK)
		{
			printf("READ(): bytes read exceeded size, BREAKING!\n");
			break;
		}
			
		fseek(disk_file, 512*cur_inode.pointers[j], SEEK_SET);
		fread(&cur_block, sizeof(cs1550_disk_block), 1, disk_file);

	}
	*/
	memset(buf, 0, sizeof(char)*MAX_DATA_IN_BLOCK);
	memcpy(buf, cur_block.data, sizeof(cur_block.data));
	//memcpy(buf, cur_block.data, size);

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error

	//size = sizeof(buf);

	printf("READ(): Buffer contains %s\n", buf);

	//size=sizeof(char)*strnlen(buf, MAX_DATA_IN_BLOCK);
	//size = sizeof(char)*MAX_DATA_IN_BLOCK;
	fclose(disk_file);
	fclose(directories_file);
	return size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	printf("WRITE(): Entering cs1550_write() with path=%s\n", path);

	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];

	memset(directory, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(filename, 0, sizeof(char)*(MAX_FILENAME+1));
	memset(extension, 0, sizeof(char)*(MAX_EXTENSION+1));

	int splice_result = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	cs1550_directory_entry cur_entry;
	memset(&cur_entry, 0, sizeof(cs1550_directory_entry));

	FILE *directories_file = fopen(".directories", "rb+");
	FILE *disk_file = fopen(".disk", "r+");

	long inode_block = 0;
	size_t bytes_read = 0;
	while(fread(&cur_entry, sizeof(cs1550_directory_entry), 1, directories_file))
	{
		if (strcmp(cur_entry.dname, directory) == 0) //if target dir found...
		{
			printf("WRITE(): Found target dir %s! Number of files contained is %d\n", cur_entry.dname, cur_entry.nFiles);
			//file_pointer_position = ftell(directories_file) - sizeof;
			int i;
			for (i = 0; i < cur_entry.nFiles; i++)
			{
				printf("WRITE(): inside of file array loop iteration %d. Comparing fname %s and passed-in filename %s as well as fext %s and passed-in extension %s\n", i, cur_entry.files[i].fname, filename, cur_entry.files[i].fext, extension);
				if ((strcmp(cur_entry.files[i].fname, filename) == 0) && (strcmp(cur_entry.files[i].fext, extension) == 0))
				{
					printf("WRITE(): FILE FOUND!\n");
					inode_block = cur_entry.files[i].nStartBlock; //we now have the location of the file's inode...	
					cur_entry.files[i].fsize+=sizeof(char)*strnlen(buf, MAX_DATA_IN_BLOCK);
					//if (inode_block == 0) //we must make a new inode, as apparently mknod() doesn't get called...
					//{
					//	printf("WRITE(): using next_available_disk_block() for the inode, as it didnt exist previously!\n");
					//	inode_block = next_available_disk_block();
					//	cur_entry.files[i].nStartBlock = inode_block;
					//}
					fseek(directories_file, bytes_read, SEEK_SET);
					printf("WRITE(): CURRENT POSITION OF FILE POINTER FOR WRITING DIR INFO IS: %d\n", ftell(directories_file));
					fwrite(&cur_entry, sizeof(cs1550_directory_entry), 1, directories_file); //write the updated directory entry back to disk
					//fseek(directories_file, sizeof(cs1550_directory_entry), SEEK_CUR);
					break;	
				}
			}
			break;
		}
		bytes_read+=sizeof(cs1550_directory_entry);
	}

	cs1550_inode cur_inode;
	memset(&cur_inode, 0, sizeof(cs1550_disk_block));
	fseek(disk_file, (512*inode_block), SEEK_SET); //file cursor is now at the location of the file's root inode...
	fread(&cur_inode, sizeof(cs1550_inode), 1, disk_file); //read in inode struct in case we have to write it back to disk later...

	printf("WRITE(): Setting block_count = to %d / MAX_DATA_IN_BLOCK, which is %d\n", size, size/MAX_DATA_IN_BLOCK);
	int block_count = size / MAX_DATA_IN_BLOCK; //we now calculate how many TOTAL blocks are going to be written to the block beginning at offset.
	bool data_to_write = true;
	bool is_new_block = false;
	int depth;

	//if (block_count == 0)
	//{
		//block_count = 1;
	//}

	if ((size % MAX_DATA_IN_BLOCK) != 0) 
	{
		//block_count++;
	}

	if (block_count < (NUM_POINTERS_IN_INODE-1))
	{
		//we just stick to the first inode
		depth = 0;
	}
	else
	{
		depth = (block_count / (NUM_POINTERS_IN_INODE-1)); //how many inodes deep does the rabbit hole go?
		if ((block_count % NUM_POINTERS_IN_INODE-1) != 0); //if remainder is anything but 0, add an additional inode to the above computed number.
		{
			depth++;
		}
	}

	int write_start = (offset / (MAX_DATA_IN_BLOCK)); //now, this is the location from which we start writing data. Location as in the number of data blocks away from the root inode...
	int byte_offset = offset % (MAX_DATA_IN_BLOCK);

	int num_allocated_blocks;
	if (write_start == 0) //in event of beginning write to new file...
	{
		cur_inode.pointers[0] = next_available_disk_block();
		num_allocated_blocks = 1;
	}
	else
	{
		num_allocated_blocks = cur_inode.children - write_start;
		num_allocated_blocks++;		
	}
	printf("WRITE(): number of allocated blocks is: %d\n", num_allocated_blocks);

	//fseek(disk_file, write_start*sizeof(cs1550_disk_block), SEEK_CUR); //we can now start writing data!
	cs1550_disk_block new_block;
	memset(&new_block, 0, sizeof(cs1550_disk_block));

	int i = 0; //i used primarily as index to buf
	int j = write_start; 
	int new_block_count; 
	//int directory_entry_block_counter = offset / MAX_DATA_IN_BLOCK;
	while (data_to_write)
	{
		if (num_allocated_blocks == 0) //we have run out of disk blocks
		{
			printf("WRITE(): FROM THIS POINT ON, new blocks must be allocated for continued writing operations\n");
			is_new_block = true;
		}

		if (is_new_block) //ELSE simply use the blocks pre-assigned to the current inode. MUST BE CAREFUL TO KEEP TRACK OF WHICH BLOCKS HAVE BEEN ALREADY BEEN ALLOCATED
		{
			printf("WRITE(): MAIN loop requesting new disk block from bitmap...\n");
			cur_inode.children++;
			cur_inode.pointers[cur_inode.children] = next_available_disk_block(); //grab a new block from the bitmap
			//directory_entry_block_counter++;
			//cur_entry.files[directory_entry_file_counter].fsize+=sizeof(cs1550_disk_block);
		}

		if (j == (NUM_POINTERS_IN_INODE-1)) //we only have to worry about making new inodes IF they havent already been laid down for us...
		{
			//write current inode to disk...its final empty pointer slot must point towards a NEW inode
			long int cur_offset = ftell(disk_file);
			fseek(disk_file, inode_block*sizeof(cs1550_disk_block), SEEK_SET); //point file ptr back to location of inode
			cur_inode.pointers[cur_inode.children] = (cur_offset / sizeof(cs1550_disk_block)); //divide by sizeof(block) to get convert offset from bytes
			fwrite(&cur_inode, sizeof(cs1550_inode), 1, disk_file);
			memset(&cur_inode, 0, sizeof(cs1550_inode)); //0 out inode struct; this effectively makes it a new inode
			fseek(disk_file, cur_offset, SEEK_SET); //return to original writing position
			fread(&cur_inode, sizeof(cs1550_inode), 1, disk_file);

			if (cur_inode.magic_number == 0xFFFFFFFF) //inode already exists
			{
				fseek(disk_file, cur_inode.pointers[0], SEEK_SET); //so move file pointer to the first datablock pointed to by cur_inode
			}
			else //we have to make an inode:
			{
				memset(&cur_inode, 0, sizeof(cs1550_inode)); //0 out the garbage memory 
				cur_inode.magic_number = 0xFFFFFFFF;
				cur_inode.children = 0;
			}

			//modify inode_block in case more inodes need it in the future:
			inode_block = (ftell(disk_file) / sizeof(cs1550_disk_block));
			i--; //adjust i b/c were not counting inodes...
		}
		else
		{
			//fseek(disk_file, 512*cur_inode.pointers[cur_inode.children], SEEK_SET);
			fseek(disk_file, cur_inode.pointers[j]*sizeof(cs1550_disk_block), SEEK_SET);
			//printf("WRITE: file pointer has landed on ")
			memset(new_block.data, 0, sizeof(char)*MAX_DATA_IN_BLOCK);
			memcpy(new_block.data, buf+(i * MAX_DATA_IN_BLOCK), MAX_DATA_IN_BLOCK); //copy from buffer into new_block
			printf("\nWRITE(): Writing new_block containing %s to disk!\n\n", new_block.data);
			new_block.magic_number = 0xF113DA7A;
			fwrite(&new_block, sizeof(cs1550_disk_block), 1, disk_file); // 
		}

		printf("checking to see if %d is equal to block_count %d\n", i, block_count);
		if (i == block_count)
		{
			printf("WRITE: EXITING LOOP!!!\n");
			break;
		}

		j++;
		i++;
		num_allocated_blocks--;
	}

	fseek(disk_file, (sizeof(cs1550_disk_block)*inode_block), SEEK_SET);
	fwrite(&cur_inode, sizeof(cs1550_inode), 1, disk_file);

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//set size (should be same as input) and return, or error


	fclose(directories_file);
	fclose(disk_file);
	return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open
    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */


    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}

/*
static int cs1550_access()
{
	return 0; //success! shut up "ls"
}
*/

//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = 
{
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
	//.access = cs1550_access
};

//reset disk bits to all 0's
static void format(void)
{
	system("dd bs=1K count=5K if=/dev/zero of=.disk");
}

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
