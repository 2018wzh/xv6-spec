# xv6-spec Lab 3

This slice adds a page-aligned physical allocator and the kernel's Sv39 mappings to the working bootstrap. `kernel/memory` owns allocator, mapping, spinlock, and generated module-test paths. The committed Spec deliberately excludes user processes and demand paging, which later labs introduce.
