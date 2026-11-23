/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kds_store.h - Internal definitions and core API for kds_store LKM
 */

#ifndef _KDS_STORE_H
#define _KDS_STORE_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define KDS_CACHE_NAME "kds_entry_cache"
#define KDS_TAG_MAX_LEN 64
#define KDS_DEFAULT_MAX_ENTRIES 1024U
#define KDS_HARD_MAX_ENTRIES 65536U

/**
 * struct kds_entry - Telemetry record stored in intrusive kernel containers
 * @id:           Unique numeric identifier (primary key for rbtree)
 * @value:        Signed 64-bit payload value
 * @timestamp_ns: Monotonic kernel timestamp at creation or last update (ns)
 * @tag_len:      Length of @tag excluding the NUL terminator
 * @tag:          Dynamically allocated label string (kmalloc/kfree)
 * @list_node:    Intrusive doubly-linked list hook (insertion order)
 * @rb_node:      Intrusive red-black tree hook (sorted by @id)
 */
struct kds_entry {
	u32 id;
	s64 value;
	u64 timestamp_ns;
	size_t tag_len;
	char *tag;
	struct list_head list_node;
	struct rb_node rb_node;
};

/**
 * struct kds_entry_snapshot - Lock-free copy of a kds_entry for safe reads
 * @id:           Record identifier
 * @value:        Record payload value
 * @timestamp_ns: Monotonic timestamp (ns)
 * @tag:          NUL-terminated copy of the entry tag
 */
struct kds_entry_snapshot {
	u32 id;
	s64 value;
	u64 timestamp_ns;
	char tag[KDS_TAG_MAX_LEN + 1];
};

/**
 * struct kds_stats - Runtime telemetry counters protected by stats_lock
 * @active_entries: Current number of live entries in the store
 * @alloc_count:    Cumulative successful allocations
 * @free_count:     Cumulative freed entries
 * @update_count:   Cumulative in-place updates
 * @lookup_hits:    Successful rbtree lookups
 * @lookup_misses:  Failed rbtree lookups
 */
struct kds_stats {
	u64 active_entries;
	u64 alloc_count;
	u64 free_count;
	u64 update_count;
	u64 lookup_hits;
	u64 lookup_misses;
};

/**
 * struct kds_store - Global state for the kds_store module
 * @cache:       Dedicated slab cache for struct kds_entry
 * @lock:        Mutex protecting containers and configuration (process context)
 * @stats_lock:  Spinlock protecting @stats (short non-sleeping critical section)
 * @stats:       Operational counters
 * @max_entries: Upper bound on active entries
 * @entry_count: Number of entries currently linked in containers (under @lock)
 * @entry_list:  Head of the intrusive doubly-linked list (insertion order)
 * @entry_tree:  Root of the intrusive red-black tree (indexed by id)
 *
 * Lock ordering: always acquire @lock before @stats_lock if both are needed.
 */
struct kds_store {
	struct kmem_cache *cache;
	struct mutex lock;
	spinlock_t stats_lock;
	struct kds_stats stats;
	u32 max_entries;
	u32 entry_count;
	struct list_head entry_list;
	struct rb_root entry_tree;
};

/* Core lifecycle and CRUD operations (kds_core.c) */
int kds_core_init(struct kds_store *store, u32 max_entries);
void kds_core_destroy(struct kds_store *store);

int kds_store_add(struct kds_store *store, u32 id, s64 value, const char *tag);
int kds_store_update(struct kds_store *store, u32 id, s64 value,
		     const char *tag);
int kds_store_lookup_copy(struct kds_store *store, u32 id,
			  struct kds_entry_snapshot *out);
int kds_store_del(struct kds_store *store, u32 id);
void kds_store_clear_all(struct kds_store *store);
int kds_store_set_max_entries(struct kds_store *store, u32 new_max);
void kds_stats_snapshot(struct kds_store *store, struct kds_stats *out);

#endif /* _KDS_STORE_H */
