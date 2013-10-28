
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <kos/fs.h>
#include <kos/mutex.h>

#include "include/fs_fat.h"

#include "fatfs.h"
#include "utils.h"
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

/* Global list of mounted FAT16/FAT32 partitions */
static struct fat_list fat_fses;

/* Mutex for file handles */
static mutex_t fat_mutex;

/* File handles */
static struct {
    int           used;       /* 0 - Not Used, 1 - Used */
    int           mode;       /* O_RDONLY, O_WRONLY, O_RDWR, O_TRUNC, O_DIR, etc */
    uint32        ptr;        /* Current read position in bytes */
    dirent_t      dirent;     /* A static dirent to pass back to clients */
    node_entry_t  *node;	  /* Pointer to node */
    node_entry_t  *dir;       /* Used by opendir */
    fs_fat_fs_t   *mnt;       /* Which mount instance are we using? */
} fh[MAX_FAT_FILES];

/* Open a file or directory */
static void *fs_fat_open(vfs_handler_t *vfs, const char *fn, int mode) {
    file_t fd;
    fs_fat_fs_t *mnt = (fs_fat_fs_t *)vfs->privdata;
	
	char *ufn = NULL; 
    node_entry_t *found = NULL;

    /* Make sure if we're going to be writing to the file that the fs is mounted
       read/write. */
    if((mode & (O_TRUNC | O_WRONLY | O_RDWR)) &&
       !(mnt->mount_flags & FS_FAT_MOUNT_READWRITE)) {
        errno = EROFS;
        return NULL;
    }
	
	/* Make sure to add the root directory to the fn */
	ufn = malloc(strlen(fn)+strlen(mnt->fs->mount)+1); 
	memset(ufn, 0, strlen(fn)+strlen(mnt->fs->mount)+1);
    strcat(ufn, mnt->fs->mount);
    strcat(ufn, fn);

	found = fat_search_by_path(mnt->fs, ufn);

    /* Handle a few errors */
    if(found == NULL && !(mode & O_CREAT)) {
        errno = ENOENT;
		free(ufn);
        return NULL;
    }
    else if(found != NULL && (mode & O_CREAT) && (mode & O_EXCL)) {
        errno = EEXIST;
		delete_struct_entry(found);
		free(ufn);
        return NULL;
    }
    else if(found == NULL && (mode & O_CREAT)) {
		found = create_entry(mnt->fs, ufn, ARCHIVE);
		
        if(found == NULL)
		{
			free(ufn);
            return NULL;
		}
    }
    else if(found != NULL && (found->Attr & READ_ONLY) && ((mode & O_WRONLY) || (mode & O_RDWR))) {
		errno = EROFS;
		delete_struct_entry(found);
		free(ufn);
		return NULL;
    }
    
    /* Set filesize to 0 if we set mode to O_TRUNC */
    if((mode & O_TRUNC) && ((mode & O_WRONLY) || (mode & O_RDWR)))
    {
        found->FileSize = 0;
        delete_cluster_list(mnt->fs, found);
		update_sd_entry(mnt->fs, found);
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
		free(ufn);
		delete_struct_entry(found);
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Make sure we're not trying to open a directory for writing */
    if((found->Attr & DIRECTORY) && (mode & (O_WRONLY | O_RDWR))) {
        errno = EISDIR;
		free(ufn);
		delete_struct_entry(found);
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Make sure if we're trying to open a directory that we have a directory */
    if((mode & O_DIR) && !(found->Attr & DIRECTORY)) {
        errno = ENOTDIR;
		free(ufn);
		delete_struct_entry(found);
        mutex_unlock(&fat_mutex);
        return NULL;
    }

    /* Fill in the rest of the handle */
    fh[fd].mode = mode;
    fh[fd].ptr = 0;
    fh[fd].mnt = mnt;
    fh[fd].node = found;
	fh[fd].node->CurrCluster = fh[fd].node->StartCluster;
	fh[fd].node->NumCluster = 0;
    fh[fd].dir = NULL;

    mutex_unlock(&fat_mutex);
	
	free(ufn);

    return (void *)(fd + 1);
}

static int fs_fat_close(void * h) {
    file_t fd = ((file_t)h) - 1;
	
    mutex_lock(&fat_mutex);

    if(fd < MAX_FAT_FILES && fh[fd].used) {
        fh[fd].used = 0;
        fh[fd].ptr = 0;
		fh[fd].mode = 0;
		
		delete_struct_entry(fh[fd].node);
		fh[fd].node = NULL;
		delete_struct_entry(fh[fd].dir);
		fh[fd].dir = NULL;
    }

    mutex_unlock(&fat_mutex);

    return 0;
}

static ssize_t fs_fat_read(void *h, void *buf, size_t cnt) {
    file_t fd = ((file_t)h) - 1;
    fatfs_t *fs;
    unsigned char *bbuf = (unsigned char *)buf;
    ssize_t rv;

    mutex_lock(&fat_mutex);

    /* Check that the fd is valid */
    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_WRONLY)) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    /* Check and make sure it is not a directory */
    if(fh[fd].mode & O_DIR) {
        mutex_unlock(&fat_mutex);
        errno = EISDIR;
        return -1;
    }

    /* Do we have enough left? */
    if((fh[fd].ptr + cnt) > fh[fd].node->FileSize)
    {
        cnt = fh[fd].node->FileSize - fh[fd].ptr;
    }

    fs = fh[fd].mnt->fs;
    rv = (ssize_t)cnt;
	
    if((fat_read_data(fs, fh[fd].node, &bbuf, (int)cnt, fh[fd].ptr)) != 0) { 
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }
	
    bbuf[cnt] = '\0';
    fh[fd].ptr += cnt;

    /* We're done, clean up and return. */
    mutex_unlock(&fat_mutex);

    return rv;
}

