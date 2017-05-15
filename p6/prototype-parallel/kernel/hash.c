#include <linux/string.h>
#include "core.h"
#include "hash.h"
extern int HASH_SIZE;
unsigned int hash(const char *str)
{
    unsigned int hash = 5381;
    int c;

    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % HASH_SIZE;
}

int hash_add(bucket *hashtable, const char *key, int index)
{
    int ret;
	int hash_index = 0;
	int count = 0;
	int blk_number;
    //unsigned long flags;
    //spin_lock_irqsave(&one_lock, flags);

	hash_index = hash(key);
	while (hashtable[hash_index].p_state)
	{
		hashtable[hash_index].dirty = 1;
		if (count == HASH_SIZE - 1){
            ret = -1;
            goto exit;
        }
		hash_index = (hash_index + 1) % HASH_SIZE;
		count++;
	}
	
	blk_number = index / config.pages_per_block;
	hashtable[hash_index].p_state = PG_VALID;
	strcpy(hashtable[hash_index].key, key);
	hashtable[hash_index].index = index;
    ret = hash_index;


exit:
    //spin_unlock_irqrestore(&one_lock, flags);
	return ret;
}

int hash_search(bucket *hashtable, const char *key)
{
    int ret;
	int hash_index = 0;
	char *hash_key;
	int counter = 0;
    //unsigned long flags;
    //spin_lock_irqsave(&one_lock, flags);

	hash_index = hash(key) % HASH_SIZE;
	while(1)
	{
		if (counter == HASH_SIZE - 1)
		{
			//printk("probe the whole hashtable\n");
			ret = -1;
            goto exit2;
		}
		
		if (hashtable[hash_index].p_state)
		{
			hash_key = hashtable[hash_index].key;
			if (!strcmp(hash_key, key))
			{
				//printk("key match! %d\n",counter);
				break;
			}
		}

		if (hashtable[hash_index].dirty == 0)
		{
			//printk("%s doesn't exist in the hashtable\n", key);
			ret = -2;
            goto exit2;
		}

		hash_index = (hash_index + 1) % HASH_SIZE;
		counter++;
	}
    ret = hash_index;
exit2:
    //spin_unlock_irqrestore(&one_lock, flags);
	return ret;
}
