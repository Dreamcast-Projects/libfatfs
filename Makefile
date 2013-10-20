# libfatfs Makefile
#
#  Add -DFATFS_DEBUG to KOS_CFLAGS to enable debug output
#

TARGET = libfatfs.a
OBJS = boot_sector.o fatfs.o dir_entry.o fs_fat.o utils.o 

KOS_CFLAGS += -W -pedantic -Werror -Wno-pointer-sign -Wno-sign-compare -std=c99  # -DFATFS_DEBUG 

all: create_kos_link defaultall

# creates the kos link to the headers
create_kos_link:
	rm -f ../include/fatfs
	ln -s ../libfatfs/include ../include/fatfs

include $(KOS_BASE)/addons/Makefile.prefab