static ssize_t fs_fat_write(void *h, const void *buf, size_t cnt)
{
    file_t fd = ((file_t)h) - 1;
    fatfs_t *fs;
    ssize_t rv;
	
	unsigned int last_end;

    mutex_lock(&fat_mutex);

    /* Check that the fd is valid */
    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_DIR) || (fh[fd].mode & O_RDONLY)) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }
    
    /* If we set mode to O_APPEND, then make sure we write to end of file */
    if(fh[fd].mode & O_APPEND)
    {
        fh[fd].ptr = fh[fd].node->FileSize;
    }
	
	/* If we want to write more than we have, set it to write the whole thing */
	if(cnt > strlen((unsigned char*)buf))
	{
		cnt = strlen((unsigned char*)buf);
	}
	
	fs = fh[fd].mnt->fs;
    rv = (ssize_t)cnt;
	last_end = fh[fd].node->EndCluster; /* Used later to determine if a cluster was allocated for this file */
	
    if(fat_write_data(fs, fh[fd].node, (unsigned char*)buf, cnt, fh[fd].ptr) != 0) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return -1;
    }

    fh[fd].ptr += cnt;
	
	/* Write it to FSInfo sector(Fat32 only) */
	if(fs->fat_type == FAT32 && (fh[fd].node->EndCluster != last_end))
		set_fsinfo_nextfree(fs); 

    fh[fd].node->FileSize = (fh[fd].ptr > fh[fd].node->FileSize) ? fh[fd].ptr : fh[fd].node->FileSize; /* Increase the file size if need be(which ever is bigger) */
				
    /* Write it to the FAT */
    update_sd_entry(fs, fh[fd].node);

    mutex_unlock(&fat_mutex);

    return rv;
}

static _off64_t fs_fat_seek64(void *h, _off64_t offset, int whence) {
    file_t fd = ((file_t)h) - 1;
    _off64_t rv;
	
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
            fh[fd].ptr = fh[fd].node->FileSize;// + offset;
            break;

        default:
            mutex_unlock(&fat_mutex);
	    errno = EINVAL;
            return -1;
    }

    rv =  (_off64_t)fh[fd].ptr;
    mutex_unlock(&fat_mutex);
	
    return rv;
}

static _off64_t fs_fat_tell64(void *h) {
    file_t fd = ((file_t)h) - 1;
    _off64_t rv;
	
    mutex_lock(&fat_mutex);

    if(fd >= MAX_FAT_FILES || !fh[fd].used || (fh[fd].mode & O_DIR)) {
        mutex_unlock(&fat_mutex);
        errno = EINVAL;
        return -1;
    }

    rv = (_off64_t)fh[fd].ptr;

    mutex_unlock(&fat_mutex);
	
    return rv;
}

