# MyOS

Simple 32-bit operating system built with C and Assembly.

## Requirements

Install the following tools:

* `gcc` (with multilib support)
* `nasm`
* `ld`
* `xorriso`
* `git` (to fetch Limine bootloader)
* `qemu` (to run the OS)

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install gcc-multilib nasm xorriso git qemu-system-x86
```

On Arch:
```bash
sudo pacman -S gcc-multilib nasm xorriso git qemu
```
> Note: select qemu-full while installing

## Build and Run

To build the kernel and create a bootable ISO with Limine bootloader:

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

---