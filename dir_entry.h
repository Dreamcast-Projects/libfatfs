

#ifndef _FAT_DIR_ENTRY_H_
#define _FAT_DIR_ENTRY_H_

__BEGIN_DECLS

struct fatfs;
typedef struct fatfs fatfs_t;

/* List of attributes the following structures may take */
#define READ_ONLY    0x01 
#define HIDDEN       0x02 
#define SYSTEM       0x04 
#define VOLUME_ID    0x08 
#define DIRECTORY    0x10 
#define ARCHIVE      0x20
#define LONGFILENAME 0x0F

/* Shortname offsets */
#define FILENAME  0x00
#define EXTENSION 0x08
#define ATTRIBUTE 0x0B
#define CREATIONTIME 0x0E
#define CREATIONDATE 0x10
#define LASTACCESSDATE 0x12
#define LASTWRITETIME 0x16
#define LASTWRITEDATE 0x18
#define STARTCLUSTER 0x1A
#define FILESIZE  0x1C

/* Long file offsets */
#define ORDER     0x00
#define FNPART1   0x01
#define CHECKSUM  0x0D
#define FNPART2   0x0E
#define FNPART3   0x1C

/* Other */
#define ENTRYSIZE 32       /* Size of an entry whether it be a lfn or just a regular file entry */
#define DELETED   0xE5     /* The first byte of a deleted entry */
#define EMPTY     0        /* Shows an empty entry */

/* FNPart1, FNPart2, and FNPart3 are unicode characters */
typedef struct fat_long_fn_dir_entry 
{
	unsigned char Order;        /* The order of this entry in the sequence of long file name entries. There can be many of these entry stacked to make a really long filename 
	                               The specification says that the last entry value(Order) will be ORed with 0x40(01000000) and it is the mark for last entry
								   0x80 is marked when LFN entry is deleted
								*/
	unsigned short FNPart1[6];  /* The first 5, 2-byte characters of this entry. */
	unsigned char Attr;         /* Should only have the value 0x0F (Specifying that it is a long name entry and not an actual file entry) */
	unsigned char Res;          /* Reserved */
	unsigned char Checksum;     /* Checksum */
	unsigned short FNPart2[6];  /* The next 6, 2-byte characters of this entry. */
	unsigned short Cluster;     /* Unused. Always 0 */
	unsigned short FNPart3[3];   /* The final 2, 2-byte characters of this entry. */
} fat_lfn_entry_t;
//__attribute__((packed)) fat_lfn_entry_t;

typedef struct fat_dir_entry 
{
    unsigned char FileName[9];  /* Represent the filename. The first character in array can hold special values. See http://www.tavi.co.uk/phobos/fat.html */
    unsigned char Ext[4];       /* Indicate the filename extension.  Note that the dot used to separate the filename and the filename extension is implied, and is not actually stored anywhere */
    unsigned char Attr;         /* Provides information about the file and its permissions. Hex numbers are bit offsets in this variable. 0x01 = 00000001
	                                                                                        0x01(0000 0001) - Read-Only
	                                                                                        0x02(0000 0002) - Hidden File
																							0x04(0000 0100) - System File 
																							0x08(0000 1000) - Contains the disk's volume label, instead of describing a file
																							0x10(0001 0000) - This is a subdirectory
																							0x20(0010 0000) - Archive flag. Set when file is modified
																							0x40(0100 0000) - Not used. Must be zero
																							0x80(1000 0000) - Not used. Must be zero
*/
    unsigned char Res;          /* Reserved */
    unsigned char CrtTimeTenth; /* Creation time in tenths of a second.  */
    unsigned short CrtTime;     /* Taken from http://www.tavi.co.uk/phobos/fat.html#file_time
	                               <------- 0x17 --------> <------- 0x16 -------->
								   15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
									h  h  h  h  h  m  m  m  m  m  m  x  x  x  x  x
								   
							       hhhhh - Indicates the binary number of hours (0-23)
                                   mmmmmm - Indicates the binary number of minutes (0-59)
                                   xxxxx - Indicates the binary number of two-second periods (0-29), representing seconds 0 to 58.
                                */								   
    unsigned short CrtDate;     /* Taken from http://www.tavi.co.uk/phobos/fat.html#file_date
							   <------- 0x19 --------> <------- 0x18 -------->
									15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
									y  y  y  y  y  y  y  m  m  m  m  d  d  d  d  d

                                        yyyyyyy - Indicates the binary year offset from 1980 (0-119), representing the years 1980 to 2099
                                        mmmm - Indicates the binary month number (1-12)
                                        ddddd - Indicates the binary day number (1-31) 
                                */	
    unsigned short LstAccDate;  /* Same format as CrtDate above */
    unsigned short FstClusHI;   /* The high 16 bits of this entry's first cluster number. For FAT 12 and FAT 16 this is always zero. */
    unsigned short WrtTime;     /* Same format as CrtTime above */
    unsigned short WrtDate;     /* Same format as CrtDate above */
    unsigned short FstClusLO;   /* The low 16 bits of this entry's first cluster number. Use this number to find the first cluster for this entry. */
    unsigned int FileSize;      /* The size of the file in bytes. This should be 0 if the file type is a folder */
} fat_dir_entry_t;
//__attribute__((packed)) fat_dir_entry_t;

