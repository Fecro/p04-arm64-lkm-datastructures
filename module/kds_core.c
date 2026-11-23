// SPDX-License-Identifier: GPL-2.0
/*
 * kds_core.c - Core slab allocator, intrusive containers, and CRUD operations
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "kds_store.h"

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

static void kds_stats_record_update(struct kds_store *store)
{
	spin_lock(&store->stats_lock);
	store->stats.update_count++;
	spin_unlock(&store->stats_lock);
}

static void kds_stats_record_lookup(struct kds_store *store, bool hit)
{
	spin_lock(&store->stats_lock);
	if (hit)
		store->stats.lookup_hits++;
	else
		store->stats.lookup_misses++;
	spin_unlock(&store->stats_lock);
}

void kds_stats_snapshot(struct kds_store *store, struct kds_stats *out)
{
	spin_lock(&store->stats_lock);
	*out = store->stats;
	spin_unlock(&store->stats_lock);
}

static char *kds_tag_dup(const char *tag, size_t *out_len)
{
	const char *src = tag ? tag : "default";
	size_t len = strnlen(src, KDS_TAG_MAX_LEN);
	char *buf;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf)
		return NULL;

	memcpy(buf, src, len);
	buf[len] = '\0';
	*out_len = len;
	return buf;
}

static struct kds_entry *kds_entry_alloc(struct kds_store *store, u32 id,
					 s64 value, const char *tag)
{
	struct kds_entry *entry;
	size_t len = 0;

	entry = kmem_cache_zalloc(store->cache, GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->tag = kds_tag_dup(tag, &len);
	if (!entry->tag)
		goto err_free_entry;

	entry->id = id;
	entry->value = value;
	entry->tag_len = len;
	entry->timestamp_ns = ktime_get_ns();
	INIT_LIST_HEAD(&entry->list_node);
	RB_CLEAR_NODE(&entry->rb_node);

	kds_stats_record_alloc(store);
	return entry;

err_free_entry:
	kmem_cache_free(store->cache, entry);
	return NULL;
}

static void kds_entry_free(struct kds_store *store, struct kds_entry *entry)
{
	if (!entry)
		return;

	kfree(entry->tag);
	kmem_cache_free(store->cache, entry);
	kds_stats_record_free(store);
}

static struct kds_entry *__kds_rb_search(struct kds_store *store, u32 id)
{
	struct rb_node *node = store->entry_tree.rb_node;

	lockdep_assert_held(&store->lock);

	while (node) {
		struct kds_entry *cur =
			rb_entry(node, struct kds_entry, rb_node);

		if (id < cur->id) {
			node = node->rb_left;
		} else if (id > cur->id) {
			node = node->rb_right;
		} else {
			kds_stats_record_lookup(store, true);
			return cur;
		}
	}

	kds_stats_record_lookup(store, false);
	return NULL;
}

static int __kds_rb_insert(struct kds_store *store, struct kds_entry *new_entry)
{
	struct rb_node **link = &store->entry_tree.rb_node;
	struct rb_node *parent = NULL;

	lockdep_assert_held(&store->lock);

	while (*link) {
		struct kds_entry *cur;

		parent = *link;
		cur = rb_entry(parent, struct kds_entry, rb_node);

		if (new_entry->id < cur->id)
			link = &(*link)->rb_left;
		else if (new_entry->id > cur->id)
			link = &(*link)->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&new_entry->rb_node, parent, link);
	rb_insert_color(&new_entry->rb_node, &store->entry_tree);
	return 0;
}

static void __kds_rb_erase(struct kds_store *store, struct kds_entry *entry)
{
	lockdep_assert_held(&store->lock);

	if (!RB_EMPTY_NODE(&entry->rb_node)) {
		rb_erase(&entry->rb_node, &store->entry_tree);
		RB_CLEAR_NODE(&entry->rb_node);
	}
}

int kds_core_init(struct kds_store *store, u32 max_entries)
{
	if (max_entries == 0 || max_entries > KDS_HARD_MAX_ENTRIES)
		return -EINVAL;

	memset(store, 0, sizeof(*store));
	mutex_init(&store->lock);
	spin_lock_init(&store->stats_lock);
	INIT_LIST_HEAD(&store->entry_list);
	store->entry_tree = RB_ROOT;
	store->max_entries = max_entries;

	store->cache = kmem_cache_create(KDS_CACHE_NAME,
					 sizeof(struct kds_entry), 0,
					 SLAB_HWCACHE_ALIGN, NULL);
	if (!store->cache) {
		pr_err("failed to create slab cache '%s'\n", KDS_CACHE_NAME);
		mutex_destroy(&store->lock);
		return -ENOMEM;
	}

	return 0;
}

void kds_core_destroy(struct kds_store *store)
{
	kds_store_clear_all(store);
	kmem_cache_destroy(store->cache);
	store->cache = NULL;
	mutex_destroy(&store->lock);
}

int kds_store_add(struct kds_store *store, u32 id, s64 value, const char *tag)
{
	struct kds_entry *entry;
	int ret;

	entry = kds_entry_alloc(store, id, value, tag);
	if (!entry)
		return -ENOMEM;

	mutex_lock(&store->lock);
	if (store->entry_count >= store->max_entries) {
		ret = -ENOSPC;
		goto err_unlock;
	}

	ret = __kds_rb_insert(store, entry);
	if (ret)
		goto err_unlock;

	list_add_tail(&entry->list_node, &store->entry_list);
	store->entry_count++;
	mutex_unlock(&store->lock);
	return 0;

err_unlock:
	mutex_unlock(&store->lock);
	kds_entry_free(store, entry);
	return ret;
}

int kds_store_update(struct kds_store *store, u32 id, s64 value,
		     const char *tag)
{
	struct kds_entry *entry;
	char *new_tag = NULL;
	char *old_tag = NULL;
	size_t new_len = 0;

	/* Pre-allocate replacement tag outside store->lock if requested */
	if (tag) {
		new_tag = kds_tag_dup(tag, &new_len);
		if (!new_tag)
			return -ENOMEM;
	}

	mutex_lock(&store->lock);
	entry = __kds_rb_search(store, id);
	if (!entry) {
		mutex_unlock(&store->lock);
		kfree(new_tag);
		return -ENOENT;
	}

	entry->value = value;
	entry->timestamp_ns = ktime_get_ns();
	if (new_tag) {
		old_tag = entry->tag;
		entry->tag = new_tag;
		entry->tag_len = new_len;
	}
	mutex_unlock(&store->lock);

	kfree(old_tag);
	kds_stats_record_update(store);
	return 0;
}

