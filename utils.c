
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "fatfs.h"
#include "fat_defs.h"

int num_alpha(char *str)
{
	int count = 0;
	const char *c = str;
	
	while (*c)
    {
		if(isalpha((int)*c))
		{
			count++;
		}
		c++;
    }

	return count;	
}

/* 'str' is the string you want to remove characters 'c' from */
char *remove_all_chars(const unsigned char* str, unsigned char c) {
	unsigned char *copy = malloc(strlen(str)+1);
	strcpy(copy,str);
    unsigned char *pr = copy;
	unsigned char *pw = copy;
    	while (*pr) {
        	*pw = *pr++;
        	pw += (*pw != c);
		}
    	*pw = '\0';
	
	return copy;
}

/* 'replace_chars' contains characters that you want to replace with character 'replace_with' in string str */
void replace_all_chars(char **str, const char* replace_chars, unsigned char replace_with) 
{
	char *c = (char *)*str;
   
	while (*c)
	{
	   if (strchr(replace_chars, *c))
	   {
		  *c = replace_with;
	   }

	   c++;
	}
}

/* Returns the number of lowercase characters found in 'str' */
int contains_lowercase(const char *str)
{
	int count = 0;
	const char *c = str;
	
	while (*c)
    {
		if(islower((int)*c))
		{
			count++;
		}
		c++;
    }

	return count;
}

/* http://stackoverflow.com/questions/1071542/in-c-check-if-a-char-exists-in-a-char-array */
int correct_filename(const char* str)
{
   const char *invalid_characters = "\\?:*\"><|";
   const char *c = str;
   
   /* Make sure the string is long and short enough */
   if(strlen(str) > 255 || strlen(str) <= 0)
   {
#ifdef FATFS_DEBUG
       printf("Invalid filename, strlen: %d\n", strlen(str)); 
#endif
       errno = ENAMETOOLONG;
       return -1;
   }
   
   while (*c)
   {
       if (strchr(invalid_characters, *c))
       {
#ifdef FATFS_DEBUG
          printf("Invalid filename, char found: %c\n", *c); 
#endif
          errno = ENAMETOOLONG;
          return -1;
       }

       c++;
   }

   return 0;
}

char *generate_short_filename(fatfs_t *fat, node_entry_t *curdir, char * fn, int *lfn, unsigned char *res)
{
	int diff = 1;
	
	char *temp1 = NULL;
	char *temp2 = NULL;
	
	char *lower = NULL;
	char *ltemp1 = NULL;
	char *ltemp2 = NULL;
	
	char *copy = NULL;
	char *integer_string = malloc(7); 
	
	char *fn_temp;
	char *filename = malloc(strlen(fn)+1); 
	char *ext = malloc(4);      
	char *fn_final;
	
	/* 1. Remove all spaces. For example "My File" becomes "MyFile". */
	copy = remove_all_chars(fn, ' ');
	
	/* 3. Translate all illegal 8.3 characters( : + , ; = [ ] ) into "_" (underscore). For example, "The[First]Folder" becomes "The_First_Folder". */
	replace_all_chars(&copy, ":+,;=[]", '_');

	/* 2. Initial periods, trailing periods, and extra periods prior to the last embedded period are removed.
	For example ".logon" becomes "logon", "junk.c.o" becomes "junkc.o", and "main." becomes "main". */

	temp1 = malloc(strlen(fn)+1);
	
	strncpy(filename, strtok(copy, "."), strlen(fn)); /* Copy Filename */
	filename[strlen(fn)] = '\0';
	
	temp2 = strtok(NULL, ".");
	
	if(temp2 == NULL) /* No periods in name at all */
	{
		strncpy(ext, "   ", 3); /* Fill ext with spaces */
		ext[3] = '\0';
	} 
	else
	{
		strncpy(ext, temp2, 3); /* Copy Extension */
		ext[3] = '\0';
	}
	
	temp2 = strtok(NULL, ".");
	
	while(temp2 != NULL)
	{
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename);
		strcat(temp1, ext);
		
		strncpy(filename, temp1, strlen(fn)); /* Copy into Filename */
		filename[strlen(fn)] = '\0';
		
		strncpy(ext, temp2, 3);               /* Copy into extension */
		ext[3] = '\0';
		
		temp2 = strtok(NULL, ".");
	}

	while(strlen(ext) < 3)
		strcat(ext, " ");
	

