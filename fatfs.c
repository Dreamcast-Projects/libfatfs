/* KallistiOS ##version##

   fat2fs.c
*/

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "utils.h"
#include "fat_defs.h"
#include "dir_entry.h"
#include "boot_sector.h"

static short sector_offset = -1; /* Makes it to where we always have to read a sector (FAT table) first */
static unsigned char buffer[512];

/* Read the Fat table from the SD card and stores it in table */
unsigned int read_fat_table_value(fatfs_t *fat, int byte_index) 
{
	unsigned int read_value;
	short byte_offset = (fat->fat_type == FAT16) ? 2 : 4;
	short ptr_offset;
	
	/* Check if we have to read a new sector */
	if(sector_offset != (byte_index / fat->boot_sector.bytes_per_sector))
	{
		/* Calculate sector offset from file_alloc_tab_sec_loc */
		sector_offset = byte_index / fat->boot_sector.bytes_per_sector;
	
		/* Clear buffer */
		memset(buffer, 0, 512*sizeof(unsigned char));
	
		/* Read new sector */
		fat->dev->read_blocks(fat->dev, fat->file_alloc_tab_sec_loc + sector_offset, 1, buffer);
	}
	
	ptr_offset = byte_index % fat->boot_sector.bytes_per_sector;
	
	memcpy(&read_value, &buffer[ptr_offset], byte_offset);
	
	return read_value;
}

void write_fat_table_value(fatfs_t *fat, int byte_index, int value) 
{
	short byte_offset = (fat->fat_type == FAT16) ? 2 : 4;
	short ptr_offset;
	
	/* Check if we have to read a new sector */
	if(sector_offset != (byte_index / fat->boot_sector.bytes_per_sector))
	{
		/* Calculate sector offset from file_alloc_tab_sec_loc */
		sector_offset = byte_index / fat->boot_sector.bytes_per_sector;
	
		/* Clear buffer */
		memset(buffer, 0, 512*sizeof(unsigned char));
	
		/* Read new sector */
		fat->dev->read_blocks(fat->dev, fat->file_alloc_tab_sec_loc + sector_offset, 1, buffer);
	}
	
	ptr_offset = byte_index % fat->boot_sector.bytes_per_sector;
	
	memcpy(&buffer[ptr_offset], &(value), byte_offset);
    
    fat->dev->write_blocks(fat->dev, fat->file_alloc_tab_sec_loc + sector_offset, 1, buffer);
}

