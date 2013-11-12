
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "utils.h"
#include "fatfs.h"

#include "cache.h"
#include "dir_entry.h"

unsigned char * extract_long_name(fat_lfn_entry_t *lfn) 
{
	int i;
	
	unsigned char temp[2];
	unsigned char buf[14];
	unsigned char *final = NULL;
	
	memset(temp, 0, sizeof(unsigned char)*2);
	memset(buf, 0, sizeof(unsigned char)*14);
	
	/* Get first five ascii */
	for(i = 0; i < 5; i++) 
	{
		memcpy(temp, &lfn->FNPart1[i*2], 1);
		strcat(buf, temp);
	}
	
	/* Next six ascii */
	for(i = 0; i < 6; i++) 
	{
		memcpy(temp, &lfn->FNPart2[i*2], 1);
		strcat(buf, temp);
	}
	
	/* Last two ascii */
	for(i = 0; i < 2; i++) 
	{
		memcpy(temp, &lfn->FNPart3[i*2], 1);
		strcat(buf, temp);
	}
	
	buf[13] = '\0';
	
	/* Remove filler chars at end of the long file name */
	final = remove_all_chars(buf, 0xFF);
	
	return final;
}

int fat_read_data(fatfs_t *fat, node_entry_t *file, unsigned char **buf, int count, int pointer)
{
	int i;
	int ptr = pointer;
	int cnt = count;
	int numOfSector = 0;
	int clusterNodeNum = 0;
	int curSectorPos = 0;
	int sector_loc = 0;  
	int numToRead = 0;
	const int bytes_per_sector = fat->boot_sector.bytes_per_sector;
	 
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */

	/* Clear */
	memset(*buf, 0, sizeof(unsigned char)*count);

	/* While we still have more to read, do it */
	while(cnt)
	{
		/* Get the number of the sector in the cluster we want to read */
		numOfSector = ptr / bytes_per_sector;

		/* Figure out which cluster we are reading from */
		clusterNodeNum = numOfSector / fat->boot_sector.sectors_per_cluster;

		/* If new cluster is needed, get it */
		if(file->NumCluster != clusterNodeNum)
		{
			if((file->NumCluster+1) == clusterNodeNum) /* If its just the next cluster, only have to read fat table once */
			{
				file->CurrCluster = read_fat_table_value(fat, file->CurrCluster*fat->byte_offset);
				file->NumCluster++;
			}
			else /* Its not the next cluster, find it starting from the beginning */
			{
				/* Set to first cluster */
				file->CurrCluster = file->StartCluster; 
		
				/* Advance to the cluster we want to read from. */
				for(i = 0; i < clusterNodeNum; i++) 
				{
					file->CurrCluster = read_fat_table_value(fat, file->CurrCluster*fat->byte_offset);
				}
				
				file->NumCluster = clusterNodeNum;
			}
		}

		/* Calculate Sector Location from cluster and sector we want to read */
		sector_loc = fat->data_sec_loc + ((file->CurrCluster - 2) * fat->boot_sector.sectors_per_cluster) + (numOfSector - (clusterNodeNum * fat->boot_sector.sectors_per_cluster)); 

		/* Read 1 sector */
		if(fat->dev->read_blocks(fat->dev, sector_loc, 1, sector) != 0)
		{
#ifdef FATFS_DEBUG
			printf("fat_read_data(dir_entry.c): Couldn't read the sector %d\n", sector_loc);
#endif
			return -1;
		}

		/* Calculate current byte position in the sector we want to start reading from */
		curSectorPos = ptr - (numOfSector * bytes_per_sector);

		/* Calculate the number of chars to read */
		numToRead = ((bytes_per_sector - curSectorPos) > cnt) ? cnt : (bytes_per_sector - curSectorPos);

		/* Concat */
		strncat(*buf, sector + curSectorPos, numToRead);

		/* Advance variables */
		ptr += numToRead;  /* Advance file pointer */
		cnt -= numToRead;  /* Decrease bytes left to read */
	}
	
	free(sector);

	return 0;
}

