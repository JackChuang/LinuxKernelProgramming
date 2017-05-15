#include "core.h"

typedef struct {
	struct list_head p_list;
    int dirty;
    int index;
    page_state p_state;
    blk_state *b_state;
    char key[88];
} bucket;

//#define HASH_SIZE 1024 
#define BUCKET_INIT { .dirty = 0, .b_state = NULL, .p_state = PG_FREE, .index = -1, .key[0]='\0'}

#define HASH_TABLE(name, size)  \
					bucket name[size] = \
					{ [0 ... (size - 1)] = BUCKET_INIT }

unsigned int hash(const char *str);
int hash_add(bucket *hashtable, const char *key, int index);
int hash_search(bucket *hashtable, const char *key);
