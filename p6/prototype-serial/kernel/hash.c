#include <linux/string.h>
#include "core.h"
#include "hash.h"

unsigned int hash(const char *str)
{
    unsigned int hash = 5381;
    int c;

    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }

    return hash;
}

int hash_add(lkp_kv_cfg *config, bucket *hashtable, const char *key, int index)
{
	int hash_index = 0;
	int count = 0;
	int hash_size = config->nb_blocks * config->pages_per_block;

	hash_index = hash(key) % hash_size;

	while (hashtable[hash_index].p_state)
	{
		hashtable[hash_index].dirty = 1;
		if (count == hash_size - 1){
            return -1;
        }

		hash_index = (hash_index + 1) % hash_size;
		count++;
	}
	
	hashtable[hash_index].p_state = PG_VALID;
	strcpy(hashtable[hash_index].key, key);
	hashtable[hash_index].index = index;

	
	return hash_index;
}

int hash_search(lkp_kv_cfg *config, bucket *hashtable, const char *key)
{
	int hash_index = 0;
	char *hash_key;
	int counter = 0;
    int hash_size = config->nb_blocks * config->pages_per_block;

	hash_index = hash(key) % hash_size;
	while(1)
	{
		if (counter == hash_size - 1)
		{
			/* printk(PRINT_PREF "probe the whole hashtable\n"); */
			return -1;
		}
		
		if (hashtable[hash_index].p_state)
		{
			hash_key = hashtable[hash_index].key;
			if (!strcmp(hash_key, key))
			{
				/* printk(PRINT_PREF "key match! %d\n",counter); */
				break;
			}
		}

		if (hashtable[hash_index].dirty == 0)
		{
			/* printk(PRINT_PREF "%s doesn't exist in the hashtable\n", key); */
			return -2;
		}

		hash_index = (hash_index + 1) % hash_size;
		counter++;
	}
    return hash_index;
}
