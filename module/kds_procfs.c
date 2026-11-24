// SPDX-License-Identifier: GPL-2.0
/*
 * kds_procfs.c - Paginated procfs views (seq_file) for list_head and rbtree
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/limits.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>

#include "kds_store.h"

#define KDS_PROC_DIR_NAME "kds_store"

/* ---- /proc/kds_store/list (chronological insertion order) ---- */

static void *kds_list_seq_start(struct seq_file *m, loff_t *pos)
{
	struct kds_store *store = m->private;

	mutex_lock(&store->lock);
	return seq_list_start_head(&store->entry_list, *pos);
}

static void *kds_list_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct kds_store *store = m->private;

	return seq_list_next(v, &store->entry_list, pos);
}

static void kds_list_seq_stop(struct seq_file *m, void *v)
{
	struct kds_store *store = m->private;

	mutex_unlock(&store->lock);
}

static int kds_list_seq_show(struct seq_file *m, void *v)
{
	struct kds_store *store = m->private;
	struct kds_entry *entry;

	if (v == &store->entry_list) {
		seq_puts(m, "ID\tVALUE\tTIMESTAMP_NS\tTAG\n");
		return 0;
	}

	entry = list_entry(v, struct kds_entry, list_node);
	seq_printf(m, "%u\t%lld\t%llu\t%s\n", entry->id, entry->value,
		   entry->timestamp_ns, entry->tag);
	return 0;
}

static const struct seq_operations kds_list_seq_ops = {
	.start = kds_list_seq_start,
	.next = kds_list_seq_next,
	.stop = kds_list_seq_stop,
	.show = kds_list_seq_show,
};

static int kds_list_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &kds_list_seq_ops);

	if (!ret) {
		struct seq_file *m = file->private_data;

		m->private = pde_data(inode);
	}
	return ret;
}

static const struct proc_ops kds_list_proc_ops = {
	.proc_open = kds_list_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};

/* ---- /proc/kds_store/rbtree (sorted by unique ID) ---- */

static struct rb_node *kds_rb_lower_bound(struct rb_root *root, u32 target_id)
{
	struct rb_node *node = root->rb_node;
	struct rb_node *best = NULL;

	while (node) {
		struct kds_entry *cur =
			rb_entry(node, struct kds_entry, rb_node);

		if (cur->id >= target_id) {
			best = node;
			node = node->rb_left;
		} else {
			node = node->rb_right;
		}
	}
	return best;
}

static void *kds_rb_seq_start(struct seq_file *m, loff_t *pos)
{
	struct kds_store *store = m->private;

	mutex_lock(&store->lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos > (loff_t)U32_MAX + 1)
		return NULL;

	return kds_rb_lower_bound(&store->entry_tree, (u32)(*pos - 1));
}

static void *kds_rb_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct kds_store *store = m->private;
	struct rb_node *next_node;

	if (v == SEQ_START_TOKEN)
		next_node = rb_first(&store->entry_tree);
	else
		next_node = rb_next((struct rb_node *)v);

	if (next_node) {
		struct kds_entry *entry =
			rb_entry(next_node, struct kds_entry, rb_node);

		*pos = (loff_t)entry->id + 1;
	} else {
		*pos = (loff_t)U32_MAX + 2;
	}

	return next_node;
}

static void kds_rb_seq_stop(struct seq_file *m, void *v)
{
	struct kds_store *store = m->private;

	mutex_unlock(&store->lock);
}

static int kds_rb_seq_show(struct seq_file *m, void *v)
{
	struct kds_entry *entry;

	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "ID\tVALUE\tTIMESTAMP_NS\tTAG\n");
		return 0;
	}

	entry = rb_entry(v, struct kds_entry, rb_node);
	seq_printf(m, "%u\t%lld\t%llu\t%s\n", entry->id, entry->value,
		   entry->timestamp_ns, entry->tag);
	return 0;
}

static const struct seq_operations kds_rb_seq_ops = {
	.start = kds_rb_seq_start,
	.next = kds_rb_seq_next,
	.stop = kds_rb_seq_stop,
	.show = kds_rb_seq_show,
};

static int kds_rb_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &kds_rb_seq_ops);

	if (!ret) {
		struct seq_file *m = file->private_data;

		m->private = pde_data(inode);
	}
	return ret;
}

static const struct proc_ops kds_rb_proc_ops = {
	.proc_open = kds_rb_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};

int kds_procfs_init(struct kds_store *store)
{
	struct proc_dir_entry *ent;

	store->proc_dir = proc_mkdir(KDS_PROC_DIR_NAME, NULL);
	if (!store->proc_dir) {
		pr_err("failed to create /proc/%s\n", KDS_PROC_DIR_NAME);
		return -ENOMEM;
	}

	ent = proc_create_data("list", 0444, store->proc_dir,
			       &kds_list_proc_ops, store);
	if (!ent)
		goto err_remove;

	ent = proc_create_data("rbtree", 0444, store->proc_dir,
			       &kds_rb_proc_ops, store);
	if (!ent)
		goto err_remove;

	return 0;

err_remove:
	remove_proc_subtree(KDS_PROC_DIR_NAME, NULL);
	store->proc_dir = NULL;
	return -ENOMEM;
}

void kds_procfs_exit(struct kds_store *store)
{
	if (store->proc_dir) {
		remove_proc_subtree(KDS_PROC_DIR_NAME, NULL);
		store->proc_dir = NULL;
	}
}
