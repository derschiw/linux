#include <linux/hashtable.h>
#include <linux/rbtree.h>
#include <linux/uidgid.h> 
#include <linux/slab.h>
#include <linux/jhash.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/log2.h>
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
/*
Main scaling factor (the bigger the the higher)

USCHED_SCALE_FACTOR  = 4
exec_count=   1, scaled=1
exec_count=   2, scaled=1
exec_count=   4, scaled=1
exec_count=   8, scaled=2
exec_count=  16, scaled=2
exec_count=  32, scaled=3
exec_count=  64, scaled=3
exec_count= 128, scaled=4
exec_count= 256, scaled=4
exec_count= 512, scaled=4
exec_count=1024, scaled=5

USCHED_SCALE_FACTOR  = 16
exec_count=   1, scaled=1
exec_count=   2, scaled=1
exec_count=   4, scaled=2
exec_count=   8, scaled=3
exec_count=  16, scaled=4
exec_count=  32, scaled=5
exec_count=  64, scaled=6
exec_count= 128, scaled=7
exec_count= 256, scaled=8
exec_count= 512, scaled=9
exec_count=1024, scaled=10
*/
#define USCHED_SHIFT 10 // Scale by 1024 (+/- 1%)
#define USCHED_SCALE_FACTOR 16 
#define USCHED_TIEBREAK 4
#define USCHED_TIEBREAK_RECIPROCAL (1 << USCHED_SHIFT) / USCHED_TIEBREAK // = 1 / USCHED_TIEBREAK
#define USCHED_LOG_MIN_COUNT 0  // log2(1)
#define USCHED_LOG_MAX_COUNT 16 // log2(65536)
#define USCHED_RECIPROCAL ((1 << USCHED_SHIFT) / USCHED_LOG_MAX_COUNT) // this should be = (1 << 10) / 16 = 1024 / 16 = 64

// Hashtable
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


// Get the usage count for function and update its value
long usched_update_usage(kuid_t uid, const char *comm) {
    struct function_usage *entry;
    struct usage_key key;

    key.uid = uid;
    strscpy(key.comm, comm, TASK_COMM_LEN);

    hash_for_each_possible(function_usage_ht, entry, hnode, hash_func(&key)) {
        if (uid_eq(entry->key.uid, uid) &&
            strncmp(entry->key.comm, comm, TASK_COMM_LEN) == 0) {
            entry->count++;
            return entry->count;
        }
    }

    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry)
        return -ENOMEM;

    entry->key = key;
    entry->count = 1;
    hash_add(function_usage_ht, &entry->hnode, hash_func(&key));
    return 1;
}

long usched_get_usage(kuid_t uid, const char *comm) {
    struct function_usage *entry;
    struct usage_key key;

    key.uid = uid;
    strscpy(key.comm, comm, TASK_COMM_LEN);

    hash_for_each_possible(function_usage_ht, entry, hnode, hash_func(&key)) {
        if (uid_eq(entry->key.uid, uid) &&
            strncmp(entry->key.comm, comm, TASK_COMM_LEN) == 0) {
            return entry->count;
        }
    }
    return 0;
}

// Scale the usage count to a value between 1 and 16
// Should be this, but we can make it simpler
// 1 + (2*USCHED_SCALE_FACTOR - 1) * (logCount - USCHED_LOG_MIN_COUNT) / (USCHED_LOG_MAX_COUNT - USCHED_LOG_MIN_COUNT);
// = 1 + (2*USCHED_SCALE_FACTOR - 1) * logCount / USCHED_LOG_MAX_COUNT;
// We dont like division use we use shifting instead (fixed point arithmetic)
// int scaled = ((A * B) * RECIP) >> SHIFT;
static inline int __usched_scale(int exec_count){
    if (exec_count <= 0)
        return 1;

    int logCount = ilog2(exec_count);
    int factor = (2 * USCHED_SCALE_FACTOR - 1);
    int scaled = (factor * logCount * USCHED_RECIPROCAL) >> USCHED_SHIFT;

    return 1 + scaled;
}

static inline u64 __usched_scale_inverse(int exec_count) {
    if (exec_count <= 0)
        exec_count = 1;
    return (1 << USCHED_SHIFT) / exec_count;
}

// Now scale the weight directly
long usched_scale_weight(long weight, int exec_count) {

    // Scale the weight by the usage count
    int scaled_usage = __usched_scale(exec_count);
    // Scale the weight by the usage count
    return (weight * scaled_usage) >> USCHED_SHIFT;
}
// Scale delta by a scaled usage count 
// First scale down the count (as it can be very high) and then scale the delta
// return delta * scaled_usage / scaled_usage_tiebreak
u64 usched_scale_delta(u64 delta, int exec_count) {
    // return (delta * USCHED_TIEBREAK * __usched_scale_inverse(exec_count)) >> USCHED_SHIFT;
    u64 scaled_usage = __usched_scale_inverse(exec_count);
    u64 scaled_delta = (delta * scaled_usage) / USCHED_TIEBREAK;

    return scaled_delta;
}