int kds_store_lookup_copy(struct kds_store *store, u32 id,
			  struct kds_entry_snapshot *out)
{
	struct kds_entry *entry;

	if (!out)
		return -EINVAL;

	mutex_lock(&store->lock);
	entry = __kds_rb_search(store, id);
	if (!entry) {
		mutex_unlock(&store->lock);
		return -ENOENT;
	}

	out->id = entry->id;
	out->value = entry->value;
	out->timestamp_ns = entry->timestamp_ns;
	strscpy(out->tag, entry->tag, sizeof(out->tag));
	mutex_unlock(&store->lock);

	return 0;
}

int kds_store_del(struct kds_store *store, u32 id)
{
	struct kds_entry *entry;

	mutex_lock(&store->lock);
	entry = __kds_rb_search(store, id);
	if (!entry) {
		mutex_unlock(&store->lock);
		return -ENOENT;
	}

	__kds_rb_erase(store, entry);
	list_del_init(&entry->list_node);
	store->entry_count--;
	mutex_unlock(&store->lock);

	kds_entry_free(store, entry);
	return 0;
}

void kds_store_clear_all(struct kds_store *store)
{
	LIST_HEAD(reap_list);
	struct kds_entry *entry, *tmp;

	mutex_lock(&store->lock);
	store->entry_tree = RB_ROOT;
	list_splice_init(&store->entry_list, &reap_list);
	store->entry_count = 0;
	mutex_unlock(&store->lock);

	list_for_each_entry_safe(entry, tmp, &reap_list, list_node) {
		RB_CLEAR_NODE(&entry->rb_node);
		list_del_init(&entry->list_node);
		kds_entry_free(store, entry);
	}
}

int kds_store_set_max_entries(struct kds_store *store, u32 new_max)
{
	if (new_max == 0 || new_max > KDS_HARD_MAX_ENTRIES)
		return -EINVAL;

	mutex_lock(&store->lock);
	if (new_max < store->entry_count) {
		mutex_unlock(&store->lock);
		return -EBUSY;
	}
	store->max_entries = new_max;
	mutex_unlock(&store->lock);

	return 0;
}
