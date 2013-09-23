

#ifndef _FS_FAT_H_
#define _FS_FAT_H_

__BEGIN_DECLS

#include <kos/blockdev.h>

int fs_fat_init(void);

int fs_fat_shutdown(void);

/* Mount flags */
#define FS_FAT_MOUNT_READONLY      0x00000000  /**< \brief Mount read-only */
#define FS_FAT_MOUNT_READWRITE     0x00000001  /**< \brief Mount read-write */

int fat_partition(uint8 partition_type);

int fs_fat_mount(const char *mp, kos_blockdev_t *dev, uint32_t flags);

int fs_fat_unmount(const char *mp);

__END_DECLS

#endif /* _FS_FAT_H_ */