/*
4. If the name does not contain an extension then truncate it to 6 characters. If the names does contain
an extension, then truncate the first part to 6 characters and the extension to 3 characters. For
example, "I Am A Dingbat" becomes "IAmADi" and not "IAmADing.bat", and "Super Duper
Editor.Exe" becomes "SuperD.Exe".*/
/*
	Truncate the filename to 6 chars(length)
	if(strlen(filename) > 6)
		filename[6] = '\0';

	if(strlen(ext) > 3)
		ext[3] = '\0';  //truncate it to 3 characters (ext)
	
	printf("Truncate - Filename: %s Extension: %s \n", filename, ext);
	*/

/*
5. If the name does not contain an extension then a "-1" is appended to the name. If the name does
contain an extension, then a "-1" is appended to the first part of the name, prior to the extension. For
example, "MyFile" becomes "MyFile-1, and "Junk.bat" becomes "Junk-1.bat". This numeric value is
always added to help reduce the conflicts in the 8.3 name space for automatic generated names. 
*/

	if(strlen(filename) > 8)
	{
		/* Concate the filename */
		memset(temp1, 0, strlen(fn)+1);
		strncpy(temp1, filename, 6);
		
		/* Append the Value(~#) */
		memset(integer_string, 0, 7);
		sprintf(integer_string, "~%d", diff++); /* Increment diff here for (maybe) future use */
		strcat(temp1, integer_string);
		
		/* Append the period */
		strcat(temp1, ".");
		
		/* Append the ext */
		strcat(temp1, ext);
		
		fn_temp = temp1; /* Contains the filename and ext */
		*lfn = 1; /* This needs a long file name entry */
	}
	else
	{
		/* Append the filename */
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename);
		
		while(strlen(temp1) < 8) 
			strcat(temp1, " ");
		
		
		/* Append the period */
		strcat(temp1, ".");
		
		/* Append the ext */
		strcat(temp1, ext);
		
		fn_temp = temp1; /* Contains the filename and ext */
	}

