// SPDX-License-Identifier: GPL-2.0
/*
 * kds_tester.c - Modern C (C23) functional, boundary, and SMP stress test
 * suite for the kds_store Linux Kernel Module (procfs + sysfs).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

constexpr char SYSFS_BASE[] = "/sys/kernel/kds_store";
constexpr char PROC_LIST[] = "/proc/kds_store/list";
constexpr char PROC_RBTREE[] = "/proc/kds_store/rbtree";
constexpr size_t MAX_PATH_LEN = 128;
constexpr size_t STRESS_COUNT = 512;
constexpr size_t SMP_THREADS = 4;
constexpr size_t SMP_OPS_PER_THREAD = 64;

struct kds_stats_parsed {
  uint64_t active_entries;
  uint32_t max_entries;
  uint64_t alloc_count;
  uint64_t free_count;
  uint64_t update_count;
  uint64_t lookup_hits;
  uint64_t lookup_misses;
};

struct thread_ctx {
  uint32_t worker_id;
  int failures;
};

[[nodiscard]] static int sysfs_write_raw(const char *attr, const void *data,
                                         size_t len) {
  char path[MAX_PATH_LEN] = {};
  int n = snprintf(path, sizeof(path), "%s/%s", SYSFS_BASE, attr);
  if (n < 0 || (size_t)n >= sizeof(path)) {
    return -ENAMETOOLONG;
  }

  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    return -errno;
  }

  ssize_t wr = write(fd, data, len);
  int saved_err = (wr < 0) ? errno : 0;
  close(fd);

  if (wr < 0) {
    return -saved_err;
  }
  return ((size_t)wr == len) ? 0 : -EIO;
}

[[nodiscard]] static int sysfs_write_str(const char *attr, const char *str) {
  return sysfs_write_raw(attr, str, strlen(str));
}

[[nodiscard]] static int read_entire_file(const char *path, char **out_buf,
                                          size_t *out_len) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -errno;
  }

  size_t cap = 4096;
  size_t used = 0;
  char *buf = malloc(cap);
  if (buf == nullptr) {
    close(fd);
    return -ENOMEM;
  }

  for (;;) {
    if (used + 1024 >= cap) {
      cap *= 2;
      char *next = realloc(buf, cap);
      if (next == nullptr) {
        free(buf);
        close(fd);
        return -ENOMEM;
      }
      buf = next;
    }

    ssize_t rd = read(fd, buf + used, cap - used - 1);
    if (rd < 0) {
      int err = errno;
      free(buf);
      close(fd);
      return -err;
    }
    if (rd == 0) {
      break;
    }
    used += (size_t)rd;
  }

  buf[used] = '\0';
  close(fd);
  *out_buf = buf;
  if (out_len != nullptr) {
    *out_len = used;
  }
  return 0;
}

[[nodiscard]] static int read_stats(struct kds_stats_parsed *st) {
  char path[MAX_PATH_LEN] = {};
  int n = snprintf(path, sizeof(path), "%s/stats", SYSFS_BASE);
  if (n < 0 || (size_t)n >= sizeof(path)) {
    return -ENAMETOOLONG;
  }

  char *buf = nullptr;
  int ret = read_entire_file(path, &buf, nullptr);
  if (ret != 0) {
    return ret;
  }

  *st = (struct kds_stats_parsed){};
  int matched = sscanf(buf,
                       "active_entries=%" SCNu64 "\n"
                       "max_entries=%" SCNu32 "\n"
                       "alloc_count=%" SCNu64 "\n"
                       "free_count=%" SCNu64 "\n"
                       "update_count=%" SCNu64 "\n"
                       "lookup_hits=%" SCNu64 "\n"
                       "lookup_misses=%" SCNu64 "\n",
                       &st->active_entries, &st->max_entries, &st->alloc_count,
                       &st->free_count, &st->update_count, &st->lookup_hits,
                       &st->lookup_misses);
  free(buf);
  return (matched == 7) ? 0 : -EPROTO;
}

[[nodiscard]] static int
verify_proc_order(const char *path, bool require_sorted, size_t *out_rows) {
  char *buf = nullptr;
  int ret = read_entire_file(path, &buf, nullptr);
  if (ret != 0) {
    return ret;
  }

  size_t rows = 0;
  uint32_t prev_id = 0;
  bool has_prev = false;
  bool first_line = true;

  char *saveptr = nullptr;
  for (char *line = strtok_r(buf, "\n", &saveptr); line != nullptr;
       line = strtok_r(nullptr, "\n", &saveptr)) {
    if (first_line) {
      first_line = false;
      if (strncmp(line, "ID\tVALUE\tTIMESTAMP_NS\tTAG", 25) != 0) {
        free(buf);
        return -EPROTO;
      }
      continue;
    }

    uint32_t id = 0;
    int64_t val = 0;
    uint64_t ts = 0;
    char tag[80] = {};
    if (sscanf(line, "%" SCNu32 "\t%" SCNd64 "\t%" SCNu64 "\t%79s", &id, &val,
               &ts, tag) != 4) {
      free(buf);
      return -EPROTO;
    }

    if (require_sorted && has_prev && id <= prev_id) {
      fprintf(stderr, "[FAIL] %s out of order: %" PRIu32 " <= %" PRIu32 "\n",
              path, id, prev_id);
      free(buf);
      return -EINVAL;
    }
    prev_id = id;
    has_prev = true;
    rows++;
  }

  free(buf);
  if (out_rows != nullptr) {
    *out_rows = rows;
  }
  return 0;
}

[[nodiscard]] static int test_crud(void) {
  printf("[1/4] Running functional CRUD & container ordering test...\n");

  if (sysfs_write_str("clear", "1") != 0 ||
      sysfs_write_str("max_entries", "1024") != 0) {
    return -1;
  }

  if (sysfs_write_str("add", "300 -50 sensor_c") != 0 ||
      sysfs_write_str("add", "100 1200 sensor_a") != 0 ||
      sysfs_write_str("add", "200 4500 sensor_b") != 0) {
    return -1;
  }

  size_t list_rows = 0;
  size_t rb_rows = 0;
  if (verify_proc_order(PROC_LIST, false, &list_rows) != 0 || list_rows != 3) {
    return -1;
  }
  if (verify_proc_order(PROC_RBTREE, true, &rb_rows) != 0 || rb_rows != 3) {
    return -1;
  }

  if (sysfs_write_str("update", "200 4999 sensor_b_cal") != 0 ||
      sysfs_write_str("lookup", "200") != 0) {
    return -1;
  }

  char *lookup_buf = nullptr;
  char lookup_path[MAX_PATH_LEN] = {};
  snprintf(lookup_path, sizeof(lookup_path), "%s/lookup", SYSFS_BASE);
  if (read_entire_file(lookup_path, &lookup_buf, nullptr) != 0) {
    return -1;
  }
  bool ok = (strstr(lookup_buf, "id=200 value=4999") != nullptr &&
             strstr(lookup_buf, "tag=sensor_b_cal") != nullptr);
  free(lookup_buf);
  if (!ok) {
    fprintf(stderr, "[FAIL] lookup mismatch after update\n");
    return -1;
  }

  if (sysfs_write_str("del", "100") != 0 ||
      sysfs_write_str("lookup", "100") != -ENOENT) {
    return -1;
  }

  printf(
      "      PASS (CRUD, list_head, rbtree sorted order, update & lookup)\n");
  return 0;
}

[[nodiscard]] static int expect_err(const char *label, const char *attr,
                                    const void *data, size_t len,
                                    int expected) {
  int ret = sysfs_write_raw(attr, data, len);
  if (ret != expected) {
    fprintf(stderr, "[FAIL] %s: expected %d (%s), got %d (%s)\n", label,
            expected, strerror(-expected), ret, strerror(-ret));
    return -1;
  }
  return 0;
}

[[nodiscard]] static int test_negative_cases(void) {
  printf("[2/4] Running defensive errno & boundary validation tests...\n");

  if (sysfs_write_str("clear", "1") != 0 ||
      sysfs_write_str("max_entries", "2") != 0 ||
      sysfs_write_str("add", "10 100 base_one") != 0) {
    return -1;
  }

  constexpr char nul_payload[] = "20 100 ok\0hidden";
  char long_tag_cmd[110] = {};
  snprintf(long_tag_cmd, sizeof(long_tag_cmd), "20 100 %065d", 0);

  if (expect_err("negative_id", "add", "-1 10 bad", 9, -EINVAL) != 0 ||
      expect_err("u32_overflow", "add", "999999999999 10 ovf", 19, -ERANGE) !=
          0 ||
      expect_err("embedded_nul", "add", nul_payload, sizeof(nul_payload) - 1,
                 -EINVAL) != 0 ||
      expect_err("invalid_tag_char", "add", "20 10 bad/tag!", 14, -EINVAL) !=
          0 ||
      expect_err("trailing_token", "add", "20 10 ok extra", 14, -EINVAL) != 0 ||
      expect_err("tag_too_long", "add", long_tag_cmd, strlen(long_tag_cmd),
                 -ENAMETOOLONG) != 0 ||
      expect_err("duplicate_id", "add", "10 200 dup", 10, -EEXIST) != 0 ||
      expect_err("del_missing", "del", "9999", 4, -ENOENT) != 0) {
    return -1;
  }

  if (sysfs_write_str("add", "20 200 base_two") != 0 ||
      expect_err("store_full", "add", "30 300 full", 11, -ENOSPC) != 0 ||
      expect_err("shrink_busy", "max_entries", "1", 1, -EBUSY) != 0) {
    return -1;
  }

  printf(
      "      PASS (all 10 negative errno cases matched kernel expectations)\n");
  return 0;
}

[[nodiscard]] static int test_stress_seqfile(void) {
  printf("[3/4] Running %zu-entry slab & multi-page seq_file stress test...\n",
         STRESS_COUNT);

  if (sysfs_write_str("clear", "1") != 0 ||
      sysfs_write_str("max_entries", "1024") != 0) {
    return -1;
  }

  char cmd[96] = {};
  for (size_t i = 0; i < STRESS_COUNT; i++) {
    /* Insert in pseudo-scrambled order to exercise rbtree rotations */
    uint32_t id = (uint32_t)(((i * 37U) + 13U) % (uint32_t)STRESS_COUNT) + 1U;
    snprintf(cmd, sizeof(cmd), "%" PRIu32 " %lld stress_node_%" PRIu32, id,
             (long long)(i * 10U), id);
    if (sysfs_write_str("add", cmd) != 0) {
      fprintf(stderr, "[FAIL] stress add failed at i=%zu id=%" PRIu32 "\n", i,
              id);
      return -1;
    }
  }

  size_t list_rows = 0;
  size_t rb_rows = 0;
  if (verify_proc_order(PROC_LIST, false, &list_rows) != 0 ||
      list_rows != STRESS_COUNT) {
    fprintf(stderr, "[FAIL] procfs list count=%zu expected=%zu\n", list_rows,
            STRESS_COUNT);
    return -1;
  }
  if (verify_proc_order(PROC_RBTREE, true, &rb_rows) != 0 ||
      rb_rows != STRESS_COUNT) {
    fprintf(stderr, "[FAIL] procfs rbtree count=%zu expected=%zu\n", rb_rows,
            STRESS_COUNT);
    return -1;
  }

  if (sysfs_write_str("clear", "1") != 0) {
    return -1;
  }

  struct kds_stats_parsed st = {};
  if (read_stats(&st) != 0 || st.active_entries != 0 ||
      st.alloc_count != st.free_count) {
    fprintf(stderr,
            "[FAIL] memory accounting mismatch: active=%" PRIu64
            " allocs=%" PRIu64 " frees=%" PRIu64 "\n",
            st.active_entries, st.alloc_count, st.free_count);
    return -1;
  }

  printf("      PASS (%zu rows verified across paginated seq_file; "
         "allocs==frees==%" PRIu64 ")\n",
         rb_rows, st.alloc_count);
  return 0;
}

