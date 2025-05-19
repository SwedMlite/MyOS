CC          	  := gcc
AS          	  := nasm
LD          	  := ld
OBJCOPY     	  := objcopy

CFLAGS      	  := -m32 -ffreestanding -c -g -fno-pie -fno-pic -Wall
ASFLAGS     	  := -f elf32
LDFLAGS     	  := -T source/ld/link.ld -m elf_i386

SRCDIR_C    	  := source/c
SRCDIR_ASM  	  := source/asm
BINDIR      	  := build
ISODIR      	  := isodir

C_SRCS      	  := $(wildcard $(SRCDIR_C)/*.c)
ASM_SRCS    	  := $(wildcard $(SRCDIR_ASM)/*.asm)

C_OBJS      	  := $(patsubst $(SRCDIR_C)/%.c,$(BINDIR)/c/%.o,$(C_SRCS))
ASM_OBJS    	  := $(patsubst $(SRCDIR_ASM)/%.asm,$(BINDIR)/asm/%.o,$(ASM_SRCS))

OBJS        	  := $(ASM_OBJS) $(C_OBJS)

TARGET      	  := $(BINDIR)/kernel.bin
GRUB_CFG          := $(ISODIR)/boot/grub/grub.cfg
ISO_IMAGE         := $(BINDIR)/myos.iso
KERNEL_IN_ISODIR  := $(ISODIR)/boot/kernel.bin

.PHONY: all diskimage iso run clean

all: diskimage

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $^ -o $@

$(BINDIR)/c/%.o: $(SRCDIR_C)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(BINDIR)/asm/%.o: $(SRCDIR_ASM)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL_IN_ISODIR): $(TARGET)
	@mkdir -p $(dir $@)
	cp $< $@

$(GRUB_CFG):
	@mkdir -p $(dir $@)
	echo "set timeout=0"           	   > $(GRUB_CFG)
	echo "set default=0"           	   >> $(GRUB_CFG)
	echo ""                        	   >> $(GRUB_CFG)
	echo "menuentry \"My OS\" {"   	   >> $(GRUB_CFG)
	echo " multiboot /boot/kernel.bin" >> $(GRUB_CFG)
	echo "    boot"                	   >> $(GRUB_CFG)
	echo "}"                       	   >> $(GRUB_CFG)

$(ISO_IMAGE): $(KERNEL_IN_ISODIR) $(GRUB_CFG)
	grub-mkrescue -o $@ $(ISODIR)

diskimage: $(ISO_IMAGE)

iso: $(ISO_IMAGE)

run: $(ISO_IMAGE)
	qemu-system-i386 -cdrom $<

clean:
	rm -rf $(BINDIR)
