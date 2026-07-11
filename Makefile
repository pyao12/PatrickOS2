CC   := clang
CPPC := clang++
ASC  := clang
LD   := ld.lld

_FLAGS_COMMON := -m64 -fno-pic -fno-pie -nostdlib
FLAGS_CPP     := $(_FLAGS_COMMON) -ffreestanding -fno-exceptions -mno-red-zone -fcf-protection=none -Wall -Wextra -I./include/
FLAGS_AS      := $(_FLAGS_COMMON)
FLAGS_LD      := -m elf_x86_64 -T linker.ld

QEMUFLAGS ?= -serial stdio --no-reboot -m 2048

BUILD_DIR        := build
PARTITION_OFFSET := 2048
PARTITION_OFFBYS := 1048576
GRUB_PC_DIR      := /usr/lib/grub/i386-pc

CPP_SOURCES := $(filter-out build/%,$(shell find . -name '*.cpp'))
ASM_SOURCES := $(filter-out build/%,$(shell find . -name '*.s'))
OBJECTS     := $(patsubst ./%.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES)) $(patsubst ./%.s,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

all: $(BUILD_DIR)/pos2.img

$(BUILD_DIR)/%.o: %.cpp
	@echo "\033[32m[Compile C++ File]\033[0m from $< to $@"
	@mkdir -p $(dir $@)
	@$(CPPC) $(FLAGS_CPP) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@echo "\033[34m[Compile AS File]\033[0m from $< to $@"
	@mkdir -p $(dir $@)
	@$(ASC) $(FLAGS_AS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(OBJECTS)
	@echo "\033[33m[Linker]\033[0m Linking kernel.elf"
	@mkdir -p $(dir $@)
	@$(LD) $(FLAGS_LD) -o $@ $(OBJECTS)

$(BUILD_DIR)/pos2.img: $(BUILD_DIR)/kernel.elf
	@echo "\033[36m[Other]\033[0m Making disk image..."
	
	@mkdir -p $(BUILD_DIR)/grub
	@dd if=/dev/zero of=$@ bs=1M count=64 status=none
	@printf "label: dos\nstart=$(PARTITION_OFFSET), type=c\n" | sfdisk $@ >/dev/null
	@mkfs.fat -F 32 --offset=$(PARTITION_OFFSET) $@ >/dev/null

	@rm -rf $(BUILD_DIR)/grubroot
	@mkdir -p $(BUILD_DIR)/grubroot/boot/grub
	@cp grub.cfg $(BUILD_DIR)/grubroot/boot/grub/grub.cfg
	@cp $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/grubroot/boot/kernel.elf

	@grub-mkimage --format=i386-pc --output=$(BUILD_DIR)/grub/core.img --prefix="(hd0,msdos1)/boot/grub" biosdisk part_msdos fat multiboot normal
	@dd if=$(GRUB_PC_DIR)/boot.img of=$@ bs=446 count=1 conv=notrunc status=none
	@dd if=$(BUILD_DIR)/grub/core.img of=$@ bs=512 seek=1 conv=notrunc status=none

	@mcopy -i $@@@$(PARTITION_OFFBYS) -s $(BUILD_DIR)/grubroot/* ::

run: $(BUILD_DIR)/pos2.img
	@echo "\033[36m[Other]\033[0m Launching QEMU..."
	@qemu-system-x86_64 -drive format=raw,file=$(BUILD_DIR)/pos2.img $(QEMUFLAGS) 

clean:
	@echo "Cleaning old files..."
	@rm -rf $(BUILD_DIR)

.PHONY: all run clean