fatfs_t *fat_fs_init(const char *mp, kos_blockdev_t *bd) {
    unsigned int i;
	
	char c;
	char *fmp = NULL; 
    fatfs_t *rv;
	
	/* For Fat32 */
	int new_sector_loc = 0;
	fat_extBS_32_t  fat32_boot_ext;
	cluster_node_t  *cluster = NULL;
	cluster_node_t  *node = NULL;
	
	fmp = malloc(strlen(mp)+1);
	memset(fmp, 0, strlen(mp)+1);
	
	i = 0;

	/* Make sure the files/folders are capitalized */
	while (mp[i])
	{
		c = mp[i];
		fmp[i] = toupper((int)c);
		i++;
	}

    if(bd->init(bd))
		return NULL;

    if(!(rv = (fatfs_t *)malloc(sizeof(fatfs_t)))) {
		bd->shutdown(bd);
		return NULL;
	}

	memset(rv, 0, sizeof(fatfs_t));
	rv->dev = bd;

	if(fat_read_bootsector(&(rv->boot_sector), bd)) {
		free(rv);
		bd->shutdown(bd);
		return NULL;
   	}

#ifdef FATFS_DEBUG
	fat_print_bootsector(&(rv->boot_sector));
#endif

	if(rv->boot_sector.table_size_16 > 0)
	{
		rv->fat_type = FAT16;
		rv->byte_offset = 2;
		rv->root_cluster_num = 0; /* Not used. Set it to zero */
		rv->table_size = rv->boot_sector.table_size_16;
		rv->root_dir_sectors_num = ((rv->boot_sector.root_entry_count * ENTRYSIZE) + (rv->boot_sector.bytes_per_sector - 1)) / rv->boot_sector.bytes_per_sector;
		rv->root_dir_sec_loc = rv->boot_sector.reserved_sector_count + (rv->boot_sector.table_count * rv->boot_sector.table_size_16); 
		rv->file_alloc_tab_sec_loc = rv->boot_sector.reserved_sector_count;
		rv->data_sec_loc = rv->root_dir_sec_loc + rv->root_dir_sectors_num;
	}
	else
	{
		memset(&fat32_boot_ext, 0, sizeof(fat_extBS_32_t));
		memcpy(&fat32_boot_ext, &(rv->boot_sector.extended_section), sizeof(fat_extBS_32_t));
		
		rv->fat_type = FAT32;
		rv->byte_offset = 4;
		rv->root_cluster_num = fat32_boot_ext.root_cluster;
		rv->table_size = fat32_boot_ext.table_size_32;
		rv->root_dir_sectors_num = 0;
		rv->root_dir_sec_loc = rv->boot_sector.reserved_sector_count + (rv->boot_sector.table_count * fat32_boot_ext.table_size_32); 
		rv->file_alloc_tab_sec_loc = rv->boot_sector.reserved_sector_count;
		rv->data_sec_loc = rv->root_dir_sec_loc + rv->root_dir_sectors_num;
	}
	
	if(rv->boot_sector.total_sectors_16 != 0) {
		rv->data_sectors_num = rv->boot_sector.total_sectors_16 - rv->data_sec_loc;
	} else {
		rv->data_sectors_num = rv->boot_sector.total_sectors_32 - rv->data_sec_loc;
	}
	
	rv->total_clusters_num = rv->data_sectors_num/rv->boot_sector.sectors_per_cluster;
	
#ifdef FATFS_DEBUG
	printf("Number of sectors the FAT table takes up: %d\n", rv->table_size);
	printf("Root directory number of sectors: %d\n", rv->root_dir_sectors_num);
	printf("Root directory sector location: %d\n", rv->root_dir_sec_loc);
	printf("File allocation table sector location: %d\n", rv->file_alloc_tab_sec_loc);
	printf("File/folder data starts at sector: %d\n", rv->data_sec_loc);
	printf("Total number of data sectors: %d\n", rv->data_sectors_num);
	printf("Total number of clusters: %d\n\n\n", rv->total_clusters_num);
#endif
	
	rv->mount = malloc(strlen(fmp)+ 1);
	strncpy(rv->mount, fmp, strlen(fmp));
	rv->mount[strlen(fmp)] = '\0';
	
#if defined (FATFS_CACHEALL) 
	
	if(rv->fat_type == FAT32)
	{
		cluster = malloc(sizeof(cluster_node_t));
		cluster = build_cluster_linklist(rv, fat32_boot_ext.root_cluster);
	}
	
	rv->root = malloc(sizeof(node_entry_t));
	rv->root->Name = (unsigned char *)remove_all_chars(fmp, '/');          /*  */
	rv->root->ShortName = (unsigned char *)remove_all_chars(fmp, '/');
	rv->root->Attr = DIRECTORY;           /* Root directory is obviously a directory */
	rv->root->Data_Clusters = cluster;    /* Root directory has no data clusters associated with it(FAT16). Non-NULL with FAT32 */
	rv->root->Parent = NULL;              /* Should always be NULL for root*/
	rv->root->Children = NULL;            /* Changes when Directory Tree is built */
	rv->root->Next = NULL;                /* Should always be NULL. Root is the top most directory and has no equal */
	
	/*  Build Directory Tree */
	if(rv->fat_type == FAT16) /* Fat16 */
	{
		for(i = 0; i < rv->root_dir_sectors_num; i++) {
			parse_directory_sector(rv, rv->root, rv->root_dir_sec_loc + i); 
		}	
	}
	else                      /* Fat32 */
	{
		node = rv->root->Data_Clusters;
				
		do {	
			new_sector_loc = rv->data_sec_loc + (node->Cluster_Num - 2) * rv->boot_sector.sectors_per_cluster; /* Calculate sector loc from cluster given */
			for(i = 0;i < rv->boot_sector.sectors_per_cluster; i++) {
				parse_directory_sector(rv, rv->root, new_sector_loc + i);
			}
			node = node->Next;
		} while(node != NULL);
	}
	
	free(fmp);
	
#endif
	
	return rv;
}

void fat_fs_shutdown(fatfs_t *fs) {

    fs->dev->shutdown(fs->dev);

    free(fs);
}
