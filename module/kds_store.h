/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kds_store.h - Internal definitions for the Kernel Data Structures Store LKM
 */

#ifndef _KDS_STORE_H
#define _KDS_STORE_H

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define KDS_CACHE_NAME "kds_entry_cache"
#define KDS_TAG_MAX_LEN 64
#define KDS_DEFAULT_MAX_ENTRIES 1024U

/**
 * struct kds_entry - Telemetry record stored in kernel containers
 * @id:           Unique numeric identifier (primary key for rbtree)
 * @value:        Signed 64-bit payload value
 * @timestamp_ns: Monotonic kernel timestamp at creation (ns)
 * @tag_len:      Length of @tag excluding the NUL terminator
 * @tag:          Dynamically allocated label string (kmalloc/kfree)
 */
struct kds_entry {
	u32 id;
	s64 value;
	u64 timestamp_ns;
	size_t tag_len;
	char *tag;
};

/**
 * struct kds_stats - Runtime telemetry counters protected by stats_lock
 * @active_entries: Current number of live entries in the store
 * @alloc_count:    Cumulative successful allocations
 * @free_count:     Cumulative freed entries
 * @lookup_hits:    Successful rbtree lookups
 * @lookup_misses:  Failed rbtree lookups
 */
struct kds_stats {
	u64 active_entries;
	u64 alloc_count;
	u64 free_count;
	u64 lookup_hits;
	u64 lookup_misses;
};

/**
 * struct kds_store - Global state for the kds_store module
 * @cache:       Dedicated slab cache for struct kds_entry
 * @lock:        Mutex protecting containers and configuration (process context)
 * @stats_lock:  Spinlock protecting @stats (short non-sleeping critical
 * section)
 * @stats:       Operational counters
 * @max_entries: Upper bound on active entries (configurable via sysfs)
 * @boot_sample: Temporary boot selftest entry (replaced by containers in Phase
 * 4)
 *
 * Lock ordering: always acquire @lock before @stats_lock if both are needed.
 */
struct kds_store {
	struct kmem_cache *cache;
	struct mutex lock;
	spinlock_t stats_lock;
	struct kds_stats stats;
	u32 max_entries;
	struct kds_entry *boot_sample;
};

#endif /* _KDS_STORE_H */
