// SPDX-License-Identifier: GPL-2.0
/*
 * kds_sysfs.c - sysfs control and telemetry attributes under /sys/kernel/kds_store
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "kds_store.h"

#define KDS_SYSFS_DIR_NAME "kds_store"
#define KDS_CMD_BUF_MAX 128

static struct kds_store *sysfs_store;

/* ---- max_entries (RW) ---- */

static ssize_t max_entries_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	u32 val;

	mutex_lock(&sysfs_store->lock);
	val = sysfs_store->max_entries;
	mutex_unlock(&sysfs_store->lock);

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t max_entries_store(struct kobject *kobj,
				 struct kobj_attribute *attr, const char *buf,
				 size_t count)
{
	u32 new_max;
	int ret;

	ret = kstrtou32(buf, 10, &new_max);
	if (ret)
		return ret;

	ret = kds_store_set_max_entries(sysfs_store, new_max);
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute max_entries_attr = __ATTR_RW(max_entries);

/* ---- stats (RO) ---- */

static ssize_t stats_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct kds_stats snap;
	u32 max_cap;

	kds_stats_snapshot(sysfs_store, &snap);

	mutex_lock(&sysfs_store->lock);
	max_cap = sysfs_store->max_entries;
	mutex_unlock(&sysfs_store->lock);

	return sysfs_emit(buf,
			  "active_entries=%llu\n"
			  "max_entries=%u\n"
			  "alloc_count=%llu\n"
			  "free_count=%llu\n"
			  "update_count=%llu\n"
			  "lookup_hits=%llu\n"
			  "lookup_misses=%llu\n",
			  snap.active_entries, max_cap, snap.alloc_count,
			  snap.free_count, snap.update_count, snap.lookup_hits,
			  snap.lookup_misses);
}
static struct kobj_attribute stats_attr = __ATTR_RO(stats);

static int kds_tag_validate(const char *tag)
{
	size_t i;

	if (!tag || tag[0] == '\0')
		return -EINVAL;

	for (i = 0; tag[i] != '\0'; i++) {
		unsigned char c = (unsigned char)tag[i];

		if (i >= KDS_TAG_MAX_LEN)
			return -ENAMETOOLONG;

		if (!isalnum(c) && c != '_' && c != '-' && c != '.')
			return -EINVAL;
	}

	return 0;
}

/* ---- Shared defensive parser for "<id> <value> [tag]" commands ---- */

static int kds_parse_record_cmd(const char *buf, size_t count,
				bool tag_required, u32 *out_id, s64 *out_val,
				char *out_tag, size_t tag_cap, bool *has_tag)
{
	char kbuf[KDS_CMD_BUF_MAX];
	char *p, *tok_id, *tok_val, *tok_tag;
	int ret;

	if (count == 0)
		return -EINVAL;

	if (count >= sizeof(kbuf))
		return -E2BIG;

	/* Reject embedded NUL bytes from raw write() calls */
	if (memchr(buf, '\0', count))
		return -EINVAL;

	memcpy(kbuf, buf, count);
	kbuf[count] = '\0';
	p = strim(kbuf);
	if (*p == '\0')
		return -EINVAL;

	tok_id = strsep(&p, " \t");
	if (!tok_id || !p)
		return -EINVAL;

	p = skip_spaces(p);
	tok_val = strsep(&p, " \t");
	if (!tok_val || *tok_val == '\0')
		return -EINVAL;

	ret = kstrtou32(tok_id, 10, out_id);
	if (ret)
		return ret;

	ret = kstrtos64(tok_val, 10, out_val);
	if (ret)
		return ret;

	if (p) {
		p = skip_spaces(p);
		if (*p != '\0') {
			tok_tag = strsep(&p, " \t");

			/* Reject unexpected trailing tokens */
			if (p && *skip_spaces(p) != '\0')
				return -EINVAL;

			ret = kds_tag_validate(tok_tag);
			if (ret)
				return ret;

			strscpy(out_tag, tok_tag, tag_cap);
			*has_tag = true;
			return 0;
		}
	}

	if (tag_required)
		return -EINVAL;

	*has_tag = false;
	return 0;
}

/* ---- add (WO): "<id> <value> <tag>" ---- */

