

#ifndef _FAT_DEFS_H_ 
#define _FAT_DEFS_H_

__BEGIN_DECLS

#include <kos/blockdev.h>

//#include "dir_entry.h"
#include "boot_sector.h"

#define FAT16TYPE1 0x04  /* 32MB */
#define FAT16TYPE2 0x06  /* Over 32 to 2GB */ 

struct node_entry;
typedef struct node_entry node_entry_t;

typedef struct fatfs
{
	kos_blockdev_t   *dev;
	fat_BS_t         boot_sector;

        // Filesystem globals    
	unsigned short   root_dir_sectors_num;     /* The number of sectors the root directory consists of */
	unsigned short   root_dir_sec_loc;         /* The first sector where the root directory starts */
	unsigned short   file_alloc_tab_sec_loc;   /* The first sector where the fat allocation table starts */
	unsigned short   data_sec_loc;             /* The first sector where the data starts(Location of cluster 2) */
	unsigned int     data_sectors_num;         /* The number of data sectors. Data sectors are sectors that exist after the boot sector, fat tables, and root directory */
	unsigned int     total_clusters_num;       /* The total number of data clusters. */
	
	node_entry_t     *root;

    // [Optional] Thread Safety
    void             (*fl_lock)(void);
    void             (*fl_unlock)(void);

} fatfs_t;

__END_DECLS

#endif /* _FAT_DEFS_H_ */