/*
6. This step is optional dependent on the name colliding with an existing file. To resolve collisions
the decimal number, that was set to 1, is incremented until the name is unique. The number of
characters needed to store the number will grow as necessary from 1 digit to 2 digits and so on. If the
length of the basis (ignoring the extension) plus the dash and number exceeds 8 characters then the
length of the basis is shortened until the new name fits in 8 characters. For example, if "FILENA-
1.EXE" conflicts the next names tried are "FILENA-2.EXE", "FILENA-3.EXE", ..., "FILEN-
10.EXE", "FILEN-11.EXE", etc.
*/

	/* If it contains lowercase letters, convert it into uppercase */
	if(contains_lowercase(fn_temp))
	{
		unsigned int i;
		
		lower = malloc(strlen(fn_temp)+1);
	
		strncpy(lower, fn_temp, strlen(fn_temp));
		lower[strlen(fn_temp)] = '\0';
		
		ltemp1 = strtok(lower, ".");
		
		ltemp2 = strtok(NULL, ".");
		
		if(contains_lowercase(ltemp1) > 0)
		{
			if((contains_lowercase(ltemp1) == num_alpha(ltemp1)) && *lfn == 0) /* Make sure all the letters are lower case. If not set lfn */
			{
				*res |= 0x08; /* lowercase basename without having to create longfilename */
			}
			else
			{
				*res = 0x00;
				*lfn = 1;  /* Create a lfn because not the whole filename is lower case which can be an exception. I.E. we have something like this: "Delete.txt"
							  instead of "delete.txt" */
			}
		}
		
		if(ltemp2 != NULL)
		{
			if(contains_lowercase(ltemp2) > 0)
			{
				if((contains_lowercase(ltemp2) == num_alpha(ltemp2)) && *lfn == 0)
				{
					*res |= 0x10; /* lowercase extension without having to create longfilename */
				}
				else
				{
					*res = 0x00;
					*lfn = 1;  /* Create a lfn because not the whole extenstion is lower case which can be an exception. I.E. we have something like this: "delete.Txt"
								  instead of "delete.txt" */
				}
			}
		}
		
		free(lower);
		
		for(i = 0; i < strlen(fn_temp); i++)
			fn_temp[i] = toupper((int)fn_temp[i]);
	}
	
	while(search_directory(fat, curdir, fn_temp) != NULL)
	{
		if(diff > 99999)
		{
#ifdef FATFS_DEBUG
			printf("Too many entries(Short Entry Name) with the same name \n");
#endif
			return NULL;
		}
		
		/* Rebuild the file name */
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename); /* Cat whole filename into temp1 */
		
		memset(integer_string, 0, 7);
		sprintf(integer_string, "~%d", diff++); /* Increment diff here for maybe future use */
		
		if((strlen(temp1) + strlen(integer_string)) <= 8)
		{
			strcat(temp1, integer_string);
			
			while(strlen(temp1) < 8) 
				strcat(temp1, " ");
		}
		else
		{
			/* Change temp according to the strlen of strlen(integer_string) [~##] */
			memcpy(temp1 + (8 - strlen(integer_string)), integer_string, strlen(integer_string));
		}
		
		temp1[8] = '\0';
		
		/* Append the period */
		strcat(temp1, ".");
		
		/* Append the ext */
		strcat(temp1, ext);
		
		fn_temp = temp1; /* Contains the filename and ext */
		
		/* If it contains lowercase letters, convert it into uppercase */
		if(contains_lowercase(fn_temp))
		{
			unsigned int i;
			
			lower = malloc(strlen(fn_temp)+1);
		
			strncpy(lower, fn_temp, strlen(fn_temp));
			lower[strlen(fn_temp)] = '\0';
			
			ltemp1 = strtok(lower, ".");
			
			ltemp2 = strtok(NULL, ".");
			
			if(contains_lowercase(ltemp1) > 0)
			{
				if((contains_lowercase(ltemp1) == num_alpha(ltemp1)) && *lfn == 0) /* Make sure all the letters are lower case. If not set lfn */
				{
					*res |= 0x08; /* lowercase basename without having to create longfilename */
				}
				else
				{
					*res = 0x00;
					*lfn = 1;  /* Create a lfn because not the whole filename is lower case which can be an exception. I.E. we have something like this: "Delete.txt"
								  instead of "delete.txt" */
				}
			}
			
			if(ltemp2 != NULL)
			{
				if(contains_lowercase(ltemp2) > 0)
				{
					if((contains_lowercase(ltemp2) == num_alpha(ltemp2)) && *lfn == 0)
					{
						*res |= 0x10; /* lowercase extension without having to create longfilename */
					}
					else
					{
						*res = 0x00;
						*lfn = 1;  /* Create a lfn because not the whole extenstion is lower case which can be an exception. I.E. we have something like this: "delete.Txt"
									  instead of "delete.txt" */
					}
				}
			}
			
			free(lower);
			
			for(i = 0; i < strlen(fn_temp); i++)
				fn_temp[i] = toupper((int)fn_temp[i]);
		}
	}
	
	/* Short Entry Name is unique. Now to copy it to its final string so it can fit nice and snug. */
	fn_final = malloc(strlen(fn_temp)+1);
	memset(fn_final, 0, strlen(fn_temp)+1);
	strncpy(fn_final, fn_temp, strlen(fn_temp));
	fn_final[strlen(fn_temp)] = '\0';

	/* Free memory */
	free(temp1);
	free(copy);
	free(filename);
	free(ext);
	free(integer_string);
	
	return fn_final;
}