static void *smp_worker(void *arg) {
  struct thread_ctx *ctx = (struct thread_ctx *)arg;
  uint32_t base = (ctx->worker_id + 1U) * 10000U;
  char cmd[96] = {};

  for (size_t i = 0; i < SMP_OPS_PER_THREAD; i++) {
    uint32_t id = base + (uint32_t)i;
    snprintf(cmd, sizeof(cmd), "%" PRIu32 " %" PRIu32 " smp_w%" PRIu32, id,
             (uint32_t)(i * 3U), ctx->worker_id);
    if (sysfs_write_str("add", cmd) != 0) {
      ctx->failures++;
      continue;
    }

    snprintf(cmd, sizeof(cmd), "%" PRIu32 " %" PRIu32 " smp_upd", id,
             (uint32_t)(i * 7U));
    if (sysfs_write_str("update", cmd) != 0) {
      ctx->failures++;
    }

    snprintf(cmd, sizeof(cmd), "%" PRIu32, id);
    if (sysfs_write_str("lookup", cmd) != 0) {
      ctx->failures++;
    }

    if ((i % 16U) == 0U) {
      size_t rows = 0;
      if (verify_proc_order(PROC_RBTREE, true, &rows) != 0) {
        ctx->failures++;
      }
    }

    if (sysfs_write_str("del", cmd) != 0) {
      ctx->failures++;
    }
  }

  return nullptr;
}

