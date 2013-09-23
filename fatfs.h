
#ifndef _FAT_FATFS_H_
#define _FAT_FATFS_H_

#include <sys/cdefs.h>
__BEGIN_DECLS

#include <kos/blockdev.h>

#include "fat_defs.h"

fatfs_t *fat_fs_init(const char *mp, kos_blockdev_t *bd);
void fat_fs_shutdown(fatfs_t  *fs);

unsigned int read_fat_table_value(fatfs_t *fat, int byte_index);
void write_fat_table_value(fatfs_t *fat, int byte_index, int value); 

__END_DECLS

#endif /* _FAT_FATFS_H_ */