fat_lfn_entry_t *generate_long_filename_entry(char * fn, unsigned char checksum, unsigned char order)
{
	int i;
	unsigned char *filename = malloc(13);
	fat_lfn_entry_t *lfn_entry = malloc(sizeof(fat_lfn_entry_t));
	
	strncpy(filename,fn, 13);
	
	if(strlen(filename) < 13)
	{
		filename[strlen(filename)] = '\0';
		
		for(i = strlen(filename)+1; i < 13; i++)
		{
			filename[i] = 0xFF;
		}
	}

	lfn_entry->Order = order;
	lfn_entry->Checksum = checksum;
	lfn_entry->Cluster = 0;
	
	memset(lfn_entry->FNPart1, 0, 10);
	memset(lfn_entry->FNPart2, 0, 12);
	memset(lfn_entry->FNPart3, 0, 4);
	
	/* Part One */
	if(filename[0] == 0xFF)
	{
		lfn_entry->FNPart1[0] = 0xFF;
		lfn_entry->FNPart1[1] = 0xFF;
	}
	else 
		lfn_entry->FNPart1[0] = filename[0];
		
	if(filename[1] == 0xFF)
	{
		lfn_entry->FNPart1[2] = 0xFF;
		lfn_entry->FNPart1[3] = 0xFF;
	}
	else 
		lfn_entry->FNPart1[2] = filename[1];
		
	if(filename[2] == 0xFF)
	{
		lfn_entry->FNPart1[4] = 0xFF;
		lfn_entry->FNPart1[5] = 0xFF;
	}
	else 
		lfn_entry->FNPart1[4] = filename[2];
	
	if(filename[3] == 0xFF)
	{
		lfn_entry->FNPart1[6] = 0xFF;
		lfn_entry->FNPart1[7] = 0xFF;
	}
	else 
		lfn_entry->FNPart1[6] = filename[3];
		
	if(filename[4] == 0xFF)
	{
		lfn_entry->FNPart1[8] = 0xFF;
		lfn_entry->FNPart1[9] = 0xFF;
	}
	else 
		lfn_entry->FNPart1[8] = filename[4];

	
	/* Part Two */
	if(filename[5] == 0xFF)
	{
		lfn_entry->FNPart2[0] = 0xFF;
		lfn_entry->FNPart2[1] = 0xFF;
	}
	else 
		lfn_entry->FNPart2[0] = filename[5];
		
	if(filename[6] == 0xFF)
	{
		lfn_entry->FNPart2[2] = 0xFF;
		lfn_entry->FNPart2[3] = 0xFF;
	}
	else 
		lfn_entry->FNPart2[2] = filename[6];
		
	if(filename[7] == 0xFF)
	{
		lfn_entry->FNPart2[4] = 0xFF;
		lfn_entry->FNPart2[5] = 0xFF;
	}
	else 
		lfn_entry->FNPart2[4] = filename[7];
	
	if(filename[8] == 0xFF)
	{
		lfn_entry->FNPart2[6] = 0xFF;
		lfn_entry->FNPart2[7] = 0xFF;
	}
	else 
		lfn_entry->FNPart2[6] = filename[8];
		
	if(filename[9] == 0xFF)
	{
		lfn_entry->FNPart2[8] = 0xFF;
		lfn_entry->FNPart2[9] = 0xFF;
	}
	else 
		lfn_entry->FNPart2[8] = filename[9];
		
	if(filename[10] == 0xFF)
	{
		lfn_entry->FNPart2[10] = 0xFF;
		lfn_entry->FNPart2[11] = 0xFF;
	}
	else 
		lfn_entry->FNPart2[10] = filename[10];
	
	/* Part 3 */
	if(filename[11] == 0xFF)
	{
		lfn_entry->FNPart3[0] = 0xFF;
		lfn_entry->FNPart3[1] = 0xFF;
	}
	else 
		lfn_entry->FNPart3[0] = filename[11];
		
	if(filename[12] == 0xFF)
	{
		lfn_entry->FNPart3[2] = 0xFF;
		lfn_entry->FNPart3[3] = 0xFF;
	}
	else 
		lfn_entry->FNPart3[2] = filename[12];
	
	lfn_entry->Attr = 0xF; /* Specifying it is a long name entry */

	free(filename);
	
	return lfn_entry;
}

unsigned char generate_checksum(char * short_filename)
{
	/* CheckSum
	1 	   Take the ASCII value of the first character. This is your first sum.
	2 	   Rotate all the bits of the sum rightward by one bit.
	3 	   Add the ASCII value of the next character with the value from above. This is your next sum.
	4 	   Repeat steps 2 through 3 until you are all through with the 11 characters in the 8.3 filename. 
	*/
	int i;
	char *name;
	unsigned char sum;
	
	name = remove_all_chars(short_filename, '.'); /* remove the period */

	for (sum = i = 0; i < 11; i++) {
		sum = (((sum & 1) << 7) | ((sum & 0xfe) >> 1)) + name[i];
	}

	/* This resulting checksum value is stored in each of the LFN entry to ensure that the short filename it 
	points to indeed is the currently 8.3 entry it should be pointing to. 
	*/

	free(name);
	
	return sum;
}