[[nodiscard]] static int test_smp_concurrency(void) {
  printf("[4/4] Running %zu-thread SMP concurrency test (%zu ops/thread)...\n",
         SMP_THREADS, SMP_OPS_PER_THREAD);

  pthread_t threads[SMP_THREADS] = {};
  struct thread_ctx ctx[SMP_THREADS] = {};

  for (size_t i = 0; i < SMP_THREADS; i++) {
    ctx[i].worker_id = (uint32_t)i;
    if (pthread_create(&threads[i], nullptr, smp_worker, &ctx[i]) != 0) {
      fprintf(stderr, "[FAIL] pthread_create failed\n");
      return -1;
    }
  }

  int total_failures = 0;
  for (size_t i = 0; i < SMP_THREADS; i++) {
    pthread_join(threads[i], nullptr);
    total_failures += ctx[i].failures;
  }

  struct kds_stats_parsed st = {};
  int stats_ret = read_stats(&st);
  if (total_failures != 0 || stats_ret != 0 || st.active_entries != 0 ||
      st.alloc_count != st.free_count) {
    fprintf(stderr,
            "[FAIL] SMP test failures=%d active=%" PRIu64 " allocs=%" PRIu64
            " frees=%" PRIu64 "\n",
            total_failures, st.active_entries, st.alloc_count, st.free_count);
    return -1;
  }

  printf("      PASS (0 races/errors; allocs==frees==%" PRIu64 ", hits=%" PRIu64
         ")\n",
         st.alloc_count, st.lookup_hits);
  return 0;
}

int main(void) {
  printf("=== kds_tester (Modern C23 userspace test suite) ===\n");

  if (test_crud() != 0 || test_negative_cases() != 0 ||
      test_stress_seqfile() != 0 || test_smp_concurrency() != 0) {
    fprintf(stderr, "=== TEST SUITE FAILED ===\n");
    return EXIT_FAILURE;
  }

  printf("=== ALL TESTS PASSED ===\n");
  return EXIT_SUCCESS;
}
