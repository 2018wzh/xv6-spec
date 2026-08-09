# xv6-spec Lab 4

This slice adds supervisor trap entry and the UART/PLIC device boundaries to the working bootstrap and memory system. `kernel/trap` owns the vector, dispatcher, drivers, console, and generated module tests. User-mode trap return and processes remain outside the Lab 4 boundary.
