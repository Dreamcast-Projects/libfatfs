
/* LRU cache */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "cache.h"
#include "dir_entry.h"

void add_to_hash(QNode *node);
void delete_from_hash(QNode *node);

void add_to_cache(const char *filepath, node_entry_t *temp);
void delete_from_cache(node_entry_t *file);

/* Global in this file */
static Queue *QUEUE;
static Hash  *HASH;

/* Hash function : http://www.cse.yorku.ca/~oz/hash.html */
unsigned long hash(const unsigned char *str)
{
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}
 
/* A utility function to create a new Queue Node. */
QNode* newQNode(const char *filepathname, node_entry_t *f)
{
    QNode* temp = (QNode *)malloc( sizeof( QNode ) );
	temp->FilePathName = malloc(strlen(filepathname)+1);
    strcpy(temp->FilePathName, filepathname); 
	temp->FilePathName[strlen(filepathname)] = '\0';
	temp->Attr = f->Attr;
	temp->Location[0] = f->Location[0];
	temp->Location[1] = f->Location[1];
 
    temp->prevQ = temp->nextQ = temp->nextH = NULL;
 
    return temp;
}
 
/* A utility function to create an empty Queue */
Queue* createQueue()
{
    QUEUE = malloc(sizeof(Queue));
 
    QUEUE->count = 0;
    QUEUE->front = QUEUE->rear = NULL;
 
    return QUEUE;
}
 
/* A utility function to create an empty Hash of given capacity */
Hash* createHash()
{
    // Allocate memory for hash
    HASH = malloc(sizeof(Hash));
 
    // Create an array of pointers for refering queue nodes
    HASH->array = malloc(CACHESIZE * sizeof( QNode* ));
 
    // Initialize all hash entries as empty
    int i;
    for( i = 0; i < CACHESIZE; ++i )
        HASH->array[i] = NULL;
 
    return HASH;
}
 
/* Is there still more room in the queue? */
int AreAllFramesFull()
{
    return QUEUE->count == CACHESIZE;
}
 
/* Check if queue is empty */
int isQueueEmpty()
{
    return QUEUE->rear == NULL;
}
 
/* Delete last node in queue */
void deQueue()
{
    if(isQueueEmpty())
        return;
 
    /* If this is the only node in list, then change front */
    if (QUEUE->front == QUEUE->rear)
        QUEUE->front = NULL;
 
    /* Change rear and remove the previous rear */
    QNode* temp = QUEUE->rear;
    QUEUE->rear = QUEUE->rear->prevQ;
 
    if (QUEUE->rear)
        QUEUE->rear->nextQ = NULL;
 
	free(temp->FilePathName);
    free(temp);
 
    /* decrement the number of nodes in queue by 1 */
    QUEUE->count--;
}

void enQueue(QNode *node)
{
	printf("Adding %s to cache with key %d", node->FilePathName, node->Key);
	
    /* If queue is full, remove the node at the rear */
    if (AreAllFramesFull())
    {
        /* Remove node from hash */
		delete_from_hash(QUEUE->rear);
		
		/* Delete rear node from queue */
        deQueue();
    }
 
    /* Add the new node to the front of queue */
    node->nextQ = QUEUE->front;
 
    /* If queue is empty, change both front and rear pointers */
    if (isQueueEmpty())
        QUEUE->rear = QUEUE->front = node;
    else  /* Else change the front */
    {
        QUEUE->front->prevQ = node;
        QUEUE->front = node;
    }
 
    /* Add node to hash also */
	add_to_hash(node);
 
    /* increment number of full frames */
    QUEUE->count++;
}
 
void ReferenceNode(QNode *node)
{
    QNode* reqPage = node;
 
    /* node is not at front, change pointer */
    if (reqPage != QUEUE->front)
    {
        /* Unlink requested node from its current location in queue. */
        reqPage->prevQ->nextQ = reqPage->nextQ;
        if (reqPage->nextQ)
           reqPage->nextQ->prevQ = reqPage->prevQ;
 
        /* If the requested node is rear, then change rear
           as this node will be moved to front
		*/
        if (reqPage == QUEUE->rear)
        {
           QUEUE->rear = reqPage->prevQ;
           QUEUE->rear->nextQ = NULL;
        }
 
        /* Put the requested node before current front */
        reqPage->nextQ = QUEUE->front;
        reqPage->prevQ = NULL;
 
        /* Change prev of current front */
        reqPage->nextQ->prevQ = reqPage;
 
        /* Change front to the requested node */
        QUEUE->front = reqPage;
    }
}

void init_cache()
{
	QUEUE = createQueue();
	HASH = createHash();
}

void deinit_cache()
{
	while(!isQueueEmpty())
	{
		deQueue();
	}
	
	free(QUEUE);
	
	free(HASH->array);
	free(HASH);
}