int fat_write_data(fatfs_t *fat, node_entry_t *file, unsigned char *bbuf, int count, int pointer)
{
	int i;
	int ptr = pointer;
	int cnt = count;
	int numOfSector = 0;
	int clusterNodeNum = 0;
	int curSectorPos = 0;
	int sector_loc = 0;  
	int numToWrite = 0;
	unsigned char *buf = bbuf;
	const int bytes_per_sector = fat->boot_sector.bytes_per_sector;
	
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */ 

	/* While we still have more to write, do it */
	while(cnt)
	{
		/* Get the number of the sector in the cluster we want to write to */
		numOfSector = ptr / bytes_per_sector;

		/* Figure out which cluster we are writing to */
		clusterNodeNum = numOfSector / fat->boot_sector.sectors_per_cluster;
		
		/* This file has no clusters allocated to it, allocate one */
		if(file->StartCluster == 0)
		{
			file->StartCluster = allocate_cluster(fat, 0);
			file->EndCluster = file->StartCluster;
			file->CurrCluster = file->StartCluster;
			file->NumCluster = 0;
		}
		else
		{
			/* If new cluster is needed, get it */
			if(file->NumCluster != clusterNodeNum)
			{
				if((file->NumCluster+1) == clusterNodeNum) /* If its just the next cluster, only have to read fat table once */
				{
					file->CurrCluster = read_fat_table_value(fat, file->CurrCluster*fat->byte_offset);
					
					/* Check if the next cluster exists. If it doesn't then allocate another cluster for this file and break */
					if((fat->fat_type == FAT16 && file->CurrCluster >= 0xFFF8)
					|| (fat->fat_type == FAT32 && file->CurrCluster >= 0xFFFFFF8))
					{
						if((file->CurrCluster = allocate_cluster(fat, file->EndCluster)) == 0)
						{
#ifdef FATFS_DEBUG
							printf("fat_write_data(dir_entry.c): All out of clusters to Allocate\n");
#endif
							return -1;
						}
						file->EndCluster = file->CurrCluster;
					}
					
					file->NumCluster++;
				}
				else
				{
					/* Set to first cluster */
					file->CurrCluster = file->StartCluster; 
			
					/* Advance to the cluster we want to write to */
					for(i = 0; i < clusterNodeNum; i++) 
					{
						/* Grab the next cluster */
						file->CurrCluster = read_fat_table_value(fat, file->CurrCluster*fat->byte_offset);
						
						/* Check if the next cluster exists. If it doesn't then allocate another cluster for this file and break */
						if((fat->fat_type == FAT16 && file->CurrCluster >= 0xFFF8)
						|| (fat->fat_type == FAT32 && file->CurrCluster >= 0xFFFFFF8))
						{
							if((file->CurrCluster = allocate_cluster(fat, file->EndCluster)) == 0)
							{
#ifdef FATFS_DEBUG
								printf("fat_write_data(dir_entry.c): All out of clusters to Allocate\n");
#endif
								return -1;
							}
							file->EndCluster = file->CurrCluster;
							break;
						}
					}
					
					file->NumCluster = clusterNodeNum;
				}
			}
		}

		/* Calculate Sector Location from cluster and sector we want to read and then write to */
		sector_loc = fat->data_sec_loc + ((file->CurrCluster - 2) * fat->boot_sector.sectors_per_cluster) + (numOfSector - (clusterNodeNum * fat->boot_sector.sectors_per_cluster)); 
		
		/* Read 1 sector */
		if(fat->dev->read_blocks(fat->dev, sector_loc, 1, sector) != 0)
		{
#ifdef FATFS_DEBUG
			printf("fat_write_data(dir_entry.c): Couldn't read the sector %d\n", sector_loc);
#endif
			return -1;
		}

		/* Calculate current byte position in the sector we want to start writing to */
		curSectorPos = ptr - (numOfSector * bytes_per_sector);

		/* Calculate the number of chars to write */
		numToWrite = ((bytes_per_sector - curSectorPos) > cnt) ? cnt : (bytes_per_sector - curSectorPos);

		/* Memcpy */
		memcpy(sector + curSectorPos, buf, numToWrite);

		/* Write one sector */
		if(fat->dev->write_blocks(fat->dev, sector_loc, 1, sector) != 0)
		{
#ifdef FATFS_DEBUG
			printf("fat_write_data(dir_entry.c): Couldnt write the sector %d\n", sector_loc);
#endif
			return -1;
		}

		/* Advance variables */
		ptr += numToWrite;  /* Advance file pointer */
		buf += numToWrite;  /* Advance buf pointer */
		cnt -= numToWrite;  /* Decrease bytes left to write */
	}
	
	free(sector);

	return 0;
}

