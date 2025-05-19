# MyOS

Simple 32-bit operating system built with C and Assembly.

## Requirements

Install the following tools:

* `gcc` (with multilib support)
* `nasm`
* `ld`
* `grub-mkrescue`
* `qemu` (to run the OS)

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install gcc-multilib nasm grub-pc-bin xorriso qemu-system-x86
```

## Build and Run

To build the kernel and create a bootable ISO:

```bash
make
```

To run in QEMU:

```bash
make run
```

To clean build artifacts:

```bash
make clean
```

## File Structure

```
source/
  ├── asm/        # Assembly source files
  ├── c/          # C source files
  └── ld/         # Linker script
build/            # Build output (.o, .bin)
isodir/           # ISO image layout
Makefile
```

---
