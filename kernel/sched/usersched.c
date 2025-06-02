#include <linux/hashtable.h>
#include <linux/rbtree.h>
#include <linux/uidgid.h> 
#include <linux/slab.h>
#include <linux/jhash.h>
#include <linux/string.h>
#include <linux/sched.h>

/*
  * Usersched - User-based function tracking
  *
  * This file implements the core logic for a tracking system that
  * count the number of times a function was called. The core idea
  * is to use this "library" to adjuste the scheduling of a task by
  * manipulating the vruntime of a task in <fair.c>.
  * 
  * Implementation details:
  * In principle the first idea was to use a hash table for each user
  * that stores a rbtree of function call counts by the user. This is
  * however not the best idea because it introduces a lot of implementation
  * overhead and complexity. Instead we use a single hash table that 
  * stores function call counts for all users. The key of each entry
  * is a combination of the user ID and the function pointer. This way  
  * user-space is seperated too, but more in an abstract, logical way
  * as in a rather concrete implementation way.
*/

// Create the hash table (2^10 = 1024 entries)
#define USCHED_HASH_BITS 10
DEFINE_HASHTABLE(function_usage_ht, USCHED_HASH_BITS);


// Use uid and comm pointer as key
struct usage_key {
    kuid_t uid;
    char comm[TASK_COMM_LEN];
};

// Define the structure for function usage tracking
struct function_usage {
    struct usage_key key;
    unsigned long count;

    struct hlist_node hnode;
};

// Use a hash function to map the comamnd and uid to a hash value
// Linux hash tables only support 64-bit keys, so we need to combine
// the uid and the function name hash into a single 64-bit value.
// In principle this could lead to collission but we check this later
// in the hash lookup calls.
static inline u64 hash_func(const struct usage_key *key) {
    u64 uid_val = key->uid.val;
    u64 name_hash = jhash(key->comm, strnlen(key->comm, TASK_COMM_LEN), 0);
    return hash_64((uid_val << 32) ^ name_hash, USCHED_HASH_BITS);
}

// Compare the uid and comm with the key to make sure the 
// hash is accessed correctly.
static inline bool cmp_uid_comm(kuid_t uid, const char *comm, struct usage_key *key) {
    key->uid = uid;
    strscpy(key->comm, comm, TASK_COMM_LEN);
    return uid_eq(key->uid, key->uid) && strncmp(key->comm, comm, TASK_COMM_LEN) == 0;
}

// Get the usage count for function and update its value
long usched_update_usage(kuid_t uid, const char *comm) {
    struct function_usage *entry;
    struct usage_key key;

    hash_for_each_possible(function_usage_ht, entry, hnode, hash_func(&key)) {
        if (cmp_uid_comm(uid, comm, &entry->key)) {
            entry->count++;
            return entry->count;
        }
    }
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);

    // Catch memory allocation failure
    if (!entry)
        return -ENOMEM;

    entry->key = key;
    entry->count = 1;
    hash_add(function_usage_ht, &entry->hnode, hash_func(&key));
    return 1;
}

// Get the usage count 
long usched_get_usage(kuid_t uid, const char *comm) {
    struct function_usage *entry;
    struct usage_key key;

    hash_for_each_possible(function_usage_ht, entry, hnode, hash_func(&key)) {
        if (cmp_uid_comm(uid, comm, &entry->key)) {
            return entry->count;
        }
    }
    // Function not used yet.
    return 0;
}