# Repository Guide

## Build And Verification

- This is a freestanding x86_64 OS: both kernel and programs use Clang/LLD with `-nostdlib`; do not assume a hosted C++ runtime or standard library.
- The image build also requires `sfdisk`, `mkfs.fat`, `grub-mkimage`, and `mcopy` (`mtools`).
- After every change, run `make clean` and then `make`; the root build compiles the kernel, every `programs/*.cpp`, and `build/pos2.img`.
- There is no automated test, lint, or formatter target. Do not use `make run`: its graphical QEMU launch fails in the agent environment.
- For runtime checks, launch headless QEMU with a timeout: `timeout 10s qemu-system-x86_64 -drive format=raw,file=build/pos2.img -serial file:serial.log --no-reboot -m 2048 --display none`. A timeout exit is expected; inspect only the first 30 lines of `serial.log`, never the whole file.
- For temporary boot tracing, use `serial_write_char()`/the serial helpers and remove diagnostic markers after debugging.

## Architecture

- `boot/boot.s` enters long mode and calls `kernel_main()` in `kernel/main.cpp`; initialization then mounts FAT32, starts console/input tasks, and runs the scheduler.
- Kernel headers live in `include/`; user programs are separate PIE ELF files built from top-level `programs/*.cpp` and copied into `/programs` in the FAT32 image.
- The program ABI is duplicated in `include/program.h` and `programs/include/program.h`. Keep their shared types in lockstep, and update the API mapping/syscall stubs in `kernel/program.cpp` when adding an operation.
- Source discovery is automatic: all `kernel/**/*.cpp` and every repository `*.s` feed the kernel build, while only top-level `programs/*.cpp` become user executables.

## C++ Style

- Keep opening braces on the declaration/control line.
- Attach `*` to the variable or function declarator, for example `char *text` and `void *allocate()`.
- Align types, names, and `=` across consecutive similar declarations or assignments, matching surrounding code.
