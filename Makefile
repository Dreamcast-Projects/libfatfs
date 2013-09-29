# libfat2fs Makefile
#

TARGET = libfat2fs.a
OBJS = boot_sector.o fatfs.o dir_entry.o fs_fat.o utils.o 

KOS_CFLAGS += -W -pedantic -Werror -Wno-pointer-sign -Wno-sign-compare -std=c99 #-DFAT2FS_DEBUG

all: create_kos_link defaultall

# creates the kos link to the headers
create_kos_link:
	rm -f ../include/fat2fs
	ln -s ../libfat2fs/include ../include/fat2fs

include $(KOS_BASE)/addons/Makefile.prefab

