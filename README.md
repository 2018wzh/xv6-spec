# xv6-spec Lab 2

This slice adds the first machine-to-supervisor bootstrap and its minimal RISC-V build projection. `kernel/boot` owns the early entry, PMP transition, byte-addressed UART publication, and generated module tests. `toolchain` owns the structured build projection and capability/clean-rebuild checks. Later course mechanisms remain intentionally absent.
