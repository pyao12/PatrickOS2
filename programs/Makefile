CPPC := clang++
LD   := ld.lld

BUILD_DIR ?= build

_FLAGS_COMMON := -m64 -nostdlib
FLAGS_CPP     := $(_FLAGS_COMMON) -ffreestanding -fpie -fno-exceptions -fno-rtti -fno-stack-protector -fcf-protection=none -mgeneral-regs-only -Wall -Wextra -I./include/
FLAGS_LD      := -m elf_x86_64 -pie --no-dynamic-linker -T program.ld

CPP_SOURCES := $(wildcard *.cpp)
OBJECTS     := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
ELFS        := $(patsubst %.cpp,$(BUILD_DIR)/%.elf,$(CPP_SOURCES))

all: $(ELFS)

$(BUILD_DIR)/%.o: %.cpp
	@echo "\033[32m[Compile Program]\033[0m from $< to $@"
	@mkdir -p $(dir $@)
	@$(CPPC) $(FLAGS_CPP) -c $< -o $@

$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.o program.ld
	@echo "\033[33m[Link Program]\033[0m Linking $@"
	@$(LD) $(FLAGS_LD) -o $@ $<

clean:
	@rm -rf $(BUILD_DIR)

.PHONY: all clean
