// SPDX-License-Identifier: GPL-2.0
/*
 * kds_store.c - Kernel Data Structures Store LKM
 *
 * Implements slab cache management (kmem_cache), dynamic kmalloc/kfree
 * allocations, and SMP-safe synchronization using mutex and spinlock_t.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "kds_store.h"

static struct kds_store g_store;

static void kds_stats_record_alloc(struct kds_store *store)
{
	spin_lock(&store->stats_lock);
	store->stats.alloc_count++;
	store->stats.active_entries++;
	spin_unlock(&store->stats_lock);
}

static void kds_stats_record_free(struct kds_store *store)
{
	spin_lock(&store->stats_lock);
	store->stats.free_count++;
	if (store->stats.active_entries > 0)
		store->stats.active_entries--;
	spin_unlock(&store->stats_lock);
}

static void kds_stats_snapshot(struct kds_store *store, struct kds_stats *out)
{
	spin_lock(&store->stats_lock);
	*out = store->stats;
	spin_unlock(&store->stats_lock);
}

/**
 * kds_entry_alloc() - Allocate and initialize a new kds_entry record
 * @store: Store instance owning the slab cache
 * @id:    Unique record identifier
 * @value: Initial 64-bit signed value
 * @tag:   NUL-terminated descriptive tag (truncated to KDS_TAG_MAX_LEN)
 *
 * Context: Process context (may sleep due to GFP_KERNEL).
 * Return: Pointer to the allocated entry, or NULL on allocation failure.
 */
static struct kds_entry *kds_entry_alloc(struct kds_store *store, u32 id,
					 s64 value, const char *tag)
{
	struct kds_entry *entry;
	const char *src_tag = tag ? tag : "default";
	size_t len;

	entry = kmem_cache_zalloc(store->cache, GFP_KERNEL);
	if (!entry)
		return NULL;

	len = strnlen(src_tag, KDS_TAG_MAX_LEN);
	entry->tag = kmalloc(len + 1, GFP_KERNEL);
	if (!entry->tag)
		goto err_free_entry;

	memcpy(entry->tag, src_tag, len);
	entry->tag[len] = '\0';

	entry->id = id;
	entry->value = value;
	entry->tag_len = len;
	entry->timestamp_ns = ktime_get_ns();

	kds_stats_record_alloc(store);
	return entry;

err_free_entry:
	kmem_cache_free(store->cache, entry);
	return NULL;
}

/**
 * kds_entry_free() - Release a kds_entry record and its dynamic tag buffer
 * @store: Store instance owning the slab cache
 * @entry: Entry to free (NULL-safe)
 */
static void kds_entry_free(struct kds_store *store, struct kds_entry *entry)
{
	if (!entry)
		return;

	kfree(entry->tag);
	kmem_cache_free(store->cache, entry);
	kds_stats_record_free(store);
}

/**
 * __kds_store_set_sample() - Attach a selftest entry while holding store->lock
 * @store: Store instance
 * @entry: Allocated entry to store
 */
static void __kds_store_set_sample(struct kds_store *store,
				   struct kds_entry *entry)
{
	lockdep_assert_held(&store->lock);
	store->boot_sample = entry;
}

static int __init kds_store_init(void)
{
	struct kds_entry *entry;
	struct kds_stats snap;

	mutex_init(&g_store.lock);
	spin_lock_init(&g_store.stats_lock);
	g_store.max_entries = KDS_DEFAULT_MAX_ENTRIES;

	g_store.cache = kmem_cache_create(KDS_CACHE_NAME,
					  sizeof(struct kds_entry), 0,
					  SLAB_HWCACHE_ALIGN, NULL);
	if (!g_store.cache) {
		pr_err("failed to create slab cache '%s'\n", KDS_CACHE_NAME);
		mutex_destroy(&g_store.lock);
		return -ENOMEM;
	}

	entry = kds_entry_alloc(&g_store, 1, 4200, "sync_selftest");
	if (!entry) {
		pr_err("failed to allocate selftest entry\n");
		kmem_cache_destroy(g_store.cache);
		g_store.cache = NULL;
		mutex_destroy(&g_store.lock);
		return -ENOMEM;
	}

	mutex_lock(&g_store.lock);
	__kds_store_set_sample(&g_store, entry);
	mutex_unlock(&g_store.lock);

	kds_stats_snapshot(&g_store, &snap);
	pr_info("loaded: active=%llu allocs=%llu frees=%llu max_entries=%u\n",
		snap.active_entries, snap.alloc_count, snap.free_count,
		g_store.max_entries);
	return 0;
}

static void __exit kds_store_exit(void)
{
	struct kds_entry *entry;
	struct kds_stats snap;

	mutex_lock(&g_store.lock);
	entry = g_store.boot_sample;
	g_store.boot_sample = NULL;
	mutex_unlock(&g_store.lock);

	kds_entry_free(&g_store, entry);

	kds_stats_snapshot(&g_store, &snap);
	kmem_cache_destroy(g_store.cache);
	g_store.cache = NULL;
	mutex_destroy(&g_store.lock);

	pr_info("unloaded: active=%llu allocs=%llu frees=%llu\n",
		snap.active_entries, snap.alloc_count, snap.free_count);
}

module_init(kds_store_init);
module_exit(kds_store_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Linux Portfolio");
MODULE_DESCRIPTION(
	"ARM64 LKM demonstrating slab, list_head, rbtree, procfs, and sysfs");
MODULE_VERSION("0.3.0");
