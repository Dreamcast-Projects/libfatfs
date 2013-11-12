

#ifndef _FAT_CACHE_H_
#define _FAT_CACHE_H_

/* Change cache size */
#define CACHESIZE 7

#include "dir_entry.h"

void init_cache();
node_entry_t *check_cache(fatfs_t *fat, const char *fn, char **ufn);
void add_to_cache(const char *filepath, node_entry_t *temp);
void delete_from_cache(node_entry_t *file);
void deinit_cache();

/* A Queue Node (Queue is implemented using Doubly Linked List) */
typedef struct QNode
{
    struct QNode *prevQ, *nextQ;
	struct QNode *nextH;
	
	unsigned char *FilePathName;
	unsigned char Attr;
	unsigned int Location[2];
	
	unsigned int Key;
} QNode;

/* Queue */
typedef struct Queue
{
    unsigned count;  
    QNode *front, *rear;
} Queue;
 
/* Hash */
typedef struct Hash
{
    QNode* *array; 
} Hash;

 #endif