void update_sd_entry(fatfs_t *fat, node_entry_t *file)
{	
	short clusthi = 0;
	short clustlo = 0;
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */
	
	time_t rawtime;
	short tme = 0;
	short date = 0;
	struct tm * timeinfo;

	time (&rawtime);
	timeinfo = localtime(&rawtime);
	
	tme = generate_time(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec/2);
	date = generate_date(1900 + timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
	
	/* Read sector */
	if(fat->dev->read_blocks(fat->dev, file->Location[0], 1, sector) != 0)
	{
#ifdef FATFS_DEBUG
		printf("update_sd_entry(dir_entry.c): Couldn't read the sector %d\n", file->Location[0]);
#endif
		return;
	}

	/* Edit Entry */
	memcpy(sector + file->Location[1] + FILESIZE, &(file->FileSize), 4); 
	
	clusthi = (file->StartCluster >> 16);
	clustlo = (file->StartCluster & 0xFFFF);
	memcpy(sector + file->Location[1] + STARTCLUSTERHI, &(clusthi), 2);
	memcpy(sector + file->Location[1] + STARTCLUSTERLOW, &(clustlo), 2);
	memcpy(sector + file->Location[1] + LASTACCESSDATE, &(date), 2);
	memcpy(sector + file->Location[1] + LASTWRITETIME, &(tme), 2);
	memcpy(sector + file->Location[1] + LASTWRITEDATE, &(date), 2);

	/* Write it back */
	if(fat->dev->write_blocks(fat->dev, file->Location[0], 1, sector) != 0)
	{
#ifdef FATFS_DEBUG
		printf("update_sd_entry(dir_entry.c): Couldn't write the sector %d\n", file->Location[0]);
#endif
		return;
	}
	
	free(sector);
}

void delete_struct_entry(node_entry_t * node)
{
	if(node == NULL)
		return;
		
    free(node->Name);      /* Free the name */
	free(node->ShortName); /* Free the shortname */
	free(node);
    
    node = NULL;
}

unsigned int allocate_cluster(fatfs_t *fat, unsigned int end_clust)
{
    unsigned int fat_index = fat->next_free_fat_index;
	
	unsigned int cap = fat_index;
	unsigned int marker = (fat->fat_type == FAT16) ? 0xFFFF : 0x0FFFFFFF;
    unsigned int cluster_num = 0;

    /* Search for a free cluster starting at index 2 or the last index where we found a free cluster(makes future calls faster) */
    while(fat_index < fat->boot_sector.bytes_per_sector*fat->table_size) { /* Go through Table one index at a time */
	
		cluster_num = read_fat_table_value(fat, fat_index*fat->byte_offset);
        
        /* If we found a free entry */
        if(cluster_num == 0x00) 
        {
#ifdef FATFS_DEBUG
            printf("allocate_cluster(dir_entry.c): Found a free cluster entry -- Index: %d\n", fat_index); 
#endif
            if(end_clust != 0) /* Cant change what doesnt exist */
            {
				write_fat_table_value(fat, end_clust*fat->byte_offset, fat_index);  /* Change the table to indicate an allocated cluster */
			}
			write_fat_table_value(fat, fat_index*fat->byte_offset, marker);     /* Put the marker(0xFFFF or 0x0FFFFFFF) at the allocated cluster index */
			
			fat->next_free_fat_index = fat_index; /* Save this index for next time */
			
            return fat_index;
        }
      
        fat_index++;
    }
	
	/* In case we did not find one above and started from a fat_index that was not 2 */
	for(fat_index = 2;fat_index < cap; fat_index++)
	{
		cluster_num = read_fat_table_value(fat, fat_index*fat->byte_offset);
        
        /* If we found a free entry */
        if(cluster_num == 0x00) 
        {
#ifdef FATFS_DEBUG
            printf("allocate_cluster(dir_entry.c): Found a free cluster entry -- Index: %d\n", fat_index); 
#endif
            if(end_clust != 0) /* Cant change what doesnt exist */
            {
				write_fat_table_value(fat, end_clust*fat->byte_offset, fat_index);  /* Change the table to indicate an allocated cluster */
			}
			write_fat_table_value(fat, fat_index*fat->byte_offset, marker);     /* Put the marker(0xFFFF or 0x0FFFFFFF) at the allocated cluster index */
            
			fat->next_free_fat_index = fat_index; /* Save this index for next time */
			
            return fat_index;
        }
	}
	
#ifdef FATFS_DEBUG
	printf("allocate_cluster(dir_entry.c): Didnt find a free Cluster\n");
#endif
    
    /* Didn't find a free cluster */
    return 0;
}


void delete_cluster_list(fatfs_t *fat, node_entry_t *f)
{
	const short int clear = 0;
	unsigned int clust = f->StartCluster;
	unsigned int value;
	
	if(clust == 0)
		return;
	
	while((fat->fat_type == FAT16 && clust < 0xFFF8)
      || (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
	{
		value = read_fat_table_value(fat, clust*fat->byte_offset);
		write_fat_table_value(fat, clust*fat->byte_offset, clear);
#ifdef FATFS_DEBUG
		printf("delete_cluster_list(dir_entry.c) Freed Cluster: %d\n", clust);
#endif
		clust = value;
	}
	
	f->StartCluster = 0;
	f->EndCluster = 0;
}

int generate_and_write_entry(fatfs_t *fat, char *entry_name, node_entry_t *newfile, node_entry_t *parent)
{
	int i;
	int *loc = NULL;
	int last;
	unsigned char order = 1;
	unsigned int offset = 0;
	
	char *shortname = NULL;
	unsigned char checksum;
	
	int longfilename = 0;
	unsigned char res = 0x00;

    fat_dir_entry_t entry;
	fat_lfn_entry_t *lfn_entry_list[20];  /* Can have a maximum of 20 long file name entries */
	
	for(i = 0; i < 20; i++)
		lfn_entry_list[i] = NULL;
    
    shortname = generate_short_filename(fat, parent, entry_name, &longfilename, &res);
	checksum = generate_checksum(shortname);
	
	newfile->ShortName = malloc(strlen(shortname) + 1);
	strncpy(newfile->ShortName, shortname, strlen(shortname));
	newfile->ShortName[strlen(shortname)] = '\0';
    
	/* Make lfn entries */
	if(longfilename)
    {
		i = 0;
		
		while(offset < strlen(entry_name))
		{
			/* Build long name entries */
			lfn_entry_list[i++] = generate_long_filename_entry(entry_name+offset, checksum, order++);
			offset += 13;
		}
		
		last = i - 1; /* Refers to last entry in array */
		
		lfn_entry_list[last]->Order |= 0x40; /* Set(OR) last one to special value order to signify it is the last lfn entry */
		
		/* Get loc for order amount of entries plus 1(shortname entry). Returns an int array Sector(loc[0]), ptr(loc[1]) */
		loc = get_free_locations(fat, parent, (int)order);
		
		/* Write it(reverse order) */
		for(i = last; i >= 0; i--)
		{
			write_entry(fat, lfn_entry_list[i], LONGFILENAME, loc);
				
			/* Do calculations for sector if need be */
			loc[1] += 32;
			
			if((loc[1]/fat->boot_sector.bytes_per_sector) >= 1)
			{
				loc[0]++;   /* New sector */
				loc[1] = 0; /* Reset ptr in new sector */
			}
		}
    }
	
	/* Allocate a cluster. Folders Only. */
	if(newfile->Attr & DIRECTORY)
	{
		newfile->StartCluster = allocate_cluster(fat, 0);
		newfile->EndCluster = newfile->StartCluster;
		clear_cluster(fat, newfile->StartCluster);
		
		if(fat->fat_type == FAT32)
			set_fsinfo_nextfree(fat); /* Write it to FSInfo sector which only exists for Fat32 */
	}
	
	/* Make regular entry and write it to SD FAT */
	strncpy(entry.FileName, shortname, 8);
	entry.FileName[8] = '\0';
	strncpy(entry.Ext, shortname+9, 3 ); /* Skip filename and '.' */
	entry.Ext[3] = '\0';
	entry.Attr = newfile->Attr;
	entry.Res = res;
	entry.FileSize = 0;   /* For both new files and new folders */
	
	if(newfile->Attr & DIRECTORY) /* Folder */
	{
		entry.FstClusHI = newfile->StartCluster >> 16;
		entry.FstClusLO = newfile->StartCluster & 0xFFFF; 
	}
	else /* File */
	{
		entry.FstClusHI = 0;
		entry.FstClusLO = 0;  
	}
	
	/* Write it (after long file name entries) */
	if(longfilename)
	{
		write_entry(fat, &entry, newfile->Attr, loc);
	} else
	{
		loc = get_free_locations(fat, parent, 1);
		write_entry(fat, &entry, newfile->Attr, loc);
	}
	
	/* Save the locations */
	newfile->Location[0] = loc[0];  
	newfile->Location[1] = loc[1]; 
	
	free(shortname);
	free(loc);
    
    return 0;
}

void delete_sd_entry(fatfs_t *fat, node_entry_t *file)
{
	unsigned int sector_loc = file->Location[0];
	int ptr = file->Location[1];
	unsigned char sector[512];
	
	/* Delete if from cache */
	delete_from_cache(file);
	
	/* Delete Long file name entries (if any) */
	ptr -= 32;
	
	if(ptr < 0)
	{
		sector_loc -= 1;
		ptr = fat->boot_sector.bytes_per_sector - 32;
	}

	/* Read fat sector */
	fat->dev->read_blocks(fat->dev, sector_loc, 1, sector);

	/* Edit Entry */
	while((sector+ptr)[ATTRIBUTE] == LONGFILENAME && (sector+ptr)[0] != 0xE5)
	{
		(sector+ptr)[0] = 0xE5;
		ptr -= 32;
	
		if(ptr < 0)
		{
			fat->dev->write_blocks(fat->dev, sector_loc, 1, sector);
			
			sector_loc -= 1;
			ptr = fat->boot_sector.bytes_per_sector - 32;
			fat->dev->read_blocks(fat->dev, sector_loc, 1, sector);
		}
	}
	
	if(sector_loc != file->Location[0]) /* Dont write and read the same block if we dont have to */
	{
		/* Write it back */
		fat->dev->write_blocks(fat->dev, sector_loc, 1, sector);
			
		/* Read a diff sector */
		fat->dev->read_blocks(fat->dev, file->Location[0], 1, sector);
	}
	
	/* Delete file/folder entry */
	(sector + file->Location[1])[0] = 0xE5;
	
	fat->dev->write_blocks(fat->dev, file->Location[0], 1, sector);
}

node_entry_t *fat_search_by_path(fatfs_t *fat, const char *fn)
{	
	bool in_cache = false;
	char *ufn = NULL;
	unsigned char *pch = NULL;
	unsigned char *filepath = NULL;
	
	node_entry_t *temp = NULL;
	node_entry_t *rv = NULL;
	
	printf("Searching for %s\n", fn);
	
	/* Check if it in cache or cache contains some path of filepath */
	if((temp = check_cache(fat, fn, &ufn)) != NULL)
	{
		in_cache = true;
	}
	else /* Otherwise..start from root */
	{
		/* Make a copy of filepath */
		ufn = malloc(strlen(fn)+1);
		memcpy(ufn, fn, strlen(fn));
		ufn[strlen(fn)] = '\0';
		
		/* Build Root */
		temp = malloc(sizeof(node_entry_t));
		temp->Name = (unsigned char *)remove_all_chars(fat->mount, '/');          /*  */
		temp->ShortName = (unsigned char *)remove_all_chars(fat->mount, '/');
		temp->Attr = DIRECTORY;           /* Root directory is obviously a directory */
		temp->StartCluster = fat->root_cluster_num;     /* Root directory has no data clusters associated with it(FAT16). Non-NULL with FAT32 */
		temp->EndCluster = end_cluster(fat, temp->StartCluster);
	}
	
	rv = temp;
	
	pch = strtok(ufn,"/");  /* Grab next element */

	while(pch != NULL) {
		if((rv = search_directory(fat, temp, pch)) != NULL) 
		{
			filepath = realloc(filepath, strlen(filepath) + strlen(pch) + 2);
			memset(filepath+strlen(filepath), 0, strlen(pch) + 2);
			strcat(filepath, pch);
			strcat(filepath, "/");
		
			pch = strtok (NULL, "/");
			delete_struct_entry(temp);
			temp = rv;
		}
		else 
		{		
#ifdef FATFS_DEBUG
			printf("Couldnt find \"%s\" in \"%s\"\n", pch, temp->Name);
#endif
			/* Save last 'temp' in cache */
			filepath[strlen(filepath)-1] = '\0'; /* Erase last '/' from filepath */
			add_to_cache(filepath, temp);
			delete_struct_entry(temp);
			
			free(ufn);
			free(filepath);
			return NULL;
		}
	}
	
	/* Save entry in cache? dont accept root */
	if(strcasecmp(rv->Name, fat->mount) != 0 && !in_cache)
		add_to_cache(fn, rv);
	
	free(ufn);
	
	if(filepath != NULL)
		free(filepath);
	
	return rv;
}

node_entry_t *create_entry(fatfs_t *fat, const char *fn, unsigned char attr)
{
	int loc[2];

    char *pch = NULL;
    char *filename = NULL;
	char *ufn = NULL;
    
    node_entry_t *newfile;
	node_entry_t *temp = NULL;
	node_entry_t *rv = NULL;
	
	fat_dir_entry_t entry;
	
	/* Check if it in cache or cache contains some path of filepath */
	temp = check_cache(fat, fn, &ufn);
	
	if(temp == NULL)
	{
		/* Make a copy of fn */
		ufn = malloc(strlen(fn)+1);
		memcpy(ufn, fn, strlen(fn));
		ufn[strlen(fn)] = '\0';
		
		/* Build Root */
		temp = malloc(sizeof(node_entry_t));
		temp->Name = (unsigned char *)remove_all_chars(fat->mount, '/');          /*  */
		temp->ShortName = (unsigned char *)remove_all_chars(fat->mount, '/');
		temp->Attr = DIRECTORY;          /* Root directory is obviously a directory */
		temp->StartCluster = fat->root_cluster_num;    /* Root directory has no data clusters associated with it(FAT16). Non-NULL with FAT32 */
		temp->EndCluster = end_cluster(fat, temp->StartCluster);
	}
	
	rv = temp;
	
	pch = strtok(ufn,"/");  /* Grab next element */
	
	while(pch != NULL) {
		if((rv = search_directory(fat, temp, pch)) != NULL) 
		{
			if(rv->Attr & READ_ONLY) /* Check and make sure directory is not read only. */
			{
				free(ufn);
				errno = EROFS;
				delete_struct_entry(temp);
				delete_struct_entry(rv);
				return NULL;
			}
			
			pch = strtok (NULL, "/");
			delete_struct_entry(temp);
			temp = rv;
		}
		else 
		{	
			filename = pch; /* We possibly have the filename */
			
			/* Return NULL if we encounter a directory that doesn't exist */
			if((pch = strtok (NULL, "/"))) { /* See if there is more to parse through */
				free(ufn);
				errno = ENOTDIR;
				delete_struct_entry(temp);
				return NULL;               /* if there is that means we are looking in a directory */
			}                              /* that doesn't exist. */
			
			/* Make sure the filename is valid */
			if(correct_filename(filename) == -1) {
				free(ufn);
				delete_struct_entry(temp);
				return NULL;
			}
	   
			/* Create file/folder */
			newfile = (node_entry_t *) malloc(sizeof(node_entry_t));
			
			newfile->Name = malloc(strlen(filename)+1); 
			memcpy(newfile->Name, filename, strlen(filename));
			newfile->Name[strlen(filename)] = '\0';
			
			newfile->Attr = attr;
			newfile->FileSize = 0;
			newfile->StartCluster = 0; 
			newfile->EndCluster = 0;
			
			if(generate_and_write_entry(fat, newfile->Name, newfile, temp) == -1)
			{
#ifdef FATFS_DEBUG
				printf("create_entry(dir_entry.c) : Didnt Create file/folder named %s\n", newfile->Name);
#endif
				free(ufn);
				errno = EDQUOT;
				delete_struct_entry(temp);
				delete_struct_entry(newfile);  /* This doesn't take care of removing clusters from SD card */
				
				return NULL;  
			}
			
			/* If its a folder, add the hidden files "." and ".." */
			if(attr & DIRECTORY)
			{
				/* Add '.' folder entry */ 
				strncpy(entry.FileName, ".       ", 8);
				entry.FileName[8] = '\0';
				strncpy(entry.Ext, "   ", 3 ); 
				entry.Ext[3] = '\0';
				entry.Attr = DIRECTORY;
				entry.FileSize = 0;   
				entry.FstClusHI = newfile->StartCluster >> 16;
				entry.FstClusLO = newfile->StartCluster & 0xFFFF;
				
				loc[0] = fat->data_sec_loc + (newfile->StartCluster - 2) * fat->boot_sector.sectors_per_cluster;
				loc[1] = 0;
				write_entry(fat, &entry, DIRECTORY, loc);

				/* Add '..' folder entry */
				strncpy(entry.FileName, "..      ", 8);
				entry.FileName[8] = '\0';
				strncpy(entry.Ext, "   ", 3 ); 
				entry.Ext[3] = '\0';
				entry.Attr = DIRECTORY;
				entry.FileSize = 0;   
				if(strcasecmp(temp->Name, fat->mount) == 0) /* If parent of this folder is the root directory, set first cluster number to 0 */
				{
					entry.FstClusHI = 0;
					entry.FstClusLO = 0;
				}
				else 
				{
					entry.FstClusHI = temp->StartCluster >> 16;
					entry.FstClusLO = temp->StartCluster & 0xFFFF; 
				}
					
				loc[1] = 32; /* loc[0] Doesnt change */
				write_entry(fat, &entry, DIRECTORY, loc);
			}
			
			add_to_cache(fn, newfile);
			
			free(ufn);
			delete_struct_entry(temp);
			
			return newfile;
		}
	}
	
	free(ufn);
	delete_struct_entry(temp);
	
	 /* Path was invalid from the beginning */
    errno = ENOTDIR;
    return NULL;
}

node_entry_t *search_directory(fatfs_t *fat, node_entry_t *node, const char *fn)
{
	int i;
	unsigned int sector_loc = 0;
	
	unsigned int clust = node->StartCluster;
	node_entry_t    *rv = NULL;

	/* If the directory is the root directory and if fat->fat_type = FAT16 consider it a special case */
	if(fat->fat_type == FAT16 && strcasecmp(node->Name, fat->mount) == 0) /* Go through special(static number) sectors. No clusters. */
	{
		for(i = 0; i < fat->root_dir_sectors_num; i++) {
		
			sector_loc = fat->root_dir_sec_loc + i;
			
			/* Search in a sector for fn */
			rv = browse_sector(fat, sector_loc, 0, fn);
			
			if(rv != NULL) /* If we found it return it, otherwise go to the next sector */
			{
				return rv;
			}
		}
	}
	else /* Go through clusters/sectors */
	{
		while((fat->fat_type == FAT16 && clust < 0xFFF8)
		   || (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
		{
			/* Go through all the sectors in this cluster */
			for(i = 0; i < fat->boot_sector.sectors_per_cluster; i++)
			{
				sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster) + i;
				
				/* Search in a sector for fn */
				rv = browse_sector(fat, sector_loc, 0, fn);
			
				if(rv != NULL) /* If we found it return it, otherwise go to the next sector */
				{
					return rv;
				}
			}
			
			/* Advance to next cluster */
			clust = read_fat_table_value(fat, clust*fat->byte_offset);
		}
	}
	
	return NULL;
}

/* If fn is NULL, then just grab the first entry encountered */
node_entry_t *browse_sector(fatfs_t *fat, unsigned int sector_loc, unsigned int ptr, const char *fn)
{
	int j, k;
	int var = ptr;
	static int contin = 0;
	unsigned char *str_temp;
	static int last_sec = 0;
	static unsigned char buf[512];
	
	fat_lfn_entry_t lfn;
	fat_dir_entry_t temp;
	
	static unsigned char lfnbuf1[256]; /* Longest a filename can be is 255 chars */
	static unsigned char lfnbuf2[256]; /* Longest a filename can be is 255 chars */
	
	node_entry_t    *new_entry;
	
	/* Only wipe if there wasnt a long file name entry in the last sector that wasnt attached to a shortname entry */
	if(contin == 0)
	{
		memset(lfnbuf1, 0, sizeof(unsigned char)*256);
		memset(lfnbuf2, 0, sizeof(unsigned char)*256);
	}

	if(sector_loc != last_sec) /* Only read a new sector when we have to */
	{
		/* Read 1 sector */
		fat->dev->read_blocks(fat->dev, sector_loc, 1, buf);
		
		last_sec = sector_loc;
	}
	
	for(j = var/32; j < 16; j++) /* How many entries per sector(32 byte entry x 16 = 512 bytes = 1 sector) */
	{	
		/* Entry does not exist if it has been deleted(0xE5) or is an empty entry(0) */
		if((buf+var)[0] == DELETED || (buf+var)[0] == EMPTY) 
		{ 
			var += ENTRYSIZE; /* Increment to next entry */
			continue; 
		}
		
		/* Check if it is a long filename entry */
		if((buf+var)[ATTRIBUTE] == LONGFILENAME) { 
			memset(&lfn, 0, sizeof(fat_lfn_entry_t));
			memcpy(&lfn, buf+var, sizeof(fat_lfn_entry_t));
		
			if(lfnbuf1[0] == '\0') 
			{
				str_temp = extract_long_name(&lfn);
				strcpy(lfnbuf1, str_temp);
				free(str_temp);
				
				if(lfnbuf2[0] != '\0') {
					strcat(lfnbuf1, lfnbuf2);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
				}
			}
			else if(lfnbuf2[0] == '\0')
			{
				str_temp = extract_long_name(&lfn);
				strcpy(lfnbuf2, str_temp);
				free(str_temp);
				
				if(lfnbuf1[0] != '\0') {
					strcat(lfnbuf2, lfnbuf1);
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
				}
			}
			contin = 1;
			var += ENTRYSIZE; 
			continue; /* Continue on to the next entry */
		}
		
		/* Its a file/folder entry */
		else {
			memset(&temp, 0, sizeof(fat_dir_entry_t));	
			
			memcpy(temp.FileName, (buf+var+FILENAME), 8); 
			memcpy(temp.Ext, (buf+var+EXTENSION), 3);
			memcpy(&(temp.Attr), (buf+var+ATTRIBUTE), 1);
			memcpy(&(temp.Res), (buf+var+RESERVED), 1);
			memcpy(&(temp.FstClusHI), (buf+var+STARTCLUSTERHI), 2);
			memcpy(&(temp.FstClusLO), (buf+var+STARTCLUSTERLOW), 2);
			memcpy(&(temp.FileSize), (buf+var+FILESIZE), 4);
			
			temp.FileName[8] = '\0';
			temp.Ext[3] = '\0';
			
			contin = 0;
			
			/* Dont care about these hidden entries */
			if(strcmp(temp.FileName, ".       ") == 0 || strcmp(temp.FileName, "..      ") == 0)
			{
				var += ENTRYSIZE; 
				continue; /* Continue on to the next entry */
			}
			
			/* Deal with no long name */
			if(lfnbuf1[0] == '\0' && lfnbuf2[0] == '\0')
			{
				strcat(lfnbuf1, temp.FileName);
				strcat(lfnbuf2, temp.FileName);
				
				if(temp.Res & 0x08) /* If the filename is supposed to appear lowercase...make it so */
				{	
					k = 0;
					
					while(lfnbuf2[k])
					{
						lfnbuf2[k] = tolower((int)lfnbuf2[k]);
						k++;
					}
				}
				
				if(temp.Ext[0] != ' ') {             /* If we actually have an extension....add it in */
					if(temp.Attr == VOLUME_ID) { /* Extension is part of the VOLUME name(node->FileName) */
						strcat(lfnbuf1, temp.Ext);
					}
					else {
						strcat(lfnbuf1, ".");
						strcat(lfnbuf1, temp.Ext);
						
						strcat(lfnbuf2, ".");
						strcat(lfnbuf2, temp.Ext);
						
						if(temp.Res & 0x10) /* If the extension is supposed to appear lowercase...make it so */
						{	
							k = strlen(temp.FileName) + 1; /* + 1 for period */
							
							while(lfnbuf2[k])
							{
								lfnbuf2[k] = tolower((int)lfnbuf2[k]);
								k++;
							}
						}
					}
				}
				
				str_temp = remove_all_chars(lfnbuf1,' '); 
				strcpy(lfnbuf1, str_temp);
				free(str_temp);
				
				str_temp = remove_all_chars(lfnbuf2,' '); 
				strcpy(lfnbuf2, str_temp);
				free(str_temp);
				
				if(fn == NULL || strcasecmp(lfnbuf1, fn) == 0) /* We found the file we are looking for */
				{
					new_entry = malloc(sizeof(node_entry_t));
					new_entry->Name = malloc(strlen(lfnbuf2));
					new_entry->ShortName = malloc(strlen(lfnbuf1));
					strcpy(new_entry->Name, lfnbuf2);
					strcpy(new_entry->ShortName, lfnbuf1);  
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
				}
				else
				{
					var += ENTRYSIZE; 
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
					continue; /* Continue on to the next entry */
				}
			}
			else if(lfnbuf1[0] != '\0')
			{
				strcat(lfnbuf2, temp.FileName);
				if(temp.Ext[0] != ' ') {             /* If we actually have an extension....add it in */
					if(temp.Attr == VOLUME_ID) { /* Extension is part of the VOLUME name(node->FileName) */
						strcat(lfnbuf2, temp.Ext);
					}
					else {
						strcat(lfnbuf2, ".");
						strcat(lfnbuf2, temp.Ext);
					}
				}
				
				if(fn == NULL || strcasecmp(lfnbuf1, fn) == 0 || strcasecmp(lfnbuf2, fn) == 0) 
				{
					new_entry = malloc(sizeof(node_entry_t));
					new_entry->Name = malloc(strlen(lfnbuf1));
					new_entry->ShortName = malloc(strlen(lfnbuf2));
					strcpy(new_entry->Name, lfnbuf1);
					strcpy(new_entry->ShortName, lfnbuf2);  
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
				}
				else
				{
					var += ENTRYSIZE; 
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
					continue; /* Continue on to the next entry */
				}
			}
			else {
				strcat(lfnbuf1, temp.FileName);
				if(temp.Ext[0] != ' ') {             /* If we actually have an extension....add it in */
					if(temp.Attr == VOLUME_ID) { /* Extension is part of the VOLUME name(node->FileName) */
						strcat(lfnbuf1, temp.Ext);
					}
					else {
						strcat(lfnbuf1, ".");
						strcat(lfnbuf1, temp.Ext);
					}
				}
				
				if(fn == NULL || strcasecmp(lfnbuf1, fn) == 0 || strcasecmp(lfnbuf2, fn) == 0) 
				{
					new_entry = malloc(sizeof(node_entry_t));
					new_entry->Name = malloc(strlen(lfnbuf2)+1);
					new_entry->ShortName = malloc(strlen(lfnbuf1)+1);
					strcpy(new_entry->Name, lfnbuf2);
					strcpy(new_entry->ShortName, lfnbuf1);  
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
				}
				else
				{
					var += ENTRYSIZE; 
					memset(lfnbuf1, 0, sizeof(unsigned char)*256);
					memset(lfnbuf2, 0, sizeof(unsigned char)*256);
					continue; /* Continue on to the next entry */
				}
			}		
			
			new_entry->Attr = temp.Attr;
			new_entry->FileSize = temp.FileSize;
			
			if(fn != NULL)  /* Only grab these when we will actually use them */
			{
				new_entry->StartCluster = ((temp.FstClusHI << 16) | temp.FstClusLO);
				new_entry->EndCluster = end_cluster(fat, new_entry->StartCluster);
			}
			
			new_entry->Location[0] = sector_loc; 
			new_entry->Location[1] = var; /* Byte in sector */
			
#ifdef FATFS_DEBUG
			printf("FileName: %s ShortName: %s Attr: %x Cluster: %d  \n", new_entry->Name, new_entry->ShortName, new_entry->Attr, (temp.FstClusHI << 16) | temp.FstClusLO);
#endif	
			return new_entry;
		}
	}
	
	return NULL; /* Didnt find file/folder named 'fn' it in this sector */
}


node_entry_t *get_next_entry(fatfs_t *fat, node_entry_t *dir, node_entry_t *last_entry)
{
	int i;
	unsigned int ptr;
	unsigned int sector_loc;
	unsigned int clust_sector_loc;
	unsigned int clust = dir->StartCluster;
	
	node_entry_t    *rv = NULL;
	
	/* Start from the beginning */
	if(last_entry == NULL)
	{
		ptr = 0;
		
		if(clust != 0)
		{
			sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster);
		}
		else if(fat->fat_type == FAT16 && strcasecmp(dir->Name, fat->mount) == 0) /* Fat16 Root Directory */
		{
			sector_loc = fat->root_dir_sec_loc;
		}
		else
		{
			printf("ERROR: The directory \"%s\" has no cluster affiliated with it and is not the root directory of an FAT16 formatted card\n", dir->Name);
			return NULL;
		}
	}
	/* Start from the last entry */
	else 
	{
		ptr = last_entry->Location[1];
		sector_loc = last_entry->Location[0];
		
		/* Free up the last directory since we dont need it anymore */
		delete_struct_entry(last_entry);
		
		if(clust != 0)
		{	
			/* Determine cluster(based on sector) */
			while((fat->fat_type == FAT16 && clust < 0xFFF8)
			   || (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
			{
				clust_sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster);
				
				/* If the sector is located in the range of this cluster, break out and use this cluster */
				if(sector_loc >= clust_sector_loc && sector_loc < (fat->boot_sector.sectors_per_cluster + clust_sector_loc))
				{
					break;
				}
				
				clust = read_fat_table_value(fat, clust*fat->byte_offset);
			}
		}
		
		ptr += 32; /* Go to the next entry */
		
		if(ptr >= 512)
		{
			ptr = 0;
			sector_loc += 1;
		}
		
		if((fat->fat_type == FAT16 && clust < 0xFFF8)
	    || (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
		{
			clust_sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster);
				
			if(!(sector_loc >= clust_sector_loc && sector_loc < (fat->boot_sector.sectors_per_cluster + clust_sector_loc)))
			{
				clust = read_fat_table_value(fat, clust*fat->byte_offset);
				
				if((fat->fat_type == FAT16 && clust < 0xFFF8)
				|| (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
				{
					sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster);
				}
				else
				{
					printf("When you changed the sector there wasnt a cluster that belongs to you to goto\n");
					return NULL;
				}
			}
		}
		else if(fat->fat_type == FAT16 && strcasecmp(dir->Name, fat->mount) == 0) /* Fat16 Root Directory */
		{
			if(sector_loc >= (fat->root_dir_sec_loc + fat->root_dir_sectors_num))
				return NULL;
		}
		else
		{
			printf("ERROR: You're trying to advance to a different sector but dont know where to go\n");
			return NULL;
		}
	}
	
	/* If the directory is the root directory and if fat->fat_type = FAT16 consider it a special case */
	if(fat->fat_type == FAT16 && strcasecmp(dir->Name, fat->mount) == 0) /* Go through special(static number) sectors. No clusters. */
	{
		for(i = (sector_loc - fat->root_dir_sec_loc); i < fat->root_dir_sectors_num; i++) 
		{
			sector_loc = fat->root_dir_sec_loc + i;
			
			rv = browse_sector(fat, sector_loc, ptr, NULL);
			
			if(rv != NULL) /* If we found one return it, otherwise go to the next sector */
			{
				return rv;
			}
			
			ptr = 0;
		}
	}
	else /* Go through clusters/sectors */
	{
		while((fat->fat_type == FAT16 && clust < 0xFFF8)
		   || (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
		{
			clust_sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster);
			
			/* Go through all the sectors in this cluster */
			for(i = (sector_loc - clust_sector_loc); i < fat->boot_sector.sectors_per_cluster; i++)
			{
				sector_loc = clust_sector_loc + i;
			
				rv = browse_sector(fat, sector_loc, ptr, NULL);
				
				if(rv != NULL) /* If we found one return it, otherwise go to the next sector */
				{
					return rv;
				}
				
				ptr = 0;
			}
			
			/* Advance to next cluster */
			clust = read_fat_table_value(fat, clust*fat->byte_offset);
			
			ptr = 0;
			
			if((fat->fat_type == FAT16 && clust < 0xFFF8)
		    || (fat->fat_type == FAT32 && clust < 0xFFFFFF8))
			{
				sector_loc = fat->data_sec_loc + ((clust - 2) * fat->boot_sector.sectors_per_cluster);
			}
		}
	}
	
	return NULL;
}

node_entry_t *build_from_loc(fatfs_t *fat, struct QNode *node)
{
	int var = node->Location[1];
	int sector_loc = node->Location[0];
	node_entry_t *rv = NULL;
	fat_dir_entry_t temp;
	unsigned char *buf = malloc(512 * sizeof(unsigned char));
	unsigned char shortname[14];
	
	/* Read 1 sector */
	fat->dev->read_blocks(fat->dev, sector_loc, 1, buf);
	
	memset(&temp, 0, sizeof(fat_dir_entry_t));
	memcpy(temp.FileName, (buf+var+FILENAME), 8); 
	memcpy(temp.Ext, (buf+var+EXTENSION), 3);
	memcpy(&(temp.Attr), (buf+var+ATTRIBUTE), 1);
	memcpy(&(temp.Res), (buf+var+RESERVED), 1);
	memcpy(&(temp.FstClusHI), (buf+var+STARTCLUSTERHI), 2);
	memcpy(&(temp.FstClusLO), (buf+var+STARTCLUSTERLOW), 2);
	memcpy(&(temp.FileSize), (buf+var+FILESIZE), 4);
	
	memset(shortname, 0, sizeof(unsigned char) *14);
	
	strcat(shortname, temp.FileName);
	
	if(temp.Ext[0] != ' ') {             /* If we actually have an extension....add it in */
		if(temp.Attr == VOLUME_ID) { /* Extension is part of the VOLUME name(node->FileName) */
			strcat(shortname, temp.Ext);
		}
		else {
			strcat(shortname, ".");
			strcat(shortname, temp.Ext);
		}
	}
	
	rv = malloc(sizeof(node_entry_t));
	
	rv->ShortName = malloc(strlen(shortname)+1);
	strcpy(rv->ShortName, shortname);
	rv->ShortName[strlen(shortname)] = '\0';
	rv->Attr = temp.Attr;
	rv->FileSize = temp.FileSize;
	rv->Location[0] = sector_loc;
	rv->Location[1] = var;
	rv->StartCluster = ((temp.FstClusHI << 16) | temp.FstClusLO);
	rv->EndCluster = end_cluster(fat, rv->StartCluster);
	
	rv->CurrCluster = 0;
	rv->NumCluster = 0;

	free(buf);
	
	return rv;
}


