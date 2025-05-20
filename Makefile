CC          	  := gcc
AS          	  := nasm
LD          	  := ld
OBJCOPY     	  := objcopy

CFLAGS      	  := -m32 -ffreestanding -c -g0 -fno-pie -fno-pic -fno-stack-protector -Os -Wall -fdata-sections -ffunction-sections
ASFLAGS     	  := -f elf32
LDFLAGS     	  := -T source/ld/link.ld -m elf_i386 -s --gc-sections

SRCDIR_C    	  := source/c
SRCDIR_ASM  	  := source/asm
BINDIR      	  := build
ISODIR      	  := build/iso
LIMINE_DIR        := build/limine

C_SRCS      	  := $(wildcard $(SRCDIR_C)/*.c)
ASM_SRCS    	  := $(wildcard $(SRCDIR_ASM)/*.asm)

C_OBJS      	  := $(patsubst $(SRCDIR_C)/%.c,$(BINDIR)/c/%.o,$(C_SRCS))
ASM_OBJS    	  := $(patsubst $(SRCDIR_ASM)/%.asm,$(BINDIR)/asm/%.o,$(ASM_SRCS))

OBJS        	  := $(ASM_OBJS) $(C_OBJS)

TARGET      	  := $(BINDIR)/kernel.bin
LIMINE_CFG        := $(ISODIR)/boot/limine.cfg
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

prepare-limine:
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
		mkdir -p $(LIMINE_DIR); \
		git clone https://github.com/limine-bootloader/limine.git --branch=v4.x-branch-binary --depth=1 $(LIMINE_DIR)/src; \
		$(MAKE) -C $(LIMINE_DIR)/src; \
	fi

$(LIMINE_CFG):
	@mkdir -p $(dir $@)
	@echo "TIMEOUT=0" > $@
	@echo "SERIAL=yes" >> $@
	@echo "" >> $@
	@echo ":MyOS" >> $@
	@echo "PROTOCOL=multiboot1" >> $@
	@echo "KERNEL_PATH=boot:///boot/kernel.bin" >> $@

$(ISO_IMAGE): $(KERNEL_IN_ISODIR) $(LIMINE_CFG) prepare-limine
	@mkdir -p $(dir $@)
	@mkdir -p $(ISODIR)/boot/limine
	cp $(LIMINE_DIR)/src/limine.sys $(ISODIR)/boot/limine/
	cp $(LIMINE_DIR)/src/limine-cd.bin $(ISODIR)/boot/limine/
	xorriso -as mkisofs -b boot/limine/limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-rational-rock -joliet -allow-lowercase \
		-iso-level 3 \
		-output-charset utf-8 \
		-quiet \
		-omit-version-number \
		-no-pad \
		$(ISODIR) -o $@
	$(LIMINE_DIR)/src/limine-deploy $@

diskimage: $(ISO_IMAGE)

iso: $(ISO_IMAGE)

run: $(ISO_IMAGE)
	qemu-system-i386 -cdrom $<

clean:
	rm -rf $(BINDIR)/c $(BINDIR)/asm $(BINDIR)/kernel.bin $(BINDIR)/myos.iso $(ISODIR)