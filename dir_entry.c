
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "fatfs.h"

#include "dir_entry.h"

unsigned char * extract_long_name(fat_lfn_entry_t *lfn) 
{
	int i;

	unsigned char temp[2];
	unsigned char *buf = malloc(sizeof(unsigned char)*13);
	unsigned char *final = NULL;
	
	memset(temp, 0, sizeof(unsigned char)*2);
	memset(buf, 0, sizeof(unsigned char)*13);
	
	/* Get first five ascii */
	for(i = 0; i < 5; i++) 
	{
		temp[0] = (unsigned char)lfn->FNPart1[i];
		strcat(buf, temp);
	}
	
	/* Next six ascii */
	for(i = 0; i < 6; i++) 
	{
		temp[0] = (unsigned char)lfn->FNPart2[i];
		strcat(buf, temp);
	}
	
	/* Last two ascii */
	for(i = 0; i < 2; i++) 
	{
		temp[0] = (unsigned char)lfn->FNPart3[i];
		strcat(buf, temp);
	}
	
	/* Remove filler chars at end of the long file name */
	final = remove_all_chars(buf, 0xFF);
	
	free(buf);
	
	return final;
}


cluster_node_t * build_cluster_linklist(fatfs_t *fat, int start_cluster)
{
    unsigned int cluster_num;
	unsigned int end_of_list = (fat->fat_type == FAT16) ? 0xFFF8 : 0xFFFFFF8; 

    cluster_node_t *start; 
	cluster_node_t *temp; 
	
	if(start_cluster == 0)  /* Cluster 0 is reserved. It means file is empty so no need to create LL for a file that is empty */
	{
		return NULL;
	}
	
	start = malloc(sizeof(cluster_node_t));
	temp = start;
	
	start->Cluster_Num = start_cluster;
	start->Next = NULL;
	cluster_num = read_fat_table_value(fat, start_cluster*fat->byte_offset);
	
	if(fat->fat_type == FAT32)
		cluster_num = cluster_num & 0x0FFFFFFF; /* Ignore the high 4 bits */
	
	while(cluster_num < end_of_list) {
		temp->Next = malloc(sizeof(cluster_node_t));
		temp = temp->Next;
		temp->Cluster_Num = cluster_num;
		temp->Next = NULL;
		cluster_num = read_fat_table_value(fat, cluster_num*fat->byte_offset);
		
		if(fat->fat_type == FAT32)
			cluster_num = cluster_num & 0x0FFFFFFF; /* Ignore the high 4 bits */
	}
	
	return start;
}

#if defined (FATFS_CACHEALL) 

node_entry_t * get_child_of_parent(node_entry_t *parent, unsigned char *child_name) {
    node_entry_t *child = parent->Children;
	
	while(child != NULL) 
	{	
	    if(strcasecmp(child->Name, child_name) == 0) {
		    return child;
		}
		else if(strcasecmp(child->ShortName, child_name) == 0)
		{
			return child;
		}
		child = child->Next;
	}
	
	return NULL;
}

node_entry_t *fat_search_by_path(node_entry_t *root, const char *fn)
{
    int i = 0;
    char c;
    char *pch;
    char *ufn = (char *)malloc(strlen(fn)+1);
    node_entry_t *target = root;
    node_entry_t *child;

    memset(ufn, 0, strlen(fn)+1);

    /* Make a copy */
	strcpy(ufn, fn);
	ufn[strlen(fn)] = '\0';

    pch = strtok(ufn,"/");
	
    if(strcasecmp(pch, target->Name) == 0) {
		pch = strtok (NULL, "/");

		if(pch != NULL) {
			child = target;
		}

		while(pch != NULL) {
		
			if((target = get_child_of_parent(child, pch))) 
			{
				pch = strtok (NULL, "/");
				
				if(pch != NULL) {
					child = target;
				}
			}
			else 
			{
#ifdef FATFS_DEBUG
				printf("Couldnt find %s\n",pch);
#endif
				return NULL;
			}
		}
    }
    else {
        return NULL;
    }

    free(ufn);
	
    return target;
}

