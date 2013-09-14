
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "fatfs.h"
#include "fat_defs.h"
#include "dir_entry.h"

char * ExtractLongName(fat_lfn_entry_t *lfn) 
{
	int i;
	char c;
	char temp[2];
	char *buf = (char *)malloc(sizeof(char)*13);
	
	memset(temp, 0, sizeof(char)*2);
	memset(buf, 0, sizeof(char)*13);
	
	// Get first five ascii
	for(i = 0; i < 5; i++) 
	{
		temp[0] = (char)lfn->FNPart1[i];
		strcat(buf, temp);
	}
	
	// Next six ascii
	for(i = 0; i < 6; i++) 
	{
		temp[0] = (char)lfn->FNPart2[i];
		strcat(buf, temp);
	}
	
	// Last two ascii
	for(i = 0; i < 2; i++) 
	{
		temp[0] = (char)lfn->FNPart3[i];
		strcat(buf, temp);
	}
	
	// Remove filler chars at end of the long file name
	buf = remove_all_chars(buf, 0xff);
	
	i = 0;
	
	// Convert long name into uppercase
	while (buf[i])
    {
       c = buf[i];
       buf[i] = toupper((int)c);
       i++;
    }
	
	return buf;
}


cluster_node_t * GetClusterList(const unsigned char *table, int start_cluster)
{
    unsigned short cluster_num;

    cluster_node_t *start = (cluster_node_t *) malloc(sizeof(cluster_node_t));
	cluster_node_t *temp = start;
	
	start->Cluster_Num = start_cluster;
	start->Next = NULL;
	memcpy(&cluster_num, &table[start_cluster*2], 2);
	
	//printf("Starting cluster: %x\n", start_cluster);
	
	while(cluster_num < 0xFFF8 ) {
		temp->Next = (cluster_node_t *) malloc(sizeof(cluster_node_t));
		temp = temp->Next;
		temp->Cluster_Num = cluster_num;
		temp->Next = NULL;
		memcpy(&cluster_num,&table[cluster_num*2], 2);
		//printf("Next cluster: %x\n", cluster_num);
	}
	
	return start;
}

node_entry_t *isChildof(node_entry_t *parent, char *child_name) {
    node_entry_t *child = parent->Children;
	
	while(child != NULL) 
	{
	    if(strcmp((const char *)child->Name, child_name) == 0) {
		    return child;
		}
		else if(strcmp((const char *)child->ShortName, child_name) == 0)
		{
			return child;
		}
		child = child->Next;
	}
	
	return NULL;
}

