K=kernel
OBJS = \
+  $K/entry.o \
+  $K/start.o \
+  $K/boot.o \
+  $K/main.o \
+  $K/string.o \
+  $K/kalloc.o \
+  $K/vm.o \
+  $K/console.o \
+  $K/printk.o \
+  $K/uart.o \
+  $K/plic.o

all: $K/kernel
$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) -T $K/kernel.ld -o $@ $(OBJS)

clean:
	rm -f $K/*.o $K/kernel