void parse_directory_sector(fatfs_t *fat, node_entry_t *parent, int sector_loc) 
{
	int i, j;
	int var = 0;
	int new_sector_loc = 0;
	
	static int contin = 0; /* Used for when long file names carry over to the next sector */
	
	unsigned char *str_temp;
	unsigned char *buf = malloc(sizeof(unsigned char)*512);
	
	static unsigned char lfnbuf1[256]; /* Longest a filename can be is 255 chars */
	static unsigned char lfnbuf2[256]; /* Longest a filename can be is 255 chars */
	
	fat_lfn_entry_t lfn;
	fat_dir_entry_t temp;
	cluster_node_t  *node;
	node_entry_t    *child;
	node_entry_t    *new_entry;

	/* Only wipe if there wasnt a long file name entry in the last sector that wasnt attached to a shortname entry */
	if(contin == 0)
	{
		memset(lfnbuf1, 0, sizeof(unsigned char)*256);
		memset(lfnbuf2, 0, sizeof(unsigned char)*256);
	}
	
	/* Read 1 sector */
	fat->dev->read_blocks(fat->dev, sector_loc, 1, buf);
	
	for(i = 0; i < 16; i++) /* How many entries per sector(32 byte entry x 16 = 512 bytes) */
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
			contin = 1; /* Makes sure that if this is the last entry in the sector that the long name will be saved when reading the next sector */
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
			memcpy(&(temp.FstClusLO), (buf+var+STARTCLUSTER), 2);
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
			
			new_entry = malloc(sizeof(node_entry_t));
			
			/* Deal with no long name */
			if(lfnbuf1[0] == '\0' && lfnbuf2[0] == '\0')
			{
                strcat(lfnbuf1, temp.FileName);
				strcat(lfnbuf2, temp.FileName);
				
				if(temp.Res & 0x08) /* If the filename is supposed to appear lowercase...make it so */
				{	
					j = 0;
					
					while(lfnbuf2[j])
					{
						lfnbuf2[j] = tolower((int)lfnbuf2[j]);
						j++;
					}
				}
				
				if(temp.Ext[0] != ' ') {             /* If we actually have an extension....add it in */
					if(temp.Attr == VOLUME_ID) { /* Extension is part of the VOLUME name(node->FileName) */
						strcat(lfnbuf1, temp.Ext);
					}
					else {
						strcat(lfnbuf1, ".");
						strcat(lfnbuf2, ".");
						
						strcat(lfnbuf1, temp.Ext);
						strcat(lfnbuf2, temp.Ext);
						
						if(temp.Res & 0x10) /* If the extension is supposed to appear lowercase...make it so */
						{
							j = strlen(temp.FileName) + 1; /* + 1 because of period */
							while(lfnbuf2[j])
							{
								lfnbuf2[j] = tolower((int)lfnbuf2[j]);
								j++;
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
				
				new_entry->Name = malloc(strlen(lfnbuf2));
				new_entry->ShortName = malloc(strlen(lfnbuf1));
				strcpy(new_entry->Name, lfnbuf2);
				strcpy(new_entry->ShortName, lfnbuf1);  
				memset(lfnbuf1, 0, sizeof(unsigned char)*256);
				memset(lfnbuf2, 0, sizeof(unsigned char)*256);
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
				
				new_entry->Name = malloc(strlen(lfnbuf1));
				new_entry->ShortName = malloc(strlen(lfnbuf2));
				strcpy(new_entry->Name, lfnbuf1);
				strcpy(new_entry->ShortName, lfnbuf2);  
				memset(lfnbuf1, 0, sizeof(unsigned char)*256);
				memset(lfnbuf2, 0, sizeof(unsigned char)*256);
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
				
				new_entry->Name = malloc(strlen(lfnbuf2));
				new_entry->ShortName = malloc(strlen(lfnbuf1));
				strcpy(new_entry->Name, lfnbuf2);
				strcpy(new_entry->ShortName, lfnbuf1);  
				memset(lfnbuf1, 0, sizeof(unsigned char)*256);
				memset(lfnbuf2, 0, sizeof(unsigned char)*256);
			}		
			
			new_entry->Attr = temp.Attr;
			new_entry->FileSize = temp.FileSize;
			new_entry->Data_Clusters = build_cluster_linklist(fat, temp.FstClusLO);
			new_entry->Location[0] = sector_loc; 
			new_entry->Location[1] = var; /* Byte in sector */
			new_entry->Parent = parent;
			new_entry->Children = NULL;
			new_entry->Next = NULL;
			
#ifdef FATFS_DEBUG
			printf("FileName: %s ShortName: %s Parent: %s  Attr: %x Cluster: %d  \n", new_entry->Name, new_entry->ShortName, parent->Name, new_entry->Attr, temp.FstClusLO);
#endif
			
			if(parent->Children == NULL) {
				parent->Children = new_entry;
			} else {
				child = parent->Children;
			    while (child->Next != NULL) {
				    child = child->Next;
				}
				child->Next = new_entry;
			}
			
			if(new_entry->Attr == DIRECTORY && new_entry->Name[0] != 0x2E && !(new_entry->Name[0] == 0x2E && new_entry->Name[1] == 0x2E)) { /* Dont do this for .(current direct) and ..(parent) */
			 
				node = new_entry->Data_Clusters;
				
				do {	
				    new_sector_loc = fat->data_sec_loc + (node->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster; /* Calculate sector loc from cluster given */
					for(j = 0;j < fat->boot_sector.sectors_per_cluster; j++) {
						parse_directory_sector(fat, new_entry, new_sector_loc + j);
					}
					node = node->Next;
				} while(node != NULL);
			}

			var += ENTRYSIZE;
		}
	}
	
	free(buf);
}

#endif

unsigned char *fat_read_data(fatfs_t *fat, node_entry_t *file, int count, int pointer)
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
	cluster_node_t  *node = NULL;
	unsigned char *buf = malloc(count*sizeof(unsigned char)+1); 
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */

	/* Clear */
	memset(buf, 0, count*sizeof(unsigned char));

	/* While we still have more to read, do it */
	while(cnt)
	{
		/* Get the number of the sector in the cluster we want to read */
		numOfSector = ptr / bytes_per_sector;

		/* Figure out which cluster we are reading from */
		clusterNodeNum = numOfSector / fat->boot_sector.sectors_per_cluster;

		/* Set to first cluster */
		node = file->Data_Clusters; 

		/* Advance to the cluster we want to read from. */
		for(i = 0; i < clusterNodeNum; i++) 
		{
			node = node->Next;
		}

		/* Calculate Sector Location from cluster and sector we want to read */
		sector_loc = fat->data_sec_loc + ((node->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster) + numOfSector; 

		/* Clear out before reading into */
		memset(sector, 0, 512*sizeof(unsigned char));

		/* Read 1 sector */
		if(fat->dev->read_blocks(fat->dev, sector_loc, 1, sector) != 0)
		{
#ifdef FATFS_DEBUG
			printf("fat_read_data(dir_entry.c): Couldn't read the sector %d\n", sector_loc);
#endif
			return NULL;
		}

		/* Calculate current byte position in the sector we want to start reading from */
		curSectorPos = ptr - (numOfSector * bytes_per_sector);

		/* Calculate the number of chars to read */
		numToRead = ((bytes_per_sector - curSectorPos) > cnt) ? cnt : (bytes_per_sector - curSectorPos);

		/* Concat */
		strncat(buf, sector + curSectorPos, numToRead);

		/* Advance variables */
		ptr += numToRead;  /* Advance file pointer */
		cnt -= numToRead;  /* Decrease bytes left to read */
	}
	
	free(sector);

	return buf;
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
	const int bytes_per_sector = fat->boot_sector.bytes_per_sector;
	cluster_node_t  *node = NULL; 
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */

	/* Clear */
	memset(sector, 0, 512*sizeof(unsigned char));

	/* While we still have more to write, do it */
	while(cnt)
	{
		/* Get the number of the sector in the cluster we want to write to */
		numOfSector = ptr / bytes_per_sector;

		/* Figure out which cluster we are writing to */
		clusterNodeNum = numOfSector / fat->boot_sector.sectors_per_cluster;

		/* Set to first cluster */
		node = file->Data_Clusters; 
		
		if(node == NULL)
		{
			file->Data_Clusters = allocate_cluster(fat, NULL);
			node = file->Data_Clusters; 
		}
		else
		{
			/* Advance to the cluster we want to write to */
			for(i = 0; i < clusterNodeNum; i++) 
			{
				/* Check if node->Next equals NULL. If it does then allocate another cluster for this file and break */
				if(node->Next == NULL)
				{
					if((node->Next = allocate_cluster(fat, file->Data_Clusters)) == NULL)
					{
#ifdef FATFS_DEBUG
						printf("fat_write_data(dir_entry.c): All out of clusters to Allocate\n");
#endif
						return -1;
					}
					
					node = node->Next;
					break;
				}
				
				node = node->Next;
			}
		}

		/* Calculate Sector Location from cluster and sector we want to read and then write to */
		sector_loc = fat->data_sec_loc + ((node->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster) + numOfSector; 

		/* Clear out before reading into */
		memset(sector, 0, 512*sizeof(unsigned char));
		
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
		memcpy(sector + curSectorPos, bbuf, numToWrite);

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
		cnt -= numToWrite;  /* Decrease bytes left to write */
	}
	
	free(sector);

	return count;
}

void update_fat_entry(fatfs_t *fat, node_entry_t *file)
{	
	const short none = 0;
	unsigned char *sector = malloc(512*sizeof(unsigned char)); /* Each sector is 512 bytes long */
	
	time_t rawtime;
	short tme = 0;
	short date = 0;
	struct tm * timeinfo;

	time (&rawtime);
	timeinfo = localtime (&rawtime);
	
	tme = generate_time(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec/2);
	date = generate_date(1900 + timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
	
	memset(sector, 0, 512*sizeof(unsigned char));
	
	/* Read sector */
	if(fat->dev->read_blocks(fat->dev, file->Location[0], 1, sector) != 0)
	{
#ifdef FATFS_DEBUG
		printf("update_fat_entry(dir_entry.c): Couldn't read the sector %d\n", file->Location[0]);
#endif
		return;
	}

	/* Edit Entry */
	memcpy(sector + file->Location[1] + FILESIZE, &(file->FileSize), 4); 
	if(file->Data_Clusters != NULL)
		memcpy(sector + file->Location[1] + STARTCLUSTER, &(file->Data_Clusters->Cluster_Num), 2);
	else
		memcpy(sector + file->Location[1] + STARTCLUSTER, &(none), 2); /* Set to zero(empty file). Folders are never empty */
	memcpy(sector + file->Location[1] + LASTACCESSDATE, &(date), 2);
	memcpy(sector + file->Location[1] + LASTWRITETIME, &(tme), 2);
	memcpy(sector + file->Location[1] + LASTWRITEDATE, &(date), 2);

	/* Write it back */
	if(fat->dev->write_blocks(fat->dev, file->Location[0], 1, sector) != 0)
	{
#ifdef FATFS_DEBUG
		printf("update_fat_entry(dir_entry.c): Couldn't write the sector %d\n", file->Location[0]);
#endif
		return;
	}
	
	free(sector);
	
#if defined (FATFS_CACHEALL) 
	if(file->Parent->Parent != NULL) /* If this file/folder is in a */
	{                                /* subdirectory[Not the ROOT Directory] update it as well (recursively) */
		update_fat_entry(fat, file->Parent);
	}
#endif
}

void delete_tree_entry(node_entry_t * node)
{
	node_entry_t *child;
    cluster_node_t  *old;
    
#if defined(FATFS_CACHEALL)
	if(node->Parent != NULL)
	{
		if(node->Parent->Children == node) /* If we are the first child in the list */
		{
			node->Parent->Children = node->Next;
		}
		else /* Otherwise... */
		{
			child = node->Parent->Children;
			
			while(child->Next != node)
			{
				child = child->Next;
			}
			
			child->Next = node->Next;
		}
	}
#endif
	
    free(node->Name);      /* Free the name */
	free(node->ShortName); /* Free the shortname */
    
    /* Free the LL Data Clusters */
    while(node->Data_Clusters != NULL)
    {
        old = node->Data_Clusters;
        node->Data_Clusters = node->Data_Clusters->Next;
        free(old);
    }
	
	free(node);
    
    node = NULL;
}

#if defined (FATFS_CACHEALL) 

void delete_directory_tree(node_entry_t * node)
{    
    if(node == NULL)
        return;
    
    delete_directory_tree(node->Next);
    delete_directory_tree(node->Children);
    
    if(node->Next == NULL && node->Children == NULL)
    {
#ifdef FATFS_DEBUG
		printf("DELETING Name: %s ShortName: %s ", node->Name, node->ShortName);
		
		if(node->Parent != NULL)
			printf("Parent: %s \n", node->Parent->Name);
		else
			printf("\n");
#endif
		
        delete_tree_entry(node);
    }
}

#endif

cluster_node_t *allocate_cluster(fatfs_t *fat, cluster_node_t  *cluster)
{
    unsigned int fat_index = 2;
	unsigned int marker = (fat->fat_type == FAT16) ? 0xFFFF : 0x0FFFFFFF;
    unsigned int cluster_num = 0;
    cluster_node_t *clust = cluster;
    cluster_node_t *new_clust = NULL;
    
    /* Handle NULL cluster(Happens when a new file is created) */
    if(cluster != NULL) {  
        while(clust->Next)
        {
            clust = clust->Next;
        }
    }
    
    /* We are now at the end of the LL. Search for a free cluster. */
    while(fat_index < fat->boot_sector.bytes_per_sector*fat->table_size) { /* Go through Table one index at a time */
	
		cluster_num = read_fat_table_value(fat, fat_index*fat->byte_offset);
        
        /* If we found a free entry */
        if(cluster_num == 0x00) 
        {
#ifdef FATFS_DEBUG
            printf("allocate_cluster(dir_entry.c): Found a free cluster entry -- Index: %d\n", fat_index); 
#endif
            cluster_num = marker;
            if(cluster != NULL) /* Cant change what doesnt exist */
            {
				write_fat_table_value(fat, clust->Cluster_Num*fat->byte_offset, fat_index);  /* Change the table to indicate an allocated cluster */
			}
			write_fat_table_value(fat, fat_index*fat->byte_offset, cluster_num);             /* Put the marker(0xFFFF or 0x0FFFFFFF) at the allocated cluster index */
            
            new_clust = (cluster_node_t *)malloc(sizeof(cluster_node_t));
            new_clust->Cluster_Num = fat_index;
            new_clust->Next = NULL;
            
            return new_clust;
        }
        else
        {
            fat_index++;
        }
    }
#ifdef FATFS_DEBUG
	printf("allocate_cluster(dir_entry.c): Didnt find a free Cluster\n");
#endif
    
    /* Didn't find a free cluster */
    return NULL;
}

void delete_cluster_list(fatfs_t *fat, node_entry_t *f)
{
	const short int clear = 0;
	cluster_node_t *clust = f->Data_Clusters;
	cluster_node_t *temp = clust;
	
	while(clust != NULL)
	{
		write_fat_table_value(fat, clust->Cluster_Num*fat->byte_offset, clear);
#ifdef FATFS_DEBUG
		printf("delete_cluster_list(dir_entry.c) Freed Cluster: %d\n", clust->Cluster_Num);
#endif
		clust = clust->Next;
		free(temp);
		temp = clust;
	}
	
	f->Data_Clusters = NULL;
}

#if defined (FATFS_CACHEALL) 

node_entry_t *create_entry(fatfs_t *fat, node_entry_t * root, char *fn, unsigned char attr)
{
    int i = 0;
    char c;
    char *pch;
    char *filename;
    char *entry_filename;
    char *ufn = (char *)malloc(strlen(fn)+1);
    node_entry_t *target = root;
    node_entry_t *child;
    node_entry_t *temp;
    node_entry_t *newfile;
	
	int loc[2];
	fat_dir_entry_t entry;

    memset(ufn, 0, strlen(fn)+1);

    /* Make sure the files/folders are capitalized */
    strcpy(ufn, fn);
	ufn[strlen(fn)] = '\0';

    pch = strtok(ufn,"/");  /* Grab Root */
	
    if(strcasecmp(pch, target->Name) == 0) {
        pch = strtok (NULL, "/"); 

        if(pch != NULL) {
            child = target;
        }

        while(pch != NULL) {
            if((target = get_child_of_parent(child, pch))) 
            {
				if(target->Attr & READ_ONLY) /* Check and make sure directory is not read only. */
				{
					free(ufn);
					errno = EROFS;
					return NULL;
				}
				
                pch = strtok (NULL, "/");
              
                if(pch != NULL) {
                    child = target;
                }
            }
            else 
            {
                filename = pch; /* We possibly have the filename */
                
                /* Return NULL if we encounter a directory that doesn't exist */
                if((pch = strtok (NULL, "/"))) { /* See if there is more to parse through */
					free(ufn);
                    errno = ENOTDIR;
                    return NULL;               /* if there is that means we are looking in a directory */
                }                              /* that doesn't exist. */
                
                /* Make sure the filename is valid */
                if(correct_filename(filename) == -1) {
					free(ufn);
                    return NULL;
                }
                
                /* Directory trees file/folder names are always capitalized 
                   Make sure we get the original case of the file/folder name to save to SD */
                entry_filename = malloc(strlen(filename) + 1);
                memset(entry_filename, 0, strlen(filename) + 1);
                strncpy(entry_filename, fn + (strlen(fn) - strlen(filename)), strlen(filename));
                entry_filename[strlen(filename)] = '\0';
           
                /* Create file/folder */
                newfile = (node_entry_t *) malloc(sizeof(node_entry_t));
				
                newfile->Name = malloc(strlen(filename)+1); 
				strncpy(newfile->Name, filename, strlen(filename));
				newfile->Name[strlen(filename)] = '\0';
				
                newfile->Attr = attr;
                newfile->FileSize = 0;
                newfile->Data_Clusters = NULL; 
                newfile->Parent = child;
                newfile->Children = NULL;  
                newfile->Next = NULL;      /* Adding to end of Parent(var child) list of children */
				
				free(ufn);
                
                if(generate_and_write_entry(fat, entry_filename, newfile) == -1)
                {
#ifdef FATFS_DEBUG
					printf("create_entry(dir_entry.c) : Didnt Create file/folder\n");
#endif
                    delete_tree_entry(newfile);  /* This doesn't take care of removing clusters from SD card */
                    errno = EDQUOT;
                    return NULL;  
                }
                
                /* Created file on SD successfully. Now add this new file to the directory tree */
                temp = child->Children;
                
                /* Folder has no children. Set Children to be this new file */
                if(temp == NULL)
                {
                    child->Children = newfile;
                }
                else /* Otherwise go to the end of the LL of Children */
                {
                    while(temp->Next != NULL)
                    {
                        temp = temp->Next;
                    }
                    
                    temp->Next = newfile;
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
					entry.FstClusLO = newfile->Data_Clusters->Cluster_Num;
					
					loc[0] = fat->data_sec_loc + (entry.FstClusLO - 2) * fat->boot_sector.sectors_per_cluster;
					loc[1] = 0;
					write_entry(fat, &entry, DIRECTORY, loc);

					/* Add '..' folder entry */
					strncpy(entry.FileName, "..      ", 8);
					entry.FileName[8] = '\0';
					strncpy(entry.Ext, "   ", 3 ); 
					entry.Ext[3] = '\0';
					entry.Attr = DIRECTORY;
					entry.FileSize = 0;   
					if(newfile->Parent->Parent == NULL) /* If parent of this folder is the root directory, set first cluster number to 0 */
						entry.FstClusLO = 0;
					else 
						entry.FstClusLO = newfile->Parent->Data_Clusters->Cluster_Num; 
						
					loc[1] = 32; /* loc[0] Doesnt change */
					write_entry(fat, &entry, DIRECTORY, loc);
				}
                
                return newfile;
            }
        }
    }
    
	free(ufn);
	
    /* Path was invalid from the beginning */
    errno = ENOTDIR;
    return NULL;
}

#endif

int generate_and_write_entry(fatfs_t *fat, char *entry_name, node_entry_t *newfile)
{
	int i;
	int *loc = NULL;
	int last;
	unsigned char order = 1;
	unsigned int offset = 0;
	int longfilename = 0;
	char *shortname;
	unsigned char checksum;
	
	unsigned char res = 0x00;

    fat_dir_entry_t entry;
	fat_lfn_entry_t *lfn_entry_list[20];  /* Can have a maximum of 20 long file name entries */
	
	for(i = 0; i < 20; i++)
		lfn_entry_list[i] = NULL;
    
    shortname = generate_short_filename(fat, newfile->Parent, entry_name, newfile->Attr, &longfilename, &res);
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
		loc = get_free_locations(fat, newfile->Parent, (int)order);
		
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
		newfile->Data_Clusters = allocate_cluster(fat, NULL);
		clear_cluster(fat, newfile->Data_Clusters->Cluster_Num);
	}
	
	/* Make regular entry and write it to SD FAT */
	strncpy(entry.FileName, shortname, 8);
	entry.FileName[8] = '\0';
	strncpy(entry.Ext, shortname+9, 3 ); /* Skip filename and '.' */
	entry.Ext[3] = '\0';
	entry.Attr = newfile->Attr;
	entry.Res = res;
	entry.FileSize = 0;   /* For both new files and new folders */
	if(newfile->Attr & DIRECTORY)
		entry.FstClusLO = newfile->Data_Clusters->Cluster_Num; /* Folder */
	else
		entry.FstClusLO = 0;  /* File */
	
	/* Write it (after long file name entries) */
	if(longfilename)
	{
		write_entry(fat, &entry, newfile->Attr, loc);
	} else
	{
		loc = get_free_locations(fat, newfile->Parent, 1);
		write_entry(fat, &entry, newfile->Attr, loc);
	}
	
	/* Save the locations */
	newfile->Location[0] = loc[0];  
	newfile->Location[1] = loc[1]; 
	
	free(shortname);
	free(loc);
    
    return 0;
}

void delete_entry(fatfs_t *fat, node_entry_t *file)
{
	unsigned int sector_loc = file->Location[0];
	int ptr = file->Location[1];
	unsigned char sector[512];

	/* Free LL of Data Clusters */
	delete_cluster_list(fat, file);
	
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

#if !defined (FATFS_CACHEALL)

node_entry_t *fat_search_by_path(fatfs_t *fat, const char *fn)
{
	int i = 0;
	char c;
	
	unsigned char *ufn = malloc(strlen(fn)+1);
	unsigned char *pch = NULL;
	node_entry_t *temp = NULL;
	node_entry_t *rv = NULL;
	cluster_node_t *cluster = NULL;
	
	if(fat->fat_type == FAT32)
	{
		cluster = malloc(sizeof(cluster_node_t));
		cluster = build_cluster_linklist(fat, fat->root_cluster_num);
	}
	
	/* Make sure the files/folders are captialized */
    strcpy(ufn, fn);
	ufn[strlen(fn)] = '\0';
	
	/* Build Root */
	temp = malloc(sizeof(node_entry_t));
	temp->Name = (unsigned char *)remove_all_chars(fat->mount, '/');          /*  */
	temp->ShortName = (unsigned char *)remove_all_chars(fat->mount, '/');
	temp->Attr = DIRECTORY;           /* Root directory is obviously a directory */
	temp->Data_Clusters = cluster;    /* Root directory has no data clusters associated with it(FAT16). Non-NULL with FAT32 */
	temp->Parent = NULL;              /* Should always be NULL for root*/
	temp->Children = NULL;            /* Changes when Directory Tree is built */
	temp->Next = NULL;                /* Should always be NULL. Root is the top most directory and has no equal */
	
	rv = temp;
	
	pch = strtok(ufn,"/");  /* Grab Root */
	
    if(strcasecmp(pch, temp->Name) == 0) {
        pch = strtok (NULL, "/"); 

        while(pch != NULL) {
            if((rv = search_directory(fat, temp, pch)) != NULL) 
            {
				pch = strtok (NULL, "/");
				delete_tree_entry(temp);
				temp = rv;
			}
			else 
			{
#ifdef FATFS_DEBUG
				printf("Couldnt find %s\n",pch);
#endif
				free(ufn);
				return NULL;
			}
		}
	}
	
	free(ufn);
	return rv;
}

node_entry_t *create_entry(fatfs_t *fat, const char *fn, unsigned char attr)
{
	int loc[2];

    char *pch;
    char *filename;
    char *entry_filename;
	char *ufn = malloc(strlen(fn)+1);
    
    node_entry_t *newfile;
	node_entry_t *temp = NULL;
	node_entry_t *rv = NULL;
	cluster_node_t *cluster = NULL;
	
	fat_dir_entry_t entry;
	
	if(fat->fat_type == FAT32)
	{
		cluster = malloc(sizeof(cluster_node_t));
		cluster = build_cluster_linklist(fat, fat->root_cluster_num);
	}
	
	/* Make a copy of fn */
	strncpy(ufn, fn, strlen(fn));
	ufn[strlen(fn)] = '\0';
	
	/* Build Root */
	temp = malloc(sizeof(node_entry_t));
	temp->Name = (unsigned char *)remove_all_chars(fat->mount, '/');          /*  */
	temp->ShortName = (unsigned char *)remove_all_chars(fat->mount, '/');
	temp->Attr = DIRECTORY;           /* Root directory is obviously a directory */
	temp->Data_Clusters = cluster;    /* Root directory has no data clusters associated with it(FAT16). Non-NULL with FAT32 */
	temp->Parent = NULL;              /* Should always be NULL for root*/
	temp->Children = NULL;            /* Changes when Directory Tree is built */
	temp->Next = NULL;                /* Should always be NULL. Root is the top most directory and has no equal */
	
	rv = temp;
	
	pch = strtok(ufn,"/");  /* Grab Root */
	
    if(strcasecmp(pch, temp->Name) == 0) {
        pch = strtok (NULL, "/"); 

        while(pch != NULL) {
            if((rv = search_directory(fat, temp, pch)) != NULL) 
            {
				if(rv->Attr & READ_ONLY) /* Check and make sure directory is not read only. */
				{
					free(ufn);
					errno = EROFS;
					delete_tree_entry(temp);
					delete_tree_entry(rv);
					return NULL;
				}
				
                pch = strtok (NULL, "/");
                delete_tree_entry(temp);
				temp = rv;
            }
            else 
            {	
				filename = pch; /* We possibly have the filename */
                
                /* Return NULL if we encounter a directory that doesn't exist */
                if((pch = strtok (NULL, "/"))) { /* See if there is more to parse through */
					free(ufn);
                    errno = ENOTDIR;
					delete_tree_entry(temp);
                    return NULL;               /* if there is that means we are looking in a directory */
                }                              /* that doesn't exist. */
                
                /* Make sure the filename is valid */
                if(correct_filename(filename) == -1) {
					free(ufn);
					delete_tree_entry(temp);
                    return NULL;
                }
                
                /* Directory trees file/folder names are always capitalized 
                   Make sure we get the original case of the file/folder name to save to SD */
                entry_filename = malloc(strlen(filename) + 1);
                memset(entry_filename, 0, strlen(filename) + 1);
                strncpy(entry_filename, fn + (strlen(fn) - strlen(filename)), strlen(filename));
                entry_filename[strlen(filename)] = '\0';
           
                /* Create file/folder */
                newfile = (node_entry_t *) malloc(sizeof(node_entry_t));
				
                newfile->Name = malloc(strlen(filename)+1); 
				strncpy(newfile->Name, filename, strlen(filename));
				newfile->Name[strlen(filename)] = '\0';
				
                newfile->Attr = attr;
                newfile->FileSize = 0;
                newfile->Data_Clusters = NULL; 
                newfile->Parent = temp;
                newfile->Children = NULL;  
                newfile->Next = NULL;      /* Adding to end of Parent(var child) list of children */
                
                if(generate_and_write_entry(fat, entry_filename, newfile) == -1)
                {
#ifdef FATFS_DEBUG
					printf("create_entry(dir_entry.c) : Didnt Create file/folder\n");
#endif
					free(ufn);
					errno = EDQUOT;
					delete_tree_entry(temp);
                    delete_tree_entry(newfile);  /* This doesn't take care of removing clusters from SD card */
                    
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
					entry.FstClusLO = newfile->Data_Clusters->Cluster_Num;
					
					loc[0] = fat->data_sec_loc + (entry.FstClusLO - 2) * fat->boot_sector.sectors_per_cluster;
					loc[1] = 0;
					write_entry(fat, &entry, DIRECTORY, loc);

					/* Add '..' folder entry */
					strncpy(entry.FileName, "..      ", 8);
					entry.FileName[8] = '\0';
					strncpy(entry.Ext, "   ", 3 ); 
					entry.Ext[3] = '\0';
					entry.Attr = DIRECTORY;
					entry.FileSize = 0;   
					if(strcasecmp(newfile->Parent->Name, fat->mount) == 0) /* If parent of this folder is the root directory, set first cluster number to 0 */
						entry.FstClusLO = 0;
					else 
						entry.FstClusLO = newfile->Parent->Data_Clusters->Cluster_Num; 
						
					loc[1] = 32; /* loc[0] Doesnt change */
					write_entry(fat, &entry, DIRECTORY, loc);
				}
				
				free(ufn);
				delete_tree_entry(temp);
				newfile->Parent = NULL;
				
                return newfile;
            }
		}
	}
	
	free(ufn);
	delete_tree_entry(temp);
	
	 /* Path was invalid from the beginning */
    errno = ENOTDIR;
    return NULL;
}

node_entry_t *search_directory(fatfs_t *fat, node_entry_t *node, const char *fn)
{
	int i;
	unsigned int sector_loc = 0;
	
	cluster_node_t  *clust = node->Data_Clusters;
	node_entry_t    *rv = NULL;

	/* If the directory is the root directory and if fat->fat_type = FAT16 consider it a special case */
	if(strcasecmp(node->Name, fat->mount) == 0 && fat->fat_type == FAT16) /* Go through special(static number) sectors. No clusters. */
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
		while(clust != NULL)
		{
			/* Go through all the sectors in this cluster */
			for(i = 0; i < fat->boot_sector.sectors_per_cluster; i++)
			{
				sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster) + i;
				
				/* Search in a sector for fn */
				rv = browse_sector(fat, sector_loc, 0, fn);
			
				if(rv != NULL) /* If we found it return it, otherwise go to the next sector */
				{
					return rv;
				}
			}
			
			/* Advance to next cluster */
			clust = clust->Next;
		}
	}
	
	return NULL;
}

/* If fn is NULL, then just grab the first entry encountered */
node_entry_t *browse_sector(fatfs_t *fat, unsigned int sector_loc, unsigned int ptr, const char *fn)
{
	int i, j;
	int var = ptr;
	static int contin = 0;
	unsigned char *str_temp;
	unsigned char *buf = malloc(sizeof(unsigned char)*512);
	
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

	/* Read 1 sector */
	fat->dev->read_blocks(fat->dev, sector_loc, 1, buf);
	
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
			memcpy(&(temp.FstClusLO), (buf+var+STARTCLUSTER), 2);
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
					j = 0;
					
					while(lfnbuf2[j])
					{
						lfnbuf2[j] = tolower((int)lfnbuf2[j]);
						j++;
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
							j = strlen(temp.FileName) + 1; /* + 1 for period */
							
							while(lfnbuf2[j])
							{
								lfnbuf2[j] = tolower((int)lfnbuf2[j]);
								j++;
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
			
			new_entry->Attr = temp.Attr;
			new_entry->FileSize = temp.FileSize;
			new_entry->Data_Clusters = build_cluster_linklist(fat, temp.FstClusLO);
			new_entry->Location[0] = sector_loc; 
			new_entry->Location[1] = var; /* Byte in sector */
			new_entry->Parent = NULL;
			new_entry->Children = NULL;
			new_entry->Next = NULL;
			
#ifdef FATFS_DEBUG
			printf("FileName: %s ShortName: %s Attr: %x Cluster: %d  \n", new_entry->Name, new_entry->ShortName, new_entry->Attr, temp.FstClusLO);
#endif
			free(buf);
			
			return new_entry;
		}
	}
	
	free(buf);
	return NULL; /* Didnt find file/folder named 'fn' it in this sector */
}


node_entry_t *get_next_entry(fatfs_t *fat, node_entry_t *dir, node_entry_t *last_entry)
{
	int i;
	unsigned int ptr;
	unsigned int sector_loc;
	unsigned int clust_sector_loc;
	cluster_node_t  *clust = dir->Data_Clusters;
	
	node_entry_t    *new_entry;
	node_entry_t    *rv = NULL;
	
	/* Start from the beginning */
	if(last_entry == NULL)
	{
		ptr = 0;
		
		if(clust != NULL)
		{
			sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster);
		}
		else /* Fat16 Root Directory */
		{
			sector_loc = fat->root_dir_sec_loc;
		}
	}
	/* Start from the last entry */
	else 
	{
		ptr = last_entry->Location[1];
		sector_loc = last_entry->Location[0];
		
		/* Free up the last directory since we dont need it anymore */
		delete_tree_entry(last_entry);
		
		if(clust != NULL)
		{	
			/* Determine cluster(based on sector) */
			while(clust != NULL)
			{
				clust_sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster);
				
				/* If the sector is located in the range of this cluster, break out and use this cluster */
				if(sector_loc >= clust_sector_loc && sector_loc < (fat->boot_sector.sectors_per_cluster + clust_sector_loc))
				{
					break;
				}
				
				clust = clust->Next; /* Otherwise continue to the next cluster */
			}
		}
		
		ptr += 32; /* Go to the next entry */
		
		if(ptr >= 512)
		{
			ptr = 0;
			sector_loc += 1;
		}
		
		if(clust != NULL)
		{
			clust_sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster);
				
			/* If the sector is located in the range of this cluster, break out and use this cluster */
			if(!(sector_loc >= clust_sector_loc && sector_loc < (fat->boot_sector.sectors_per_cluster + clust_sector_loc)))
			{
				clust = clust->Next;
				
				if(clust != NULL)
				{
					sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster);
				}
			}
		}
		else if(strcasecmp(dir->Name, fat->mount) == 0 && fat->fat_type == FAT16) /* Fat16 Root Directory */
		{
			if(sector_loc >= (fat->root_dir_sec_loc + fat->root_dir_sectors_num))
				return NULL;
		}
	}
	
	/* If the directory is the root directory and if fat->fat_type = FAT16 consider it a special case */
	if(strcasecmp(dir->Name, fat->mount) == 0 && clust == NULL && fat->fat_type == FAT16) /* Go through special(static number) sectors. No clusters. */
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
		while(clust != NULL)
		{
			clust_sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster);
			
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
			clust = clust->Next;
			
			ptr = 0;
			
			if(clust != NULL)
			{
				sector_loc = fat->data_sec_loc + ((clust->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster);
			}
		}
	}
	
	return NULL;
}

#endif