static uint64 fs_fat_total64(void *h) {
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

    mutex_lock(&fat_mutex);

    /* Check that the fd is valid */
    if(fd >= MAX_FAT_FILES || !fh[fd].used || !(fh[fd].mode & O_DIR)) {
        mutex_unlock(&fat_mutex);
        errno = EBADF;
        return NULL;
    }

    /* Get the first child of this folder if NULL */
    if(fh[fd].dir == NULL) 
    {
		fh[fd].dir = get_next_entry(fh[fd].mnt->fs, fh[fd].node, NULL);
    } 
    /* Move on to the next child */
    else  
    {
		fh[fd].dir = get_next_entry(fh[fd].mnt->fs, fh[fd].node, fh[fd].dir);
    }
	
    /* Make sure we're not at the end of the directory */
    if(fh[fd].dir == NULL) {
        mutex_unlock(&fat_mutex);
        return NULL;
    }
	
    /* Fill in the static directory entry */
    fh[fd].dirent.size = fh[fd].dir->FileSize;
    memcpy(fh[fd].dirent.name, fh[fd].dir->Name, strlen(fh[fd].dir->Name));
    fh[fd].dirent.name[strlen(fh[fd].dir->Name)] = '\0';
    fh[fd].dirent.attr = fh[fd].dir->Attr;
    fh[fd].dirent.time = 0; 

    mutex_unlock(&fat_mutex);

    return &fh[fd].dirent;
}

static int fs_fat_rename(vfs_handler_t *vfs, const char *fn1, const char *fn2) {
	int i;
    fs_fat_fs_t *mnt = (fs_fat_fs_t *)vfs->privdata;
    node_entry_t *found = NULL;
	char *cpy;
	
	unsigned char attr;
	unsigned int start_cluster;
	unsigned int filesize;

    /* Make sure we get valid filenames. */
    if(!fn1 || !fn2) {
        errno = ENOENT;
        return -1;
    }
	
	/* Make a copy of old and cat mount to it */
	cpy = malloc(strlen(fn1) + strlen(mnt->fs->mount) + 1);
	memset(cpy, 0, strlen(fn1) + strlen(mnt->fs->mount) + 1);
	strcat(cpy, mnt->fs->mount);
	strcat(cpy, fn1);
	
    /* No, you cannot move the root directory. */
    if(strcasecmp(cpy, mnt->fs->mount) == 0) {
        errno = EBUSY;
        return -1;
    }

    /* Make sure the fs is writable */
    if(!(mnt->mount_flags & FS_FAT_MOUNT_READWRITE)) {
        errno = EROFS;
        return -1;
    }
	
	found = fat_search_by_path(mnt->fs, cpy);
	
	free(cpy);
		
	mutex_lock(&fat_mutex);
	
	if(found) {
        /* Make sure it's not in use */
		for(i=0;i<MAX_FAT_FILES; i++)
		{
			if(fh[i].used == 1 && fh[i].node == found)
			{
				errno = EBUSY;
				delete_struct_entry(found);
				mutex_unlock(&fat_mutex);
				return -1;
			}
		}
		
		/* Make sure its not Read Only */
		if(found->Attr & READ_ONLY)
		{
			errno = EROFS;
			delete_struct_entry(found);
			mutex_unlock(&fat_mutex);
			return -1;
		}
       
	    attr = found->Attr;
		start_cluster = found->StartCluster;
		filesize = found->FileSize;
	   
		/* Remove it from SD card. Doesnt remove allocated clusters */
		delete_sd_entry(mnt->fs, found);

		/* Free up struct entry */
		delete_struct_entry(found);
    }
	else /* Not found */
	{
		errno = ENOENT;
		mutex_unlock(&fat_mutex);
		return -1;
	}
	
	/* Make a copy of new and cat mount to it */
	cpy = malloc(strlen(fn2) + strlen(mnt->fs->mount) + 1);
	memset(cpy, 0, strlen(fn2) + strlen(mnt->fs->mount) + 1);
	strcat(cpy, mnt->fs->mount);
	strcat(cpy, fn2);
	
	/* See if new filename already exists */
	found = fat_search_by_path(mnt->fs, cpy);
	
	if(found)
	{
		free(cpy);
		
		if(found->Attr & DIRECTORY)
		{
			/* Make sure directory is empty besides "." and ".." */
			if(get_next_entry(mnt->fs, found, NULL) != NULL)
			{
				errno = ENOTEMPTY;
				delete_struct_entry(found);
				mutex_unlock(&fat_mutex);
				return -1;
			}
			/* If this is a directory then old one must be too */
			else if(!(attr & DIRECTORY))
			{
				errno = EISDIR;
				delete_struct_entry(found);
				mutex_unlock(&fat_mutex);
				return -1;
			}
			
			/* So it is an empty directory and old name was a directory too. Work your magic */
			delete_cluster_list(mnt->fs, found);
			found->StartCluster = start_cluster;
			update_sd_entry(mnt->fs, found);
			
			delete_struct_entry(found);
			
			mutex_unlock(&fat_mutex);
			return 0;
		}
		else 
		{
			if(attr != found->Attr)
			{
				errno = EISDIR;
				delete_struct_entry(found);
				mutex_unlock(&fat_mutex);
				return -1;
			}
			
			/* Its a file that already exists. Delete its clusters and point it to the old clusters */
			delete_cluster_list(mnt->fs, found);
			found->StartCluster = start_cluster;
			found->FileSize = filesize;
			update_sd_entry(mnt->fs, found);
			
			delete_struct_entry(found);
			
			mutex_unlock(&fat_mutex);
			return 0;
		}
	}
	
	if((found = create_entry(mnt->fs, cpy, attr)) == NULL)
	{
		free(cpy);
		mutex_unlock(&fat_mutex);
		return -1;
	}
	
	if(attr & DIRECTORY)
	{
		delete_cluster_list(mnt->fs, found);
		found->StartCluster = start_cluster;
		update_sd_entry(mnt->fs, found);
	}
	else
	{
		found->StartCluster = start_cluster;
		found->FileSize = filesize;
		update_sd_entry(mnt->fs, found);
	}
	
	free(cpy);
	delete_struct_entry(found);
    mutex_unlock(&fat_mutex);
	
    return 0;
}

