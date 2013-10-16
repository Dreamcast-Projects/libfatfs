# libfatfs Makefile
#
#  Add -DFATFS_DEBUG to KOS_CFLAGS to enable debug output
#
#  Add -DFATFS_CACHEALL to KOS_CFLAGS to cache the whole file system to make reads and writes faster. This consumes
#  a LOT of (ram) memory IF you have many files+folders on your SD card so I do not recommend enabling unless you  
#  keep the number of files on your SD card low.
#
#  By default nothing is cached, so the amount of memory used by the library is small.
#
#

TARGET = libfatfs.a
OBJS = boot_sector.o fatfs.o dir_entry.o fs_fat.o utils.o 

KOS_CFLAGS += -W -pedantic -Werror -Wno-pointer-sign -Wno-sign-compare -std=c99 -Wno-unused-variable -Wno-unused-parameter # -DFATFS_DEBUG # -DFATFS_CACHEALL  

all: create_kos_link defaultall

# creates the kos link to the headers
create_kos_link:
	rm -f ../include/fatfs
	ln -s ../libfatfs/include ../include/fatfs

include $(KOS_BASE)/addons/Makefile.prefab

