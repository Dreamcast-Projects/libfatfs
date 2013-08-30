

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <kos/fs.h>
#include <kos/mutex.h>

#include "include/fs_fat.h"

#include "fat_defs.h"
#include "dir_entry.h"

#define MAX_FAT_FILES 16

typedef struct fs_fat_fs {
    LIST_ENTRY(fs_fat_fs) entry;

    vfs_handler_t *vfsh;
    fatfs_t *fs;
    uint32_t mount_flags;
} fs_fat_fs_t;

LIST_HEAD(fat_list, fs_fat_fs);

/* Global list of mounted FAT16 partitions */
static struct fat_list fat_fses;

/* Mutex for file handles */
static mutex_t fat_mutex;

/* File handles */
static struct {
    int           used;       /* 0 - Not Used, 1 - Used */
    int           mode;       /* O_RDONLY, O_WRONLY, O_RDWR, O_TRUNC, O_DIR, etc */
    uint32        ptr;        /* Current read position in bytes */
    dirent_t      dirent;     /* A static dirent to pass back to clients */
    node_entry_t  *node;
    node_entry_t  *dir;       /* Used by opendir */
    fs_fat_fs_t   *mnt;       /* Which mount instance are we using? */
} fh[MAX_FAT_FILES];

/* Open a file or directory */
static void *fs_fat_open(vfs_handler_t *vfs, const char *fn, int mode) {
    file_t fd;
    char *ufn = (char *)malloc(strlen(fn)+4); // 4:  3 for "/sd" and 1 for null character
    fs_fat_fs_t *mnt = (fs_fat_fs_t *)vfs->privdata;
    node_entry_t *found = NULL;

    memset(ufn, 0, strlen(fn)+4);    // 4:  3 for "/sd" and 1 for null character

    strcat(ufn, "/sd");
    strcat(ufn, fn);

    /* Find the object in question */
    found = fat_search_by_path(mnt->fs->root, ufn);

    /* Handle a few errors */
    if(found == NULL && !(mode & O_CREAT)) {
        errno = ENOENT;
        return NULL;
    }
    else if(found != NULL && (mode & O_CREAT) && (mode & O_EXCL)) {
        errno = EEXIST;
        return NULL;
    }
    else if(found == NULL && (mode & O_CREAT)) {
         found = create_file(mnt->fs, mnt->fs->root, ufn);
         
         if(found == NULL)
             return NULL;
    }
    
    /* Set filesize to 0 if we set mode to O_TRUNC */
    if((mode & O_TRUNC) && ((mode & O_WRONLY) || (mode & O_RDWR)))
    {
        found->FileSize = 0;
        // Also change FAT table. I.E. delete all clusters associated with this file.
    }

    /* Find a free file handle */
    mutex_lock(&fat_mutex);

    for(fd = 0; fd < MAX_FAT_FILES; ++fd) {
        if(fh[fd].used == 0) {
            fh[fd].used = 1;
            break;
        }
    }

    if(fd >= MAX_FAT_FILES) {
        errno = ENFILE;
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Make sure we're not trying to open a directory for writing */
    if((found->Attr & DIRECTORY) && (mode & (O_WRONLY | O_RDWR))) {
        errno = EISDIR;
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Make sure if we're trying to open a directory that we have a directory */
    if((mode & O_DIR) && !(found->Attr & DIRECTORY)) {
        errno = ENOTDIR;
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Fill in the rest of the handle */
    fh[fd].mode = mode;
    fh[fd].ptr = 0;
    fh[fd].mnt = mnt;
    fh[fd].node = found;
    fh[fd].dir = NULL;

    mutex_unlock(&fat_mutex);

    return (void *)(fd + 1);
}

static void fs_fat_close(void * h) {
    file_t fd = ((file_t)h) - 1;

    mutex_lock(&fat_mutex);

    if(fd < MAX_FAT_FILES && fh[fd].mode) {
        fh[fd].used = 0;
        fh[fd].mode = 0;
        fh[fd].ptr = 0;

        // If it was open for writing make sure to update entry on SD card
        // Change file size
        // Change time
        if(fh[fd].mode & O_WRONLY || fh[fd].mode & O_RDWR)
            update_fat_entry(fh[fd].mnt->fs, fh[fd].node);
    }

    mutex_unlock(&fat_mutex);
}

static ssize_t fs_fat_read(void *h, void *buf, size_t cnt) {
    file_t fd = ((file_t)h) - 1;
    fatfs_t *fs;
    uint32_t bytes_per_sector;
    uint8_t *block;
    uint8_t *bbuf = (uint8_t *)buf;
    ssize_t rv;

    mutex_lock(&fat_mutex);

    // Check that the fd is valid 
    if(fd >= MAX_FAT_FILES || !fh[fd].used) {
        mutex_unlock(&fat_mutex);
        errno = EINVAL;
        return -1;
    }

    // Check and make sure it is not a directory
    if(fh[fd].mode & O_DIR) {
        mutex_unlock(&fat_mutex);
        errno = EISDIR;
        return -1;
    }
    
    // Check and make sure it is opened for reading
    if(fh[fd].mode & O_WRONLY) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    // Do we have enough left? 
    if((fh[fd].ptr + cnt) > fh[fd].node->FileSize)
    {
        cnt = fh[fd].node->FileSize - fh[fd].ptr;
    }

    // Make sure we clean out the string that we are going to return 
    memset(bbuf, 0, sizeof(bbuf));

    fs = fh[fd].mnt->fs;
    rv = (ssize_t)cnt;
	
    if(!(block = fat_read_data(fs, fh[fd].node, (int)cnt, fh[fd].ptr))) { 
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    memcpy(bbuf, block, cnt);
    bbuf[cnt] = '\0';
    fh[fd].ptr += cnt;

    /* We're done, clean up and return. */
    mutex_unlock(&fat_mutex);
    
    free(block);

    return rv;
}

static ssize_t fs_fat_write(void *h, void *buf, size_t cnt)
{
    file_t fd = ((file_t)h) - 1;
    fatfs_t *fs;
    uint8_t *bbuf = (uint8_t *)malloc(sizeof(uint8_t)*(cnt+1)); // 
    ssize_t rv;

    mutex_lock(&fat_mutex);

    // Check that the fd is valid 
    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_DIR)) {
        mutex_unlock(&fat_mutex);
        errno = EINVAL;
        return -1;
    }
    
    // Check and make sure it is opened for Writing
    if(fh[fd].mode & O_RDONLY) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }
    
    fs = fh[fd].mnt->fs;
    rv = (ssize_t)cnt;

    // Copy the bytes we want to write
    strncpy(bbuf, buf, cnt);
    bbuf[cnt] = '\0';
    
    // If we set mode to O_APPEND, then make sure we write to end of file
    if(fh[fd].mode & O_APPEND)
    {
        fh[fd].ptr = fh[fd].node->FileSize;
    }
	
    if(!fat_write_data(fs, fh[fd].node, bbuf, cnt, fh[fd].ptr)) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    fh[fd].ptr += cnt;
    fh[fd].node->FileSize = (fh[fd].ptr > fh[fd].node->FileSize) ? fh[fd].ptr : fh[fd].node->FileSize; // Increase the file size if need be

    // Write it to the FAT
    //update_fat_entry(fs, fh[fd].node);

    mutex_unlock(&fat_mutex);

    return rv;
}

static off_t fs_fat_seek(void *h, off_t offset, int whence) {
    file_t fd = ((file_t)h) - 1;
    off_t rv;

    mutex_lock(&fat_mutex);

    /* Check that the fd is valid */
    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_DIR)) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    /* Update current position according to arguments */
    switch(whence) {
        case SEEK_SET:
            fh[fd].ptr = offset;
            break;

        case SEEK_CUR:
            fh[fd].ptr += offset;
            break;

        case SEEK_END:
            fh[fd].ptr = fh[fd].node->FileSize + offset;
            break;

        default:
            mutex_unlock(&fat_mutex);
			errno = EINVAL;
            return -1;
    }

    /* Check bounds */ 
    if(fh[fd].ptr > fh[fd].node->FileSize) 
        fh[fd].ptr = fh[fd].node->FileSize;

    rv = (off_t)fh[fd].ptr;

    mutex_unlock(&fat_mutex);
	
    return rv;
}

