#!/bin/sh
# Automated memory-leak and lifecycle verification script for kds_store LKM.
# Runs repeated insmod -> slab inspection -> kds_tester -> rmmod cycles
# followed by a full kmemleak scan.

set -e

CYCLES="${1:-5}"
MOD_PATH="/lib/modules/kds_store.ko"
SLAB_DIR="/sys/kernel/slab/kds_entry_cache"
KMEMLEAK="/sys/kernel/debug/kmemleak"

echo "========================================================"
echo " kds_store Memory & Lifecycle Verification (${CYCLES} cycles)"
echo "========================================================"

if [ ! -e "${KMEMLEAK}" ]; then
  echo "[FAIL] ${KMEMLEAK} not found (is debugfs mounted and kmemleak=on?)" >&2
  exit 1
fi

# Clear any prior kmemleak entries before running the test cycles
echo clear >"${KMEMLEAK}"

i=1
while [ "${i}" -le "${CYCLES}" ]; do
  echo ""
  echo "--- Cycle ${i}/${CYCLES} ---"

  insmod "${MOD_PATH}"

  if [ ! -e "${SLAB_DIR}" ]; then
    echo "[FAIL] Slab cache entry ${SLAB_DIR} missing after insmod" >&2
    exit 1
  fi

  OBJ_SIZE="$(cat "${SLAB_DIR}/object_size" 2>/dev/null || echo unknown)"
  SLAB_OBJS="$(cat "${SLAB_DIR}/objects" 2>/dev/null || echo unknown)"
  echo "[SLAB] kds_entry_cache: object_size=${OBJ_SIZE} bytes, active_objects=${SLAB_OBJS}"

  /usr/bin/kds_tester

  rmmod kds_store
  echo "[OK] Cycle ${i} unloaded cleanly"
  i=$((i + 1))
done

echo ""
echo "--- Snapshot of /proc/slabinfo (top 5 lines) ---"
head -n 5 /proc/slabinfo

echo ""
echo "--- Waiting 6s for kmemleak grace period, then scanning ---"
sleep 6
echo scan >"${KMEMLEAK}"
sleep 1
echo scan >"${KMEMLEAK}"

LEAK_REPORT="$(cat "${KMEMLEAK}")"
if [ -n "${LEAK_REPORT}" ]; then
  echo "[FAIL] kmemleak detected unreferenced allocations:" >&2
  echo "${LEAK_REPORT}" >&2
  exit 1
fi

echo "[PASS] kmemleak report is empty (0 leaks across ${CYCLES} full stress cycles)"