static int fs_fat_unlink(vfs_handler_t * vfs, const char *fn) {

	int i;
	node_entry_t *f = NULL;
	fs_fat_fs_t *mnt = (fs_fat_fs_t *)vfs->privdata;
	char *ufn = NULL;

	ufn = malloc(strlen(fn)+strlen(mnt->fs->mount)+1); 
	memset(ufn, 0, strlen(fn)+strlen(mnt->fs->mount)+1);    
    strcat(ufn, mnt->fs->mount);
    strcat(ufn, fn);

    mutex_lock(&fat_mutex);
	
	f = fat_search_by_path(mnt->fs, ufn);
	
	free(ufn);

    if(f) {
        /* Make sure it's not in use */
		for(i=0;i<MAX_FAT_FILES; i++)
		{
			if(fh[i].used == 1 && fh[i].node == f)
			{
				errno = EBUSY;
				delete_struct_entry(f);
				mutex_unlock(&fat_mutex);
				return -1;
			}
		}
		
		/* Make sure it isnt a directory(files only) */
		if(f->Attr & DIRECTORY)
		{
			errno = EISDIR;
			delete_struct_entry(f);
			mutex_unlock(&fat_mutex);
			return -1;
		}
		
		/* Make sure its not Read Only */
		if(f->Attr & READ_ONLY)
		{
			errno = EROFS;
			delete_struct_entry(f);
			mutex_unlock(&fat_mutex);
			return -1;
		}
       
		/* Remove it from SD card */
		delete_sd_entry(mnt->fs, f);
		
		/* Free Data Clusters in FAT table */
		delete_cluster_list(mnt->fs, f);

		/* Free node */
		delete_struct_entry(f);
    }
	else /* Not found */
	{
		errno = ENOENT;
		mutex_unlock(&fat_mutex);
		return -1;
	}

    mutex_unlock(&fat_mutex);
    
	return 0;
}

static int fs_fat_mkdir(vfs_handler_t *vfs, const char *fn)
{
    char *ufn = NULL;
    fs_fat_fs_t *mnt = (fs_fat_fs_t *)vfs->privdata;
    node_entry_t *found = NULL;

	ufn = malloc(strlen(fn)+strlen(mnt->fs->mount)+1); 
    memset(ufn, 0, strlen(fn)+strlen(mnt->fs->mount)+1);    
    strcat(ufn, mnt->fs->mount);
    strcat(ufn, fn);

    /* Make sure there is a filename given */
    if(!fn) {
        errno = ENOENT;
		free(ufn);
        return -1;
    }

    /* Make sure the fs is writable */
    if(!(mnt->mount_flags & FS_FAT_MOUNT_READWRITE)) {
        errno = EROFS;
		free(ufn);
        return -1;
    }

	found = fat_search_by_path(mnt->fs, ufn);

    /* Handle a few errors */
    if(found != NULL) {
        errno = EEXIST;  
		delete_struct_entry(found);
		free(ufn);
        return -1;
    }

	found = create_entry(mnt->fs, ufn, DIRECTORY);
 
    if(found == NULL)
	{
		errno = ENOSPC;
		free(ufn);
		return -1;
	}
		
	free(ufn);
	delete_struct_entry(found);

    return 0;
}

