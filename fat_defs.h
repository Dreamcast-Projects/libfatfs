

#ifndef _FAT_DEFS_H_ 
#define _FAT_DEFS_H_

__BEGIN_DECLS

#include <kos/blockdev.h>

#include "dir_entry.h"
#include "boot_sector.h"

#define FAT16 0
#define FAT32 1

#define FAT16TYPE1 0x04  /* 32MB */
#define FAT16TYPE2 0x06  /* Over 32 to 2GB */ 
#define FAT32TYPE1 0x0B  /*  */
#define FAT32TYPE2 0x0C  /*  */

/* typedef struct fatfs fatfs_t; */

struct fatfs
{
    kos_blockdev_t   *dev;
    fat_BS_t         boot_sector;

    /* Filesystem globals */ 
	unsigned char    *mount;                   /* Save what the user mounts the sd card to e.g. "/sd" so I can add it back when certain functions(open, unlink, mkdir) are called */
	unsigned short   fat_type;                 /* 0 - Fat16, 1 - Fat32 */
	unsigned short   table_size;               /* */
    unsigned short   root_dir_sectors_num;     /* The number of sectors the root directory consists of. Should be zero for Fat32 */
    unsigned short   root_dir_sec_loc;         /* The first sector where the root directory starts */
    unsigned short   file_alloc_tab_sec_loc;   /* The first sector where the fat allocation table starts */
    unsigned short   data_sec_loc;             /* The first sector where the data starts(Location of cluster 2) */
    unsigned int     data_sectors_num;         /* The number of data sectors. Data sectors are sectors that exist after the boot sector, fat tables, and root directory */
    unsigned int     total_clusters_num;       /* The total number of data clusters. */

    struct node_entry     *root;

    /* [Optional] Thread Safety */
    void             (*fl_lock)(void);
    void             (*fl_unlock)(void);
};

__END_DECLS

#endif /* _FAT_DEFS_H_ */
