/* KallistiOS ##version##

   mkdir.c
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
	
	int fd;
	unsigned char buf[1024]; // Each sector is 512 bytes long

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

    /* Check to see if the MBR says that we have a FAT partition. */
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
	
	const char * code = "\n\n#include <kos.h>\n\nextern uint8 romdisk[];\n\nKOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);\n\nKOS_INIT_ROMDISK(romdisk);\n\nint main(int argc, char **argv) {\n\tprintf(\"\\nHello world!\\n\\n\");\n\treturn 0;\n}";
	
	/* Create a folder with read/write/search permissions for owner and group, and with read/search permissions for others */
	mkdir("/sd/hello", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	fd = open("/sd/hello/hello.c", O_RDWR | O_TRUNC | O_CREAT);
	
	printf("\n\nWrite code to source file\n");
	
	write(fd, code, strlen(code)); 
	
	lseek(fd, 0, SEEK_SET);
	
	read(fd, buf, 512);
	
	printf("hello.c Contents: %s\n\n", buf);

    close(fd);
	
    /* Clean up the filesystem and everything else */
    fs_fat_unmount("/sd");
    fs_fat_shutdown();
    sd_shutdown();

    return 0;
}