node_entry_t *check_cache(fatfs_t *fat, const char *fpn, char **ufn)
{
	char *partial = NULL;
	char *temp = NULL;
	QNode *node = NULL;
	node_entry_t *rv = NULL;
	long hash_num = hash(fpn);

	hash_num = hash_num % CACHESIZE;
	
	node = HASH->array[hash_num];
	
	printf("Checking cache for: %s\n", fpn);
	
	temp = strrchr(fpn, '\\');
	
	while(node != NULL)
	{
		if(strcasecmp(fpn, node->FilePathName) == 0)
		{
			ReferenceNode(node);
			*ufn = malloc(2);
			*ufn[0] = '\\';
			*ufn[1] = '\0';
			printf("FOUND: %s\n", node->FilePathName);
			break;
		}
		
		node = node->nextH;
	}
		
	/* Extract info */
	if(node != NULL)
	{
		temp++; /* Skip '\' character */
		
		rv = build_from_loc(fat, node);
		
		rv->Name = malloc(strlen(temp)+1);
		memset(rv->Name, 0, strlen(temp)+1);
		strcpy(rv->Name, temp);
		
		return rv;
	}
	else
	{
		partial = malloc(temp-fpn+1);
		memset(partial, 0, temp-fpn+1);
		strncpy(partial, fpn, temp-fpn);
		
		hash_num = hash(partial);
		hash_num = hash_num % CACHESIZE;
		
		node = HASH->array[hash_num];
		
		while(node != NULL)
		{
			if(strcasecmp(partial, node->FilePathName) == 0)
			{
				ReferenceNode(node);
				*ufn = malloc(strlen(temp)+1);
				memset(*ufn, 0, strlen(temp)+1);
				strcpy(*ufn, temp);
				printf("FOUND: %s\n", node->FilePathName);
				break;
			}
			
			node = node->nextH;
		}
		
		if(node != NULL)
		{
			temp = strrchr(partial, '\\');
			temp++; /* Skip '\\' character */
		
			rv = build_from_loc(fat, node);
			
			rv->Name = malloc(strlen(temp)+1);
			memset(rv->Name, 0, strlen(temp)+1);
			strcpy(rv->Name, temp);
			
			free(partial);
			
			return rv;
		}
		free(partial);
	}
	
	return NULL;
}

void add_to_cache(const char *filepath, node_entry_t *temp)
{
	QNode *node = NULL;
	long hash_num = hash(filepath);
	hash_num = hash_num % CACHESIZE;
	
	node = newQNode(filepath, temp);
	
	node->Key = hash_num;
	
	enQueue(node);																																			
}

void delete_from_cache(node_entry_t *file)
{
	unsigned char *start;
	QNode *node = QUEUE->front;
	
	while(node != NULL)
	{
		start = strrchr(node->FilePathName, '\\');
		start++;
		
		if(strcasecmp(file->Name, start) == 0 
		&& file->Location[0] == node->Location[0] 
		&& file->Location[1] == node->Location[1] 
		&& file->Attr == node->Attr)
		{
			delete_from_hash(node);
			
			if(node == QUEUE->front)
			{
				QUEUE->front = node->nextQ;
				
				if(node->nextQ)
				{
					node->nextQ->prevQ = NULL;
				}
				else
				{
					QUEUE->rear = NULL;
				}
			}
			else 
			{
				/* Unlink requested node from its current location in queue. */
				node->prevQ->nextQ = node->nextQ;
				if (node->nextQ)
				   node->nextQ->prevQ = node->prevQ;
		 
				/* If the requested node is rear, then change rear */
				if (node == QUEUE->rear)
				{
				   QUEUE->rear = node->prevQ;
				   QUEUE->rear->nextQ = NULL;
				}
			}
			
			free(node->FilePathName);
			free(node);
			
			/* Decrement the number of queue nodes by 1 */
			QUEUE->count--;
		}
	}
}

void add_to_hash(QNode *node)
{
	QNode *temp = HASH->array[ node->Key ];
	
	if(temp == NULL)
	{
		HASH->array[ node->Key ] = node;
	}
	else 
	{
		while(temp->nextH != NULL)
		{
			temp = temp->nextH;
		}
		
		temp->nextH = node;
	}
}

void delete_from_hash(QNode *node)
{
	QNode *prev = NULL;
	QNode *del = HASH->array[node->Key];
	
	while(del != NULL)
	{
		if(strcasecmp(node->FilePathName, del->FilePathName) == 0)
		{
			if(prev != NULL)
			{
				prev->nextH = del->nextH;
			}
			else
			{
				HASH->array[node->Key] = NULL;
			}
		}
		
		prev = del;
		del = del->nextH;
	}
}

 