int write_entry(fatfs_t *fat, void * entry, unsigned char attr, int loc[])
{
	fat_lfn_entry_t *lfn_entry;
	fat_dir_entry_t *f_entry;
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */
	time_t rawtime;
	short int tme = 0;
	short int date = 0;
	struct tm * timeinfo;
	
	time (&rawtime);
	timeinfo = localtime (&rawtime);
	
	if(attr == 0x0F)
	{
		lfn_entry = entry;
	}
	else
	{
		f_entry = entry;
	}
	
	/* Clear out before reading into */
	memset(sector, 0, 512*sizeof(unsigned char));
		
	/* Read it */
	fat->dev->read_blocks(fat->dev, loc[0], 1, sector); 
	
	if(attr == 0x0F) /* Long file entry */
	{
		memcpy(sector + loc[1] + ORDER, &(lfn_entry->Order), 1);
		memcpy(sector + loc[1] + FNPART1, lfn_entry->FNPart1, 10);
		memcpy(sector + loc[1] + ATTRIBUTE, &(lfn_entry->Attr), 1);
		memcpy(sector + loc[1] + CHECKSUM, &(lfn_entry->Checksum), 1);
		memcpy(sector + loc[1] + FNPART2, lfn_entry->FNPart2, 12);
		memcpy(sector + loc[1] + FNPART3, lfn_entry->FNPart3, 4);
	}
	else             /* File/folder entry */
	{
		tme = generate_time(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec/2);
		date = generate_date(1900 + timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
		
		memcpy(sector + loc[1] + FILENAME, f_entry->FileName, 8);
		memcpy(sector + loc[1] + EXTENSION, f_entry->Ext, 3);
		memcpy(sector + loc[1] + ATTRIBUTE, &(f_entry->Attr), 1);
		memcpy(sector + loc[1] + RESERVED, &(f_entry->Res), 1);
		
		memcpy(sector + loc[1] + CREATIONTIME, &(tme), 2);
		memcpy(sector + loc[1] + CREATIONDATE, &(date), 2);
		memcpy(sector + loc[1] + LASTACCESSDATE, &(date), 2);
		memcpy(sector + loc[1] + LASTWRITETIME, &(tme), 2);
		memcpy(sector + loc[1] + LASTWRITEDATE, &(date), 2);
		
		memcpy(sector + loc[1] + STARTCLUSTERHI, &(f_entry->FstClusHI), 2);
		memcpy(sector + loc[1] + STARTCLUSTERLOW, &(f_entry->FstClusLO), 2);
		memcpy(sector + loc[1] + FILESIZE, &(f_entry->FileSize), 4);
		
	}
	
	/* Write it back */
	fat->dev->write_blocks(fat->dev, loc[0], 1, sector);

	free(sector);
	
	return 0;
}

int *get_free_locations(fatfs_t *fat, node_entry_t *curdir, int num_entries)
{
	int i;
	int j;
	int empty_entry_count = 0;
	int sector_loc;
	int ptr_loc;
	int *locations = malloc(sizeof(int)*2);
	unsigned char  *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */
	unsigned int cur_cluster = curdir->StartCluster;
	
	locations[0] = -1;
	locations[1] = -1;
	
#ifdef FATFS_DEBUG
	printf("Trying to create the file in this directory: %s\n", curdir->Name);
#endif
	
	/* Dealing with root directory (FAT16 only) */
	if(cur_cluster == 0 && fat->fat_type == FAT16)  /* Fat16 root directory has a constant amount of memory to build entries while fat32's root directory uses clusters(expandable) */
	{
		for(i = 0; i < fat->root_dir_sectors_num; i++) {
			
			/* Get the sector location */
			sector_loc = fat->root_dir_sec_loc + i;
			
			/* Read it */
			fat->dev->read_blocks(fat->dev, sector_loc, 1, sector); 
			
			ptr_loc = 0;
				
			/* Find the ptr loc */
			for(j = 0; j < fat->boot_sector.bytes_per_sector/32; j++)
			{
				if(sector[ptr_loc] == 0x00 || sector[ptr_loc] == 0xe5)
				{
					empty_entry_count++;
					
					if(empty_entry_count == num_entries)
					{
						//printf("Root -- Found location with %d free entries \n", num_entries);
						if(ptr_loc < ((num_entries - 1)* 32)) /* Corner case: traveling across sectors to fill the case */
						{
							locations[0] = sector_loc - 1;  /* Go to previous sector */ 
							locations[1] = fat->boot_sector.bytes_per_sector - (((num_entries - 1)*32) - ptr_loc);
						}
						else
						{
							locations[0] = sector_loc;
							locations[1] = ptr_loc - ((num_entries-1)*32);
						}
						
						goto finish; /* OMG!@!!$!!@ Yes, its a goto. Used correctly. */
					}
				}
				else
				{
					empty_entry_count = 0; /* Restart entry count */
				}
				
				ptr_loc += 32;
			}
		}
	}
	else
	{
		/* Go through each cluster that belongs to this folder */
		while((cur_cluster < 0xFFF8 && fat->fat_type == FAT16)
		   || (cur_cluster < 0xFFFFFF8 && fat->fat_type == FAT32)) 
		{
			/* Go through each sector in the cluster */
			for(i = 0; i < fat->boot_sector.sectors_per_cluster; i++)
			{
				/* Get the sector location */
				sector_loc = fat->data_sec_loc + (cur_cluster - 2) * fat->boot_sector.sectors_per_cluster + i;
				
				/* Read it */
				fat->dev->read_blocks(fat->dev, sector_loc, 1, sector); 
				
				ptr_loc = 0;
				
				/* Find the ptr loc */
				for(j = 0; j < fat->boot_sector.bytes_per_sector/32; j++)
				{
					if(sector[ptr_loc] == 0x00 || sector[ptr_loc] == 0xe5)
					{
						empty_entry_count++;
						
						if(empty_entry_count == num_entries)
						{
							//printf("Other -- Found location with %d free entries \n", num_entries);
							if(ptr_loc < ((num_entries - 1)* 32)) /* Corner case: traveling across sectors to fill the case */
							{
								locations[0] = sector_loc - 1;  /* Go to previous sector */
								locations[1] = fat->boot_sector.bytes_per_sector - (((num_entries - 1)*32) - ptr_loc);
							}
							else
							{
								locations[0] = sector_loc;
								locations[1] = ptr_loc - ((num_entries-1)*32);
							}
							
							goto finish; /* OMG!@!!$!!@ Yes, its a goto. Used correctly. */
						}
					}
					else
					{
						empty_entry_count = 0; /* Restart entry count */
					}
					
					ptr_loc += 32;
				}
			}
			
			/* Cant go across clusters to fulfill case */
			empty_entry_count = 0;  
			cur_cluster = read_fat_table_value(fat, cur_cluster*fat->byte_offset);
		}
	}
	finish:
	
	/* Didnt find one? Allocate a new cluster for this folder and set loc accordingly. */
	if(locations[0] == -1 && locations[1] == -1)
	{
		
#ifdef FATFS_DEBUG
		printf("Couldn't find the free entries. Allocating a Cluster...\n");
#endif
		
		cur_cluster = allocate_cluster(fat, curdir->EndCluster);
		curdir->EndCluster = cur_cluster;
		
		clear_cluster(fat, cur_cluster);
		
		locations[0] = fat->data_sec_loc + ((cur_cluster - 2) * fat->boot_sector.sectors_per_cluster);   
		locations[1] = 0;
	}
	
	free(sector);
	
	return locations;
}

void clear_cluster(fatfs_t *fat, int cluster_num)
{
	int i;
	int sector_loc;
	unsigned char *empty = malloc(512*sizeof(unsigned char));
	
	memset(empty, 0, 512*sizeof(unsigned char));
	
	for(i = 0; i < fat->boot_sector.sectors_per_cluster; i++)
	{
		sector_loc = fat->data_sec_loc + ((cluster_num - 2) * fat->boot_sector.sectors_per_cluster) + i;   
		fat->dev->write_blocks(fat->dev, sector_loc, 1, empty);
	}
	
	free(empty);
}

short int generate_time(int hour, int minutes, int seconds)
{
/*
 <------- 0x17 --------> <------- 0x16 -------->
15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
h  h  h  h  h  m  m  m  m  m  m  x  x  x  x  x
*/
	char h = hour;
	char m = minutes;
	char s = seconds;
	short int time = 0;
	
	time = (((h & 0x1F) << 11) | ((m & 0x3F) << 5) | (s & 0x1F));
	
	return time;
}

short int generate_date(int year, int month, int day)
{
/*
<------- 0x19 --------> <------- 0x18 -------->
15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
y  y  y  y  y  y  y  m  m  m  m  d  d  d  d  d
*/

	char y = year - 1980;
	char m = month;
	char d = day;
	short int date = 0;
	
	date = (((y & 0x7F) << 9) | ((m & 0x0F) << 5) | (d & 0x1F));
	
	return date;
}

int strcasecmp( const char *s1, const char *s2 )
{
	int c1, c2;
	for(;;)
	{
		c1 = tolower( (unsigned char) *s1++ );
		c2 = tolower( (unsigned char) *s2++ );
		if (c1 == 0 || c1 != c2)
			return c1 - c2;
	}
}

unsigned int end_cluster(fatfs_t *fat, unsigned int start_cluster)
{
	unsigned int clust = start_cluster;
	unsigned int value = clust;
	
	if(clust == 0)
		return 0;
	
	while((clust < 0xFFF8 && fat->fat_type == FAT16)
	   || (clust < 0xFFFFFF8 && fat->fat_type == FAT32))
	{
		value = clust;
		clust = read_fat_table_value(fat, clust*fat->byte_offset);
	}
	
	return value;
}