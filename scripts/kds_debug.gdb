# GDB helper commands for live inspection of kds_store intrusive containers
# Usage (after 'lx-symbols module'):
#   source scripts/kds_debug.gdb
#   kds-info
#   kds-dump-list
#   kds-dump-rbroot

define kds-info
    printf "=== g_store summary ===\n"
    printf "  entry_count   = %u / %u\n", g_store.entry_count, g_store.max_entries
    printf "  slab cache    = %s (object_size=%u)\n", g_store.cache->name, g_store.cache->object_size
    printf "  allocs/frees  = %llu / %llu\n", g_store.stats.alloc_count, g_store.stats.free_count
    printf "  hits/misses   = %llu / %llu\n", g_store.stats.lookup_hits, g_store.stats.lookup_misses
end

define kds-dump-list
    printf "=== Intrusive list_head walk (via $container_of) ===\n"
    set $head = &g_store.entry_list
    set $cur = $head->next
    set $idx = 0
    while $cur != $head
        set $e = $container_of($cur, "struct kds_entry", "list_node")
        printf "  [%d] entry=%p id=%u value=%lld tag=\"%s\" (tag_ptr=%p)\n", \
               $idx, $e, $e->id, $e->value, $e->tag, $e->tag
        set $cur = $cur->next
        set $idx = $idx + 1
    end
end

define kds-dump-rbroot
    printf "=== Intrusive rbtree root inspection ===\n"
    set $root = g_store.entry_tree.rb_node
    if $root == 0
        printf "  (empty tree)\n"
    else
        set $e = $container_of($root, "struct kds_entry", "rb_node")
        printf "  root=%p id=%u value=%lld tag=\"%s\" left=%p right=%p\n", \
               $e, $e->id, $e->value, $e->tag, $root->rb_left, $root->rb_right
    end
end
