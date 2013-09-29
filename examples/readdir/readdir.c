/* KallistiOS ##version##

   sd-ext2fs.c
   Copyright (C) 2012 Lawrence Sebald

   This example shows the basics of how to interact with the SD card adapter
   driver and to make it work with fs_ext2 (in libkosext2fs).

   About all this example program does is to mount the SD card (if one is found
   and it is detected to be ext2fs) on /sd of the VFS and print a directory
   listing of the root directory of the SD card.
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include <unistd.h>

#include <dc/sd.h>
#include <kos/blockdev.h>
#include <ext2/fs_ext2.h>

#include <wchar.h>
#include <fat2fs/fs_fat.h>

int main(int argc, char *argv[]) {
    
    kos_blockdev_t sd_dev;
    uint8 partition_type;
    DIR *d;
    struct dirent *entry;

    if(sd_init()) {
        printf("Could not initialize the SD card. Please make sure that you "
               "have an SD card adapter plugged in and an SD card inserted.\n");
        exit(EXIT_FAILURE);
    }

    /* Grab the block device for the first partition on the SD card. Note that
       you must have the SD card formatted with an MBR partitioning scheme. */
    if(sd_blockdev_for_partition(0, &sd_dev, &partition_type)) {
        printf("Could not find the first partition on the SD card!\n");
        exit(EXIT_FAILURE);
    }

    /* Check to see if the MBR says that we have a FAT partition (16 or 32). */
    if(!fat_partition(partition_type)) {
        printf("MBR indicates a non-fat filesystem.\n");
    }
	
    /* Initialize fs_fat and attempt to mount the device. */
    if(fs_fat_init()) {
        printf("Could not initialize fs_fat!\n");
        exit(EXIT_FAILURE);
    }

    if(fs_fat_mount("/sd", &sd_dev, FS_FAT_MOUNT_READWRITE)) {
        printf("Could not mount SD card as fat2fs. Please make sure the card "
               "has been properly formatted.\n");
        exit(EXIT_FAILURE);
    }
	
    /* Open the root of the SD card's filesystem and list the contents. */
    if(!(d = opendir("/sd"))) {
        printf("Could not open /sd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
	printf("Contents of the root directory:\n\n");
	
    while((entry = readdir(d))) {
        printf("%s\n", entry->d_name);
    }
	
    if(closedir(d)) {
        printf("Could not close directory: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } 

    /* Clean up the filesystem and everything else */
    fs_fat_unmount("/sd");
    fs_fat_shutdown();
    sd_shutdown();

    return 0;
}


