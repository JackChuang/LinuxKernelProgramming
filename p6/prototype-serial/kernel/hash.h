#include "core.h"

typedef struct bucket_ {
	struct list_head p_list;
    int dirty;
    int index;
    char key[128];
    page_state p_state;
    blk_state *b_state;
} bucket;

unsigned int hash(const char *str);
int hash_add(lkp_kv_cfg *config,bucket *hashtable, const char *key, int index);
int hash_search(lkp_kv_cfg *config, bucket *hashtable, const char *key);
