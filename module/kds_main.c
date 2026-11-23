// SPDX-License-Identifier: GPL-2.0
/*
 * kds_main.c - Module entry/exit points and boot selftest for kds_store LKM
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "kds_store.h"

static unsigned int max_entries = KDS_DEFAULT_MAX_ENTRIES;
module_param(max_entries, uint, 0444);
MODULE_PARM_DESC(max_entries, "Maximum number of entries allowed in kds_store");

static bool run_selftest = true;
module_param(run_selftest, bool, 0444);
MODULE_PARM_DESC(run_selftest, "Run internal CRUD selftest on module load");

static struct kds_store g_store;

static int kds_run_boot_selftest(struct kds_store *store)
{
	struct kds_entry_snapshot snap;
	int ret;

	ret = kds_store_add(store, 205, 2500, "temp_core0");
	if (ret)
		return ret;

	ret = kds_store_add(store, 101, 3300, "vcc_rail");
	if (ret)
		return ret;

	/* Duplicate ID must fail with -EEXIST */
	ret = kds_store_add(store, 101, 9999, "duplicate");
	if (ret != -EEXIST) {
		pr_err("selftest failed: expected -EEXIST, got %d\n", ret);
		return -EIO;
	}

	/* In-place update of value and tag */
	ret = kds_store_update(store, 205, 2750, "temp_core0_cal");
	if (ret)
		return ret;

	/* Lock-safe snapshot lookup */
	ret = kds_store_lookup_copy(store, 205, &snap);
	if (ret)
		return ret;

	pr_info("selftest ok: id=%u val=%lld tag='%s'\n", snap.id, snap.value,
		snap.tag);
	return 0;
}

static int __init kds_store_init(void)
{
	struct kds_stats stats;
	int ret;

	ret = kds_core_init(&g_store, max_entries);
	if (ret)
		return ret;

	if (run_selftest) {
		ret = kds_run_boot_selftest(&g_store);
		if (ret) {
			kds_core_destroy(&g_store);
			return ret;
		}
	}

	kds_stats_snapshot(&g_store, &stats);
	pr_info("loaded: max_entries=%u active=%llu updates=%llu hits=%llu misses=%llu\n",
		g_store.max_entries, stats.active_entries, stats.update_count,
		stats.lookup_hits, stats.lookup_misses);
	return 0;
}

static void __exit kds_store_exit(void)
{
	struct kds_stats stats;

	kds_core_destroy(&g_store);
	kds_stats_snapshot(&g_store, &stats);

	pr_info("unloaded: active=%llu allocs=%llu frees=%llu updates=%llu\n",
		stats.active_entries, stats.alloc_count, stats.free_count,
		stats.update_count);
}

module_init(kds_store_init);
module_exit(kds_store_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Linux Portfolio");
MODULE_DESCRIPTION(
	"ARM64 LKM demonstrating slab, list_head, rbtree, procfs, and sysfs");
MODULE_VERSION("0.4.3");