/* Used to make a linked list of clusters that make up a file/folder. Used so we dont have to keep checking the FAT. */
typedef struct cluster_node {
    unsigned short Cluster_Num;
    struct cluster_node *Next;
} cluster_node_t;

/* Structure used to build an N-ary tree of files/folders. Which makes it easier on us later on. 
   http://blog.mozilla.org/nnethercote/2012/03/07/n-ary-trees-in-c/ 
   
   Files/folders in the same directory are linked together in a Singly-Linked List(*Next) 
*/
typedef struct node_entry {
	unsigned char *Name;               /* Holds name of file/folder */
	unsigned char *ShortName;          /* Holds the short name (entry). No two files can have the same short name. Can be equal to Name. */
	unsigned char Attr;                /* Holds the attributes of entry */
	unsigned int FileSize;             /* Holds the size of the file */
	unsigned int Location[2];          /* Location in FAT Table. Location[0]: Sector, Location[1]: Byte in that sector */
	cluster_node_t    *Data_Clusters;  /* A linked list to all the data clusters this file/folder uses */
 	struct node_entry *Parent;         /* The folder this file/folder belongs to */
	struct node_entry *Children;       /* Should only be NULL when this is a file or an empty folder. Only folders are allowed to have children */
	struct node_entry *Next;           /* The next file/folder in the current directory */
} node_entry_t;

/* Prototypes */
int generate_and_write_entry(fatfs_t *fat, char *filename, node_entry_t *newfile);
void delete_entry(fatfs_t *fat, node_entry_t *file);
node_entry_t *create_entry(fatfs_t *fat, node_entry_t * root, char *fn, unsigned char attr);
cluster_node_t *allocate_cluster(fatfs_t *fat, cluster_node_t  *cluster);
void delete_cluster_list(fatfs_t *fat, node_entry_t *file);
void delete_tree_entry(node_entry_t * node);
void delete_directory_tree(node_entry_t * node);
void update_fat_entry(fatfs_t *fat, node_entry_t *file);
int fat_write_data(fatfs_t *fat, node_entry_t *file, uint8_t *bbuf, int count, int ptr);
uint8_t *fat_read_data(fatfs_t *fat, node_entry_t *file, int cnt, int ptr);
node_entry_t *fat_search_by_path(node_entry_t *dir, char *fn);
void parse_directory_sector(fatfs_t *fat, node_entry_t *parent, int sector_loc, unsigned char *fat_table);


node_entry_t *isChildof(node_entry_t *children, char *child_name);

__END_DECLS
#endif /* _FAT_DIR_ENTRY_H_ */