static ssize_t add_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	u32 id;
	s64 value;
	char tag[KDS_TAG_MAX_LEN + 1];
	bool has_tag = false;
	int ret;

	ret = kds_parse_record_cmd(buf, count, true, &id, &value, tag,
				   sizeof(tag), &has_tag);
	if (ret)
		return ret;

	ret = kds_store_add(sysfs_store, id, value, tag);
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute add_attr = __ATTR_WO(add);

/* ---- update (WO): "<id> <value> [tag]" ---- */

static ssize_t update_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	u32 id;
	s64 value;
	char tag[KDS_TAG_MAX_LEN + 1];
	bool has_tag = false;
	int ret;

	ret = kds_parse_record_cmd(buf, count, false, &id, &value, tag,
				   sizeof(tag), &has_tag);
	if (ret)
		return ret;

	ret = kds_store_update(sysfs_store, id, value, has_tag ? tag : NULL);
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute update_attr = __ATTR_WO(update);

/* ---- del (WO): "<id>" ---- */

static ssize_t del_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	u32 id;
	int ret;

	ret = kstrtou32(buf, 10, &id);
	if (ret)
		return ret;

	ret = kds_store_del(sysfs_store, id);
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute del_attr = __ATTR_WO(del);

/* ---- lookup (RW): write "<id>", read last matched snapshot ---- */

static ssize_t lookup_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	struct kds_entry_snapshot snap;
	bool valid;

	mutex_lock(&sysfs_store->lock);
	valid = sysfs_store->has_last_lookup;
	if (valid)
		snap = sysfs_store->last_lookup;
	mutex_unlock(&sysfs_store->lock);

	if (!valid)
		return sysfs_emit(buf, "none\n");

	return sysfs_emit(buf, "id=%u value=%lld timestamp_ns=%llu tag=%s\n",
			  snap.id, snap.value, snap.timestamp_ns, snap.tag);
}

static ssize_t lookup_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	struct kds_entry_snapshot snap;
	u32 id;
	int ret;

	ret = kstrtou32(buf, 10, &id);
	if (ret)
		return ret;

	ret = kds_store_lookup_copy(sysfs_store, id, &snap);
	if (ret) {
		mutex_lock(&sysfs_store->lock);
		sysfs_store->has_last_lookup = false;
		mutex_unlock(&sysfs_store->lock);
		return ret;
	}

	mutex_lock(&sysfs_store->lock);
	sysfs_store->last_lookup = snap;
	sysfs_store->has_last_lookup = true;
	mutex_unlock(&sysfs_store->lock);

	return count;
}
static struct kobj_attribute lookup_attr = __ATTR_RW(lookup);

/* ---- clear (WO): write "1" ---- */

static ssize_t clear_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	u32 confirm;
	int ret;

	ret = kstrtou32(buf, 10, &confirm);
	if (ret || confirm != 1)
		return -EINVAL;

	kds_store_clear_all(sysfs_store);

	mutex_lock(&sysfs_store->lock);
	sysfs_store->has_last_lookup = false;
	mutex_unlock(&sysfs_store->lock);

	return count;
}
static struct kobj_attribute clear_attr = __ATTR_WO(clear);

static struct attribute *kds_sysfs_attrs[] = {
	&max_entries_attr.attr, &stats_attr.attr,
	&add_attr.attr,		&update_attr.attr,
	&del_attr.attr,		&lookup_attr.attr,
	&clear_attr.attr,	NULL,
};

static const struct attribute_group kds_sysfs_group = {
	.attrs = kds_sysfs_attrs,
};

int kds_sysfs_init(struct kds_store *store)
{
	int ret;

	sysfs_store = store;

	store->kobj = kobject_create_and_add(KDS_SYSFS_DIR_NAME, kernel_kobj);
	if (!store->kobj) {
		pr_err("failed to create /sys/kernel/%s\n", KDS_SYSFS_DIR_NAME);
		sysfs_store = NULL;
		return -ENOMEM;
	}

	ret = sysfs_create_group(store->kobj, &kds_sysfs_group);
	if (ret) {
		pr_err("failed to create sysfs attribute group (%d)\n", ret);
		kobject_put(store->kobj);
		store->kobj = NULL;
		sysfs_store = NULL;
		return ret;
	}

	return 0;
}

void kds_sysfs_exit(struct kds_store *store)
{
	if (store->kobj) {
		sysfs_remove_group(store->kobj, &kds_sysfs_group);
		kobject_put(store->kobj);
		store->kobj = NULL;
	}
	sysfs_store = NULL;
}
