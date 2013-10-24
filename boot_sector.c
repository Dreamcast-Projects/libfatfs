
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "boot_sector.h"

int fat_read_bootsector (kos_blockdev_t *bd, fat_BS_t *bs) 
{
    unsigned char *buf;
	
    if(bd == NULL) 
		return -EIO;

    if(!(buf = malloc(512*sizeof(unsigned char)))) 	
		return -ENOMEM;
		
	if(bd->read_blocks(bd, 0, 1, buf))
	{
		free(buf);
        return -EIO;
	}
		
	memcpy(bs, buf, sizeof(fat_BS_t));
    free(buf);
	
    return 0;	
}

#ifdef FATFS_DEBUG
void fat_print_bootsector(const fat_BS_t* fat) {
	fat_extBS_32_t ext;

    printf("\n\nFATFS Boot Sector: \n\n");
    printf("Boot JMP: %x:%x:%x\n", fat->bootjmp[0],fat->bootjmp[1],fat->bootjmp[2]);
	printf("OEM Name: %s\n", fat->oem_name);
	printf("Bytes Per Sector: %d\n", fat->bytes_per_sector);
	printf("Sectors Per Cluster: %d\n", fat->sectors_per_cluster);
	printf("Reserved Sector Count: %d\n", fat->reserved_sector_count);
	printf("Table Count: %d\n", fat->table_count);
	printf("Root Entry Count: %d\n", fat->root_entry_count);
	printf("Total Sectors 16: %d\n", fat->total_sectors_16);
	printf("Media Type: %x\n", fat->media_type);
	printf("Table Size 16: %d\n", fat->table_size_16);
	printf("Sectors Per Track: %d\n", fat->sectors_per_track);
	printf("Head Side Count: %d\n", fat->head_side_count);
	printf("Hidden Sector Count: %d\n", fat->hidden_sector_count);
	printf("Total Sectors 32: %d\n", fat->total_sectors_32);
	
	if(fat->total_sectors_16 == 0)
	{
		memset(&ext,0, sizeof(fat_extBS_32_t));
		
		memcpy(&ext, &(fat->extended_section), sizeof(fat_extBS_32_t));
		
		printf("Table Size 32(Fat32 Only): %d\n", ext.table_size_32);
		printf("Starting Root Cluster(Fat32 Only): %d\n", ext.root_cluster);
		printf("FSInfo Sector location(Fat32 Only): %d\n", ext.fat_info);
	}
	printf("\n\n");
}
#endif