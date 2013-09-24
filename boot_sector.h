

#ifndef _FAT_BOOT_SECTOR_
#define _FAT_BOOT_SECTOR_

#include <sys/cdefs.h>
__BEGIN_DECLS

#define FAT2FS_DEBUG 1

#include <stdint.h>

#include <kos/blockdev.h>

/* Following three structs are copy+pasted from http://wiki.osdev.org/FAT#Boot_Record 
   NOTE: The boot sector of all FAT file systems is 512 bytes. So grab the 512 bytes of info and
   memcpy that into a fat_BS_t structure. Then once you figure out which file system FAT12/16 or FAT32
   (table_size_16 > 0 for FAT12/16 and table_size_16 = 0 for FAT32) you can memcopy/cast the 'extended_section' 
   of the fat_BS_t struct into the proper struct(fat_extBS_16[works for FAT12 as well] or fat_extBS_32).
 */

typedef struct fat_extBS_32
{
	/* extended fat32 stuff */
	unsigned int		table_size_32;
	unsigned short		extended_flags;
	unsigned short		fat_version;
	unsigned int		root_cluster;
	unsigned short		fat_info;
	unsigned short		backup_BS_sector;
	unsigned char 		reserved_0[12];
	unsigned char		drive_number;
	unsigned char 		reserved_1;
	unsigned char		boot_signature;
	unsigned int 		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];
 
}__attribute__((packed)) fat_extBS_32_t;
 
typedef struct fat_extBS_16
{
	/* extended fat12 and fat16 stuff */
	unsigned char		bios_drive_num;          /* Drive number. Nothing important. */
	unsigned char		reserved1;
	unsigned char		boot_signature;
	unsigned int		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];
 
}__attribute__((packed)) fat_extBS_16_t;
 
typedef struct fat_BS
{
	unsigned char 		bootjmp[3];              /* The first three bytes EB 3C and 90 disassemble to JMP SHORT 3C NOP. Used to jump to data. Nothing for you to worry about. */
	unsigned char 		oem_name[8];             /* OEM identifier */
	unsigned short 	    bytes_per_sector;        /* The number of Bytes per sector (remember, all numbers are in the little-endian format) */
	unsigned char		sectors_per_cluster;     /* Number of sectors per cluster. */
	unsigned short		reserved_sector_count;   /* Number of reserved sectors. The boot record sectors are included in this value. */
	unsigned char		table_count;             /* Number of File Allocation Tables (FAT's) on the storage media. Often this value is 2. */
	unsigned short		root_entry_count;        /* Number of directory entries (must be set so that the root directory occupies entire sectors). */
	unsigned short		total_sectors_16;        /* The total sectors in the logical volume. If this value is 0, it means there are more than 65535 sectors in the volume, and the actual count is stored in "Large Sectors (bytes 32-35)[total_sectors_32]". */
	unsigned char		media_type;              /* This Byte indicates the media descriptor type. Typically 0xF8(any hard drive) or 0xF0(3 1/2 floppy, 1.44MB) */
	unsigned short		table_size_16;           /* Number of sectors per FAT. FAT12/FAT16 only. Must be zero for FAT32. Cant be zero for FAT12/16 */
	unsigned short		sectors_per_track;       /* Sector per track/head.  Is the number of sectors grouped under one head. Not used in LBA partitions. */
	unsigned short		head_side_count;         /* Number of heads or sides on the storage media. */ 
	unsigned int 		hidden_sector_count;     /* Number of hidden sectors. (i.e. the LBA of the beginning of the partition.) */
	unsigned int 		total_sectors_32;        /* Large amount of sector on media. This field is set if there are more than 65535 sectors in the volume, in which case total_sectors_16 is zero */
 
	/* this will be cast to it's specific type once the driver actually knows what type of FAT this is. */
	unsigned char		extended_section[54];
 
}__attribute__((packed)) fat_BS_t;

int fat_read_bootsector __P((fat_BS_t *bs, kos_blockdev_t *bd));

#ifdef FAT2FS_DEBUG
void fat_print_bootsector __P((const fat_BS_t *bs));
#endif

__END_DECLS

#endif /* _FAT_BOOT_SECTOR */