static off_t fs_fat_tell(void *h) {
    file_t fd = ((file_t)h) - 1;
    off_t rv;

	printf("Tell Function Called\n");
    
    mutex_lock(&fat_mutex);

    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_DIR)) {
        mutex_unlock(&fat_mutex);
        errno = EINVAL;
        return -1;
    }

    rv = (off_t)fh[fd].ptr;

    mutex_unlock(&fat_mutex);
	
    return rv;
}

static size_t fs_fat_total(void *h) {
    file_t fd = ((file_t)h) - 1;
    size_t rv;
    
    mutex_lock(&fat_mutex);

    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_DIR)) {
        mutex_unlock(&fat_mutex);
        errno = EINVAL;
        return -1;
    }

    rv = fh[fd].node->FileSize;
    mutex_unlock(&fat_mutex);
	
    return rv;
}

static dirent_t *fs_fat_readdir(void *h) {
    file_t fd = ((file_t)h) - 1;
    int err;

    mutex_lock(&fat_mutex);

    /* Check that the fd is valid */
    if(fd >= MAX_FAT_FILES || !fh[fd].used || !(fh[fd].mode & O_DIR)) {
	//printf("This file descriptor is NOT valid. Exited ReadDir\n");
        mutex_unlock(&fat_mutex);
        errno = EINVAL;
        return NULL;
    }

    /* Get the children of this folder if NULL */
    if(fh[fd].dir == NULL) 
    {
        fh[fd].dir = fh[fd].node->Children;
    } 
    /* Move on to the next child */
    else  
    {
        fh[fd].dir = fh[fd].dir->Next;
    }
	
    /* Make sure we're not at the end of the directory */
    if(fh[fd].dir == NULL) {
	//printf("Reached End of Directory\n");
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Fill in the static directory entry */
    fh[fd].dirent.size = fh[fd].dir->FileSize;
    memcpy(fh[fd].dirent.name, fh[fd].dir->Name, strlen(fh[fd].dir->Name));
    fh[fd].dirent.name[strlen(fh[fd].dir->Name)] = 0;
    fh[fd].dirent.attr = fh[fd].dir->Attr;
    fh[fd].dirent.time = 0; //inode->i_mtime;
   // fh[fd].ptr += dent->rec_len;

    mutex_unlock(&fat_mutex);

    return &fh[fd].dirent;
}

static int fs_fat_stat(vfs_handler_t *vfs, const char *fn, stat_t *rv) {
    /*
    fs_ext2_fs_t *fs = (fs_ext2_fs_t *)vfs->privdata;
    int irv;
    ext2_inode_t *inode;
    uint32_t used;

    if(!rv) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&fat_mutex);

    /* Find the object in question 
    if((irv = ext2_inode_by_path(fs->fs, fn, &inode, &used, 1, NULL))) {
        mutex_unlock(&fat_mutex);
        errno = -irv;
        return -1;
    }

    /* Fill in the easy parts of the structure. 
    rv->dev = vfs;
    rv->unique = used;
    rv->size = inode->i_size;
    rv->time = inode->i_mtime;
    rv->attr = 0;

    /* Parse out the ext2 mode bits 
    switch(inode->i_mode & 0xF000) {
        case EXT2_S_IFLNK:
            rv->type = STAT_TYPE_SYMLINK;
            break;

        case EXT2_S_IFREG:
            rv->type = STAT_TYPE_FILE;
            break;

        case EXT2_S_IFDIR:
            rv->type = STAT_TYPE_DIR;
            break;

        case EXT2_S_IFSOCK:
        case EXT2_S_IFIFO:
        case EXT2_S_IFBLK:
        case EXT2_S_IFCHR:
            rv->type = STAT_TYPE_PIPE;
            break;

        default:
            rv->type = STAT_TYPE_NONE;
            break;
    }

    /* Set the attribute bits based on the user permissions on the file. 
    if(inode->i_mode & EXT2_S_IRUSR)
        rv->attr |= STAT_ATTR_R;
    if(inode->i_mode & EXT2_S_IWUSR)
        rv->attr |= STAT_ATTR_W;

    ext2_inode_put(inode);
    mutex_unlock(&fat_mutex);
    */
    return 0;
}

static int fs_fat_fcntl(void *h, int cmd, va_list ap) {
    file_t fd = ((file_t)h) - 1;
    int rv = -1;

    (void)ap;

    mutex_lock(&fat_mutex);

    if(fd >= MAX_FAT_FILES || !fh[fd].used) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    switch(cmd) {
        case F_GETFL:
            rv = fh[fd].mode;
            break;

        case F_SETFL:
        case F_GETFD:
        case F_SETFD:
            rv = 0;
            break;

        default:
            errno = EINVAL;
    }

    mutex_unlock(&fat_mutex);
    return rv;
}

/* This is a template that will be used for each mount */
static vfs_handler_t vh = {
    /* Name Handler */
    {
        { 0 },                  /* name */
        0,                      /* in-kernel */
        0x00010000,             /* Version 1.0 */
        NMMGR_FLAGS_NEEDSFREE,  /* We malloc each VFS struct */
        NMMGR_TYPE_VFS,         /* VFS handler */
        NMMGR_LIST_INIT         /* list */
    },

    0, NULL,                   /* no cacheing, privdata */

    fs_fat_open,               /* open */
    fs_fat_close,              /* close */
    fs_fat_read,               /* read */
    fs_fat_write,              /* write */
    fs_fat_seek,               /* seek */
    fs_fat_tell,               /* tell */
    fs_fat_total,              /* total */
    fs_fat_readdir,            /* readdir */
    NULL,                      /* ioctl */
    NULL,                      /* rename */
    NULL, //fs_fat_unlink      /* unlink(delete a file) */
    NULL,                      /* mmap */
    NULL,                      /* complete */
    fs_fat_stat,               /* stat */
    NULL,//fs_fat_mkdir,                /* mkdir */
    NULL, //fs_fat_rmdir,               /* rmdir */
    fs_fat_fcntl,              /* fcntl */
    NULL                       /* poll */
};

static int initted = 0;

/* These two functions borrow heavily from the same functions in fs_romdisk */
int fs_fat_mount(const char *mp, kos_blockdev_t *dev, uint32_t flags) {
    fatfs_t *fs;
    fs_fat_fs_t *mnt;
    vfs_handler_t *vfsh;

    if(!initted)
        return -1;

    mutex_lock(&fat_mutex);

    /* Try to initialize the filesystem */
    if(!(fs = fat_fs_init(mp, dev))) {
        mutex_unlock(&fat_mutex);
        printf("fs_fat: device does not contain a valid fat2fs.\n");
        return -1;
    }

    /* Create a mount structure */
    if(!(mnt = (fs_fat_fs_t *)malloc(sizeof(fs_fat_fs_t)))) {
        printf("fs_fat: out of memory creating fs structure\n");
        fat_fs_shutdown(fs);
        mutex_unlock(&fat_mutex);
        return -1;
    }

    mnt->fs = fs;
    mnt->mount_flags = flags;

    /* Create a VFS structure */
    if(!(vfsh = (vfs_handler_t *)malloc(sizeof(vfs_handler_t)))) {
        printf("fs_fat: out of memory creating vfs handler\n");
        free(mnt);
        fat_fs_shutdown(fs);
        mutex_unlock(&fat_mutex);
        return -1;
    }

    memcpy(vfsh, &vh, sizeof(vfs_handler_t));
    strcpy(vfsh->nmmgr.pathname, mp);
    vfsh->privdata = mnt;
    mnt->vfsh = vfsh;

    /* Add it to our list */
    LIST_INSERT_HEAD(&fat_fses, mnt, entry);

    /* Register with the VFS */
    if(nmmgr_handler_add(&vfsh->nmmgr)) {
        printf("fs_fat: couldn't add fs to nmmgr\n");
        free(vfsh);
        free(mnt);
        fat_fs_shutdown(fs);
        mutex_unlock(&fat_mutex);
        return -1;
    }

    mutex_unlock(&fat_mutex);

    return 0;
}

int fs_fat_unmount(const char *mp) {
    fs_fat_fs_t *i;
    int found = 0, rv = 0;

    /* Find the fs in question */
    mutex_lock(&fat_mutex);

    LIST_FOREACH(i, &fat_fses, entry) {
        if(!strcmp(mp, i->vfsh->nmmgr.pathname)) {
            found = 1;
            break;
        }
    }

    if(found) {
        LIST_REMOVE(i, entry);

        /* XXXX: We should probably do something with open files... */
        nmmgr_handler_remove(&i->vfsh->nmmgr);
        free(i->vfsh);
        free(i);
    }
    else {
        errno = ENOENT;
        rv = -1;
    }

    mutex_unlock(&fat_mutex);
    return rv;
}

int fs_fat_init(void) {
    if(initted)
        return 0;

	/* Init our list of mounted entries */	
    LIST_INIT(&fat_fses);
	
	/* Reset fd's */
	memset(fh, 0, sizeof(fh));
	
	/* Init thread mutexes */
    mutex_init(&fat_mutex, MUTEX_TYPE_NORMAL);
	
    initted = 1;

    return 0;
}

int fs_fat_shutdown(void) {
    fs_fat_fs_t *i, *next;

    if(!initted)
        return 0;

    /* Clean up the mounted filesystems */
    i = LIST_FIRST(&fat_fses);
	
    while(i) {
        next = LIST_NEXT(i, entry);

        /* XXXX: We should probably do something with open files... */
        nmmgr_handler_remove(&i->vfsh->nmmgr);
        free(i->vfsh);
        free(i);

        i = next;
    }

    mutex_destroy(&fat_mutex);
    initted = 0;

    return 0;
}