node_entry_t *fat_search_by_path(node_entry_t *root, char *fn)
{
    int i = 0;
    char c;
    char *pch;
    char *ufn = (char *)malloc(strlen(fn)+1);
    node_entry_t *target = root;
    node_entry_t *child;

    memset(ufn, 0, strlen(fn)+1);

    /* Make sure the files/folders are captialized */
    while (fn[i])
    {
       c = fn[i];
       ufn[i] = toupper((int)c);
       i++;
    }

    pch = strtok(ufn,"/");
	
    if(strcmp(pch, (const char*)target->Name) == 0) {
            pch = strtok (NULL, "/");

            if(pch != NULL) {
                    child = target;//->Children;
            }

            while(pch != NULL) {
                    if(target = isChildof(child, pch)) 
                    {
                            pch = strtok (NULL, "/");
                            printf("Next: %s\n", pch);
                            if(pch != NULL) {
                                    child = target->Children;
                            }
                    }
                    else 
                    {
                            printf("Couldnt find %s\n",pch);
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

void parse_directory_sector(fatfs_t *fat, node_entry_t *parent, int sector_loc, unsigned char *fat_table) 
{
	int i, j;
	int var = 0;
	int new_sector_loc = 0;
	
	unsigned char *buf = (unsigned char *)malloc(sizeof(unsigned char)*512);
	unsigned char *lfnbuf1 = (unsigned char *)malloc(sizeof(unsigned char)*255); // Longest a filename can be is 255 chars
	unsigned char *lfnbuf2 = (unsigned char *)malloc(sizeof(unsigned char)*255); // Longest a filename can be is 255 chars
	
	fat_lfn_entry_t lfn;
	fat_dir_entry_t temp;
	cluster_node_t  *node;
	node_entry_t    *child;
	node_entry_t    *new_entry;

	memset(lfnbuf1, 0, sizeof(lfnbuf1));
	memset(lfnbuf2, 0, sizeof(lfnbuf2));
	
	/* Read 1 sector */
	fat->dev->read_blocks(fat->dev, sector_loc, 1, buf);
	
	for(i = 0; i < 16; i++) /* How many entries per sector(512 bytes) */
	{
		/* Entry does not exist if it has been deleted(0xE5) or is an empty entry(0) */
		if((buf+var)[0] == DELETED || (buf+var)[0] == EMPTY) 
		{ 
			var += ENTRYSIZE; /* Increment to next entry */
			//printf("BLANK ENRY FOUND\n");
			continue; 
		}
		
		/* Check if it is a long filename entry */
		if((buf+var)[ATTRIBUTE] == LONGFILENAME) { 
		
			//printf("LONGNAME FOUND\n");
			memset(&lfn, 0, sizeof(fat_lfn_entry_t));
			memcpy(&lfn, buf+var, sizeof(fat_lfn_entry_t));
		
			if(lfnbuf1[0] == '\0') 
			{
				strcpy((char *)lfnbuf1, ExtractLongName(&lfn));
				if(lfnbuf2[0] != '\0') {
				    strcat((char *)lfnbuf1, (const char *)lfnbuf2);
					memset(lfnbuf2, 0, sizeof(lfnbuf2));
				}
			}
			else if(lfnbuf2[0] == '\0')
			{
				strcpy((char *)lfnbuf2, ExtractLongName(&lfn));
				if(lfnbuf1[0] != '\0') {
				    strcat((char *)lfnbuf2, (const char *)lfnbuf1);
					memset(lfnbuf1, 0, sizeof(lfnbuf1));
				}
			}
			var += ENTRYSIZE; 
			continue; // Continue on to the next entry
			//printf("Got through %dth iteration Sector(%d) : LONGNAME\n", i, sector_loc);
		}
		/* Its a file/folder entry */
		else {
			new_entry = (node_entry_t *)malloc(sizeof(node_entry_t));
		
			memset(&temp, 0, sizeof(fat_dir_entry_t));	
			
			memcpy(temp.FileName, (buf+var+FILENAME), 8); 
			memcpy(temp.Ext, (buf+var+EXTENSION), 3);
			memcpy(&(temp.Attr), (buf+var+ATTRIBUTE), 1);
			memcpy(&(temp.FstClusLO), (buf+var+STARTCLUSTER), 2);
			memcpy(&(temp.FileSize), (buf+var+FILESIZE), 4);
			
			temp.FileName[8] = '\0';
			temp.Ext[3] = '\0';
			
			// Deal with no long name
			if(lfnbuf1[0] == '\0' && lfnbuf2[0] == '\0')
			{
                                //printf("NO LONGNAME\n");
                                strcat((char *)lfnbuf1, (const char *)temp.FileName);
				if(temp.Ext[0] != ' ') { // If we actually have an extension....add it in
					if(temp.Attr == VOLUME_ID) { // Extension is part of the VOLUME name(node->FileName)
						strcat((char *)lfnbuf1, (const char *)temp.Ext);
					}
					else {
						strcat((char *)lfnbuf1, ".");
						strcat((char *)lfnbuf1, (const char *)temp.Ext);
					}
				}
				lfnbuf1 = (unsigned char*)remove_all_chars((const char*)lfnbuf1,' '); 
				new_entry->Name = malloc(strlen((const char *)lfnbuf1));
				new_entry->ShortName = malloc(strlen((const char *)lfnbuf1));
				strcpy((char *)new_entry->Name, (const char *)lfnbuf1);
				strcpy((char *)new_entry->ShortName, (const char *)lfnbuf1);  // Added
				memset(lfnbuf1, 0, sizeof(lfnbuf1));
				//printf("FullName: \"%s\" Shortname: %s\n", new_entry->Name, new_entry->ShortName);
			}
			else if(lfnbuf1[0] != '\0')
			{
				//printf("LONGNAME :1\n");
                                strcat((char *)lfnbuf2, (const char *)temp.FileName);
				if(temp.Ext[0] != ' ') { // If we actually have an extension....add it in
					if(temp.Attr == VOLUME_ID) { // Extension is part of the VOLUME name(node->FileName)
						strcat((char *)lfnbuf2, (const char *)temp.Ext);
					}
					else {
						strcat((char *)lfnbuf2, ".");
						strcat((char *)lfnbuf2, (const char *)temp.Ext);
					}
				}
				
				new_entry->Name = (char *)malloc(strlen((const char *)lfnbuf1));
				new_entry->ShortName = (char *)malloc(strlen((const char *)lfnbuf2));
				strcpy((char *)new_entry->Name, (const char *)lfnbuf1);
				strcpy((char *)new_entry->ShortName, (const char *)lfnbuf2);  // Added
				memset(lfnbuf1, 0, sizeof(lfnbuf1));
				memset(lfnbuf2, 0, sizeof(lfnbuf1));
				//printf("FullName: \"%s\" Shortname: %s\n", new_entry->Name, new_entry->ShortName);
			}
			else {
				//printf("LONGNAME :2\n");
				strcat(lfnbuf1, (const char *)temp.FileName);
				if(temp.Ext[0] != ' ') { // If we actually have an extension....add it in
					if(temp.Attr == VOLUME_ID) { // Extension is part of the VOLUME name(node->FileName)
						strcat(lfnbuf1, temp.Ext);
					}
					else {
						strcat(lfnbuf1, ".");
						strcat(lfnbuf1, temp.Ext);
					}
				}
				
				new_entry->Name = (char *)malloc(strlen(lfnbuf2));
				new_entry->ShortName = (char *)malloc(strlen(lfnbuf1));
				strcpy(new_entry->Name, lfnbuf2);
				strcpy(new_entry->ShortName, lfnbuf1);  // Added
				memset(lfnbuf1, 0, sizeof(lfnbuf1));
				memset(lfnbuf2, 0, sizeof(lfnbuf2));
				//printf("FullName: \"%s\" Shortname: %s\n", new_entry->Name, new_entry->ShortName);
			}			
			
			new_entry->Attr = temp.Attr;
			new_entry->FileSize = temp.FileSize;
			new_entry->Data_Clusters = GetClusterList(fat_table, temp.FstClusLO);
			new_entry->Location[0] = sector_loc; // Sector
			new_entry->Location[1] = var; // Byte in sector
			new_entry->Parent = parent;
			new_entry->Children = NULL;
			new_entry->Next = NULL;
			
			printf("FileName: %s ShortName: %s Parent: %s  Attr: %x Cluster: %d  \n", new_entry->Name, new_entry->ShortName, parent->Name, new_entry->Attr, new_entry->Data_Clusters->Cluster_Num);
			
			if(parent->Children == NULL) {
				parent->Children = new_entry;
			} else {
				child = parent->Children;
			    while (child->Next != NULL) {
				    child = child->Next;
				}
				child->Next = new_entry;
			}
			
			if(new_entry->Attr == DIRECTORY && new_entry->Name[0] != 0x2E && !(new_entry->Name[0] == 0x2E && new_entry->Name[1] == 0x2E)) { // Dont do this for .(current direct) and ..(parent) 
			    //printf("Found a folder !!\n");
			 
				node = new_entry->Data_Clusters;
				
				do {	
				    new_sector_loc = fat->data_sec_loc + (node->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster; // Calculate sector loc from cluster given
					for(j = 0;j < fat->boot_sector.sectors_per_cluster; j++) {
						parse_directory_sector(fat, new_entry, new_sector_loc + j, fat_table);
					}
					node = node->Next;
				}while(node != NULL);
			}

			var += ENTRYSIZE;
		}
	}
	
	free(buf);
	free(lfnbuf1);
	free(lfnbuf2);
}

uint8_t *fat_read_data(fatfs_t *fat, node_entry_t *file, int count, int pointer)
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
	uint8_t *buf = (uint8_t *)malloc(sizeof(uint8_t)*count); // 
	uint8_t sector[512]; // Each sector is 512 bytes long

	/* Clear */
	memset(buf, 0, sizeof(buf));

	/* While we still have more to read, do it */
	while(cnt)
	{
		// Get the number of the sector in the cluster we want to read
		numOfSector = ptr / bytes_per_sector;

		// Figure out which cluster we are reading from
		clusterNodeNum = numOfSector / fat->boot_sector.sectors_per_cluster;

		// Set to first cluster
		node = file->Data_Clusters; 

		// Start at 1 because we set to first cluster above. Advance to the cluster we want to read from.
		for(i = 1; i < clusterNodeNum; i++) 
		{
			node = node->Next;
		}

		/* Calculate Sector Location from cluster and sector we want to read */
		sector_loc = fat->data_sec_loc + ((node->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster) + numOfSector; 

		/* Clear out before reading into */
		memset(sector, 0, sizeof(sector));

		/* Read 1 sector */
		if(fat->dev->read_blocks(fat->dev, sector_loc, 1, sector) != 0)
		{
			printf("Couldn't read the sector\n");
			return NULL;
		}

		/* Calculate current byte position in the sector we want to start reading from */
		curSectorPos = ptr - (numOfSector * bytes_per_sector);

		/* Calculate the number of chars to read */
		numToRead = ((bytes_per_sector - curSectorPos) > cnt) ? cnt : (bytes_per_sector - curSectorPos);

		/* Concat */
		strncat(buf, sector + curSectorPos, numToRead);

		/* Advance variables */
		ptr += numToRead;  // Advance file pointer 
		cnt -= numToRead;  // Decrease bytes left to read 
	}

	return buf;
}

int fat_write_data(fatfs_t *fat, node_entry_t *file, uint8_t *bbuf, int count, int pointer)
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
	uint8_t *buf = (uint8_t *)malloc(sizeof(uint8_t)*count); // 
	uint8_t sector[512]; // Each sector is 512 bytes long

	/* Clear */
	memset(sector, 0, sizeof(sector));

	/* While we still have more to write, do it */
	while(cnt)
	{
		/* Get the number of the sector in the cluster we want to write to */
		numOfSector = ptr / bytes_per_sector;

		/* Figure out which cluster we are writing to */
		clusterNodeNum = numOfSector / fat->boot_sector.sectors_per_cluster;

		/* Set to first cluster */
		node = file->Data_Clusters; 

		/* Start at 1 because we set to first cluster above. Advance to the cluster we want to write to */
		for(i = 1; i < clusterNodeNum; i++) 
		{
			node = node->Next;
		}

		/* Check if node equals NULL. If it does then allocate another cluster for this file */
                if(node == NULL)
                {
                    if(node = allocate_cluster(fat, file->Data_Clusters))
                    {
                        printf("All out of clusters to Allocate\n");
                        return -1;
                    }
                }

		/* Calculate Sector Location from cluster and sector we want to read and then write to */
		sector_loc = fat->data_sec_loc + ((node->Cluster_Num - 2) * fat->boot_sector.sectors_per_cluster) + numOfSector; 

		/* Clear out before reading into */
		memset(sector, 0, sizeof(sector));

		/* Read 1 sector */
		fat->dev->read_blocks(fat->dev, sector_loc, 1, sector); 

		//printf("Read Sector: %s\n", sector);

		/* Calculate current byte position in the sector we want to start writing to */
		curSectorPos = ptr - (numOfSector * bytes_per_sector);

		/* Calculate the number of chars to write */
		numToWrite = ((bytes_per_sector - curSectorPos) > cnt) ? cnt : (bytes_per_sector - curSectorPos);

		/* Memcpy */
		memcpy(sector + curSectorPos, bbuf, numToWrite);

		/* Write one sector */
		if(fat->dev->write_blocks(fat->dev, sector_loc, 1, sector))
		{
			printf("Couldnt write the blocks\n");
			return -1;
		}

		/* Advance variables */
		ptr += numToWrite;  // Advance file pointer 
		cnt -= numToWrite;  // Decrease bytes left to write
	}

	return count;
}

void update_fat_entry(fatfs_t *fat, node_entry_t *file)
{
	//char buffer[20];
	uint8_t sector[512]; // Each sector is 512 bytes long
	
	time_t rawtime;
	short int tme = 0;
	short int date = 0;
	struct tm * timeinfo;

	time (&rawtime);
	timeinfo = localtime (&rawtime);
	
	tme = generate_time(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec/2);
	date = generate_date(1900 + timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
	
	memset(sector, 0, sizeof(sector));

	/* Read fat sector */
	fat->dev->read_blocks(fat->dev, file->Location[0], 1, sector);

	/* Edit Entry */
	memcpy(sector + file->Location[1] + FILESIZE, &(file->FileSize), 4); 
	memcpy(sector + file->Location[1] + LASTACCESSDATE, &(date), 2);
	memcpy(sector + file->Location[1] + LASTWRITETIME, &(tme), 2);
	memcpy(sector + file->Location[1] + LASTWRITEDATE, &(date), 2);

	/* Write it back */
	fat->dev->write_blocks(fat->dev, file->Location[0], 1, sector);
        
        if(file->Parent->Parent != NULL) // If this file/folder is in a 
        {                                // subdirectory[Not the ROOT Directory] update it as well
            memset(sector, 0, sizeof(sector));
            
            /* Read fat sector */
            fat->dev->read_blocks(fat->dev, file->Parent->Location[0], 1, sector);
            
            /* Edit Folder */
            memcpy(sector + file->Parent->Location[1] + LASTACCESSDATE, &(date), 2);
            memcpy(sector + file->Parent->Location[1] + LASTWRITETIME, &(tme), 2);
            memcpy(sector + file->Parent->Location[1] + LASTWRITEDATE, &(date), 2);

            /* Write it back */
            fat->dev->write_blocks(fat->dev, file->Parent->Location[0], 1, sector);
        }
        
	return;
}

void delete_tree_entry(node_entry_t * node)
{
    cluster_node_t  *old;
    
    free(node->Name); // Free the name
    
    // Free the LL Data Clusters
    while(node->Data_Clusters)
    {
        old = node->Data_Clusters;
        node->Data_Clusters = node->Data_Clusters->Next;
        free(old);
    }
    
    node = NULL;
}

void delete_directory_tree(node_entry_t * node)
{
    static int count = 0;
    
    if(node == NULL)
        return;
    
    delete_directory_tree(node->Next);
    delete_directory_tree(node->Children);
    
    if(node->Next == NULL && node->Children == NULL)
    {
        delete_tree_entry(node);
        count++;
        printf("Count: %d\n", count);
    }
}

cluster_node_t *allocate_cluster(fatfs_t *fat, cluster_node_t  *cluster)
{
    int fat_index = 2;
    int cluster_num = -1;
    cluster_node_t *clust = cluster;
    cluster_node_t *temp = NULL;
    
    unsigned char *fat_table;
    
    read_fat_table(fat, &fat_table); 
    
    // Handle NULL cluster(Happens when a new file is created)
    if(cluster != NULL) {  
        while(clust->Next)
        {
            clust = clust->Next;
        }
    }
    
    // We are now at the end of the LL. Search for a free cluster.
    while(fat_index < fat->boot_sector.bytes_per_sector*fat->boot_sector.table_size_16) { // Go through Table one index at a time
	
        memcpy(&cluster_num, &fat_table[fat_index*2], 2);
		
		printf("Cluster num value found: %d\n", cluster_num);
        
        // If we found a free entry
        if(cluster_num == 0xFFFF0000)
        {
            printf("Found a free cluster entry -- Index: %d\n", fat_index);
		
            cluster_num = 0xFFF8;
            if(cluster != NULL) // Cant change what doesnt exist
                memcpy(&fat_table[clust->Cluster_Num*2], &fat_index, 2); // Change the table to indicate an allocated cluster
            memcpy(&fat_table[fat_index*2], &cluster_num, 2); // Change the table to indicate new end of file
            write_fat_table(fat,fat_table); // Write table back to SD
            
            temp = (cluster_node_t *)malloc(sizeof(cluster_node_t));
            temp->Cluster_Num = fat_index;
            temp->Next = NULL;
            
            return temp;
        }
        else
        {
            fat_index++;
        }
    }
	
	printf("Didnt find a free Cluster\n");
    
    /* Didn't find a free cluster */
    return NULL;
}

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

    memset(ufn, 0, strlen(fn)+1);

    /* Make sure the files/folders are capitalized */
    while (fn[i])
    {
       c = fn[i];
       ufn[i] = (char)toupper(c);
       i++;
    }

    pch = strtok(ufn,"/");  // Grab Root
	
    if(strcmp(pch, target->Name) == 0) {
        pch = strtok (NULL, "/"); 

        if(pch != NULL) {
            child = target;
        }

        while(pch != NULL) {
            if(target = isChildof(child, pch)) 
            {
				if(target->Attr & READ_ONLY) // Check and make sure directory is not read only.
				{
					errno = EROFS;
					return NULL;
				}
				
                pch = strtok (NULL, "/");
              
                if(pch != NULL) {
                    child = target->Children;
                }
            }
            else 
            {
                filename = pch; // We possibly have the filename
                
                printf("Possible Name: %s\n", filename);
                
                // Return NULL if we encounter a directory that doesn't exist
                if(pch = strtok (NULL, "/")) { // See if there is more to parse through
                    errno = ENOTDIR;
                    return NULL;               // if there is that means we are looking in a directory
                }                              // that doesn't exist.
                
                // Make sure the filename is valid 
                if(correct_filename(filename) == -1)
                {
                    return NULL;
                }
                
                // Directory trees file/folder names are always capitalized 
                // Make sure we get the original case of the file/folder name to save to SD
                entry_filename = (char *)malloc(strlen(filename) + 1);
                memset(entry_filename, 0, strlen(filename) + 1);
                strncpy(entry_filename, fn + (strlen(fn) - strlen(filename)), strlen(filename));
                entry_filename[strlen(filename)] = '\0';
                
                printf("Entry Name: %s\n", entry_filename);
           
                // Create file/folder
                newfile = (node_entry_t *) malloc(sizeof(node_entry_t));
                newfile->Name = filename;
                newfile->Attr = attr;
                newfile->FileSize = 0;
                newfile->Data_Clusters = NULL; 
                newfile->Parent = child;
                newfile->Children = NULL;  
                newfile->Next = NULL;      // Adding to end of Parent(var child) list of children
                
                if(generate_and_write_entry(fat, entry_filename, newfile) == -1)
                {
					printf("Didnt Create file/folder\n");
                    delete_tree_entry(newfile);  //This doesn't take care of removing clusters from SD card
                    errno = EDQUOT;
                    return NULL;  
                }
                
                // Created file on SD successfully. Now add this new file to the directory tree
                temp = child->Children;
                
                // Folder has no children. Set Children to be this new file
                if(temp == NULL)
                {
                    child->Children = newfile;
                }
                else // Otherwise go to the end of the LL of Children
                {
                    while(temp->Next != NULL)
                    {
                        temp = temp->Next;
                    }
                    
                    temp->Next = newfile;
                }      
                
                return newfile;
            }
        }
    }
    
    // Path was invalid from the beginning
    errno = ENOTDIR;
    return NULL;
}

int generate_and_write_entry(fatfs_t *fat, char *entry_name, node_entry_t *newfile)
{
	int i;
	int j;
	int *loc;
	int last;
	unsigned char order = 1;
	int offset = 0;
	int longfilename = 0;
	char *shortname;
	unsigned char checksum;

    fat_dir_entry_t entry;
	fat_lfn_entry_t *lfn_entry;
	fat_lfn_entry_t *lfn_entry_list[20];  // Can have a maximum of 20 long file name entries
	
	for(i = 0; i < 20; i++)
		lfn_entry_list[i] = NULL;
    
    shortname = generate_short_filename(newfile->Parent, entry_name, newfile->Attr, &longfilename);
	checksum = generate_checksum(shortname);
	
	printf("Shortfile Name: %s\n", shortname);
    
	// Make lfn entries
	if(longfilename)
    {
		i = 0;
		
		while(offset < strlen(entry_name))
		{
			// Build long name entries
			lfn_entry_list[i++] = generate_long_filename_entry(entry_name+offset, checksum, order++);
			offset += 13;
		}
		
		printf("Built %d Long file entrie(s) \n", (order-1));
		
		last = i - 1; // Refers to last entry in array
		
		lfn_entry_list[last]->Order |= 0x40; // Set(OR) last one to special value order
		
		// Get loc for j amount of entries plus 1(shortname entry). Returns an int array Sector(loc[0]), ptr(loc[1])
		loc = get_free_locations(fat, newfile->Parent, (int)order);
		
		printf("Sector: %d Ptr: %d\n", loc[0], loc[1]);
		
		// Write it(reverse order)
		for(i = last; i >= 0; i--)
		{
			write_entry(fat, lfn_entry_list[i], LONGFILENAME, loc);
				
			printf("Wrote lfn entry\n");	
				
			// Do calculations for sector if need be
			loc[1] += 32;
			
			if((loc[1]/fat->boot_sector.bytes_per_sector) >= 1)
			{
				loc[0]++;   // New sector
				loc[1] = 0; // Reset ptr in sector
			}
		}
		
		printf("Finished writing lfn\n");
    }
	
	/* Allocate a cluster */
	newfile->Data_Clusters = allocate_cluster(fat, NULL);
	
	// Make regular entry and write it to SD FAT
	strncpy(entry.FileName, shortname, 8);
	entry.FileName[8] = '\0';
	strncpy(entry.Ext, shortname+9, 3 ); // Skip filename and '.' 
	entry.Ext[3] = '\0';
	entry.Attr = newfile->Attr;
	entry.FileSize = 0;   // For both new files and new folders
	entry.FstClusLO = newfile->Data_Clusters->Cluster_Num;
	
	// Write it (after long file name entries)
	if(longfilename)
	{
		write_entry(fat, &entry, newfile->Attr, loc);
	} else
	{
		loc = get_free_locations(fat, newfile->Parent, 1);
		printf("Sector: %d Ptr: %d\n", loc[0], loc[1]);
		write_entry(fat, &entry, newfile->Attr, loc);
	}
	
	/* Save the locations */
	newfile->Location[0] = loc[0];  
	newfile->Location[1] = loc[1]; 
	
	free(loc);
    
    return 0;
}
