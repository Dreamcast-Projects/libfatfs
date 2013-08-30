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

static int initted = 0;

/* Read the Fat table from the SD card and stores it in table */
void read_fat_table(fatfs_t *fat, unsigned char **table) 
{
    int i = 0;
    unsigned char *temp = (unsigned char *)malloc(512*sizeof(unsigned char));
    *table = (unsigned char *)malloc(fat->boot_sector.bytes_per_sector*fat->boot_sector.table_size_16*sizeof(unsigned char));

    memset(temp, 0, 512*sizeof(unsigned char));
    memset((*table), 0, fat->boot_sector.bytes_per_sector*fat->boot_sector.table_size_16*sizeof(unsigned char));

    for(i = 0; i < fat->boot_sector.table_size_16; i++) {
            fat->dev->read_blocks(fat->dev, fat->file_alloc_tab_sec_loc + i, 1, temp);
            memcpy((*table) + 512*i, temp, 512);
    }

    free(temp);
}

void write_fat_table(fatfs_t *fat, unsigned char *table) 
{
    int i;
    unsigned char *temp = (unsigned char *)malloc(512*sizeof(unsigned char));
    memset(temp, 0, 512*sizeof(unsigned char));
    
    for(i = 0; i < fat->boot_sector.table_size_16; i++) {
        //memcpy((*table) + 512*i, temp, 512);
        memcpy(temp, table + 512*i, 512);
        fat->dev->write_blocks(fat->dev, fat->file_alloc_tab_sec_loc + i, 1, temp);
    }
    
    free(temp);
}

fatfs_t *fat_fs_init(const char *mp, kos_blockdev_t *bd) {
    	int i;
	char c;
	char *fmp = (char *)malloc(strlen(mp)+1);
    	fatfs_t *rv;
	unsigned char *fat_table;
	
	memset(fmp, 0, strlen(mp)+1);
	
	i = 0;

	/* Make sure the files/folders are capitalized */
	while (mp[i])
    	{
       		c = mp[i];
      	 	fmp[i] = (char)toupper(c);
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

	#ifdef FAT2FS_DEBUG
    	fat_print_bootsector(&(rv->boot_sector));
    	#endif

	rv->root_dir_sectors_num = ((rv->boot_sector.root_entry_count * ENTRYSIZE) + (rv->boot_sector.bytes_per_sector - 1)) / rv->boot_sector.bytes_per_sector;
	rv->root_dir_sec_loc = rv->boot_sector.reserved_sector_count + (rv->boot_sector.table_count * rv->boot_sector.table_size_16); 
	rv->file_alloc_tab_sec_loc = rv->boot_sector.reserved_sector_count;
	rv->data_sec_loc = rv->root_dir_sec_loc + rv->root_dir_sectors_num;
	
	if(rv->boot_sector.total_sectors_16 != 0) {
		rv->data_sectors_num = rv->boot_sector.total_sectors_16 - rv->data_sec_loc;
	} else {
		rv->data_sectors_num = rv->boot_sector.total_sectors_32 - rv->data_sec_loc;
	}
	
	rv->total_clusters_num = rv->data_sectors_num/rv->boot_sector.sectors_per_cluster;
	
	#ifdef FAT2FS_DEBUG
	printf("Root directory number of sectors: %d\n", rv->root_dir_sectors_num);
	printf("Root directory sector location: %d\n", rv->root_dir_sec_loc);
	printf("File allocation table sector location: %d\n", rv->file_alloc_tab_sec_loc);
	printf("File/folder data starts at sector: %d\n", rv->data_sec_loc);
	printf("Total number of data sectors: %d\n", rv->data_sectors_num);
	printf("Total number of clusters: %d\n\n\n", rv->total_clusters_num);
	#endif
	
	rv->root = (node_entry_t *)malloc(sizeof(node_entry_t));
	rv->root->Name = remove_all_chars(fmp, '/');          /*  */
	rv->root->Attr = DIRECTORY;     /* Root directory is obviously a directory */
	rv->root->Data_Clusters = NULL; /* Root directory has no data clusters associated with it */
	rv->root->Parent = NULL;        /* Should always be NULL for root*/
	rv->root->Children = NULL;      /* Changes when Directory Tree is built */
	rv->root->Next = NULL;          /* Should always be NULL. Root is the top most directory and has no equals */

	/* Get FAT */
	read_fat_table(rv,&fat_table); 
	
	/*  Build Directory Tree */
	for(i = 0; i < rv->root_dir_sectors_num; i++) {
		parse_directory_sector(rv, rv->root, rv->root_dir_sec_loc + i, fat_table); 
	}	

	free(fat_table);
	
	return rv;
}

void fat_fs_shutdown(fatfs_t *fs) {

    fs->dev->shutdown(fs->dev);
    
    // Free the Directory tree fs->root
    delete_directory_tree(fs->root);

    free(fs);
}
