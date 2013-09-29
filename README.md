													LibFat2FS
												for Sega Dreamcast
					   
This is library is an add on that can be used with KallistiOS to support FAT16 and FAT32 file systems on SD cards.
Of course you will need an SD adaptor to use a SD card for the Sega Dreamcast. This compiles with the latest KallistiOS
(as of 2013-09-13). Also, this library is in no way affiliated with or endorsed by the people behind KallistiOS because of 
copyright(Microsoft) complications. I decided to go out on a limb and create the library anyway because I wanted to learn 
the in's and out's of the FAT16 & FAT32 file systems and benefit software engineers who program for the Sega Dreamcast in 
the process.

NOTE: A good portion(especially in fs_fat.c) of the code is used (as a skeleton) from libkosext2fs. libkosext2fs was written 
by Lawrence Sebald (BlueCrab). So kudos to him. Giving him credit does not mean he was affiliated with or endorsed this library.

#   This library is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy(COPYRIGHT.txt) of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Listed below are the File system functions that are supported and examples.

================================= --- Open --- ===================================

Opens a file or folder.

int fd = open("/sd/hello.c", O_RDWR | O_CREAT); /* Returns a file descriptor */

================================= --- Close --- ===================================

Closes a file or folder

close(fd);   /* Closes file/folder associated with file descriptor */

================================= --- Read --- ===================================

Read from a file

read(fd, buf, 512);   /* Read 512 bytes from file associated with fd into buf */

================================= --- Write --- ===================================

Write to a file

write(fd, text, strlen(text));        /* Write strlen(text) number of bytes of the string stored 
										 in text(char *) to file associated with fd */

================================= --- Seek --- ===================================

Repositions the offset of the open file

lseek(fd, 9, SEEK_SET); /* Sets byte offset to 9 bytes from the beginning of the file */

================================= --- Tell --- ===================================

Tells the current byte offset of the open file.

fs_tell(fd); or lseek(fd, 0, SEEK_CUR); /* Both return the same value
                                          (current byte offset of the open file) */

================================= --- Total --- ===================================

Returns the size of the file.

fs_total(fd); /* Return the size of the file in bytes */	

================================= --- ReadDir --- =================================

Returns a pointer to a dirent structure representing the next directory entry in the directory stream

DIR *d = opendir("/sd");

dirent *entry = readdir(d);

/* See readdir example */

================================= --- Unlink --- ===================================

Deletes a file

unlink("/sd/deleteme.txt");  /* Deletes "deleteme.txt" from "sd" directory */

================================= --- MkDir --- ===================================

Makes a directory

mkdir("/sd/hello", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); /* Create a folder with read/write/search permissions for owner and group, and with read/search permissions for others */

================================= --- RmDir --- ===================================

Deletes a empty directory

rmdir("/sd/folder1");  /* Deletes "folder1" folder from "sd" directory */

================================= --- Fcntl --- ===================================

Get the file access mode and the file status flags of an opened file.

fcntl(fd, F_GETFL, ... /* arg */ ); /* Only command supported right now. Returns the file access mode and the file status flags of a file/folder associated with fd */