static int fs_fat_rmdir(vfs_handler_t *vfs, const char *fn)
{
	int i;
	node_entry_t *f = NULL;
	char *ufn = NULL;
	fs_fat_fs_t *mnt = (fs_fat_fs_t *)vfs->privdata;
	
	ufn = malloc(strlen(fn)+strlen(mnt->fs->mount)+1); 
	memset(ufn, 0, strlen(fn)+strlen(mnt->fs->mount)+1);    
    strcat(ufn, mnt->fs->mount);
    strcat(ufn, fn);

    mutex_lock(&fat_mutex);

	f = fat_search_by_path(mnt->fs, ufn);
	
	free(ufn);

    if(f) {
        /* Make sure it's not in use */
		for(i=0;i<MAX_FAT_FILES; i++)
		{
			if(fh[i].used == 1 && fh[i].node == f)
			{
				errno = EBUSY;
				delete_struct_entry(f);
				mutex_unlock(&fat_mutex);
				return -1;
			}
		}
		
		/* Make sure it isnt a file */
		if(f->Attr & ARCHIVE)
		{
			errno = ENOTDIR;
			delete_struct_entry(f);
			mutex_unlock(&fat_mutex);
			return -1;
		}
		
		/* Make sure its not Read Only */
		if(f->Attr & READ_ONLY)
		{
			errno = EROFS;
			delete_struct_entry(f);
			mutex_unlock(&fat_mutex);
			return -1;
		}
		
	   if(get_next_entry(mnt->fs, f, NULL) != NULL)
	   {
			errno = ENOTEMPTY;
			delete_struct_entry(f);
			mutex_unlock(&fat_mutex);
			return -1;
	   }
	   
		/* Remove it from SD card */
		delete_sd_entry(mnt->fs, f);
		
		/* Free Data Clusters in FAT table */
		delete_cluster_list(mnt->fs, f);

		/* Free node */
		delete_struct_entry(f);
    }
	else /* Not found */
	{
		errno = ENOENT;
		mutex_unlock(&fat_mutex);
		return -1;
	}

    mutex_unlock(&fat_mutex);
	
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
    NULL,             		   /* seek */
    NULL,              		   /* tell */
    NULL,            		   /* total */
    fs_fat_readdir,            /* readdir */
    NULL,                      /* ioctl */
    fs_fat_rename,             /* rename */
    fs_fat_unlink,             /* unlink */
    NULL,                      /* mmap */
    NULL,                      /* complete */
    NULL,                      /* stat */
    fs_fat_mkdir,              /* mkdir */
    fs_fat_rmdir,              /* rmdir */
    fs_fat_fcntl,              /* fcntl */
    NULL,                      /* poll */
    NULL,                      /* link */
    NULL,                      /* symlink */
    fs_fat_seek64,             /* seek64 */
    fs_fat_tell64,             /* tell64 */
    fs_fat_total64,            /* total64 */
    NULL                       /* readlink */
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
        printf("fs_fat: device does not contain a valid fatfs.\n");
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
	int j;
    int found = 0, rv = 0;

    /* Find the fs in question */
    mutex_lock(&fat_mutex);

    LIST_FOREACH(i, &fat_fses, entry) {
        if(!strcasecmp(mp, i->vfsh->nmmgr.pathname)) {
            found = 1;
            break;
        }
    }

    if(found) {
		
		free(i->fs->mount); /* Free str mem */
		
		/* Handle dealloc all the open files */
		for(j=0;j<MAX_FAT_FILES; j++)
		{
			if(fh[j].used == 1)
			{
				fh[j].used = 0;
				fh[j].ptr = 0;
				fh[j].mode = 0;

				delete_struct_entry(fh[j].node);
				fh[j].node = NULL;
				delete_struct_entry(fh[j].dir);
				fh[j].dir = NULL;
			}
		}
		
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

int fat_partition(uint8 partition_type)
{
	if(partition_type == FAT16TYPE1
	|| partition_type == FAT16TYPE2
	|| partition_type == FAT32TYPE1
	|| partition_type == FAT32TYPE2 
	)
		return 1;
		
	return 0;
}
