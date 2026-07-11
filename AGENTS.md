# Repository Guidelines

## Project Structure & Module Organization

This is a 64-bit x86 educational operating-system kernel. `boot/boot.s` contains the Multiboot entry point, early paging, and the transition to long mode. Kernel implementations live in `kernel/`, with the framebuffer code in `kernel/graphics/` and console code in `kernel/console/`. Public interfaces and shared types belong in `include/`, mirroring implementation areas where practical (for example, `include/graphics/`). `grub.cfg` and `linker.ld` define boot and link behavior. Generated objects, the ELF, and disk image are written to `build/`; never edit or commit that directory.

## Build, Test, and Development Commands

- `make`: compiles all `*.cpp` and `*.s` sources, links `build/kernel.elf`, and creates `build/pos2.img`.
- `make run`: builds as needed, then boots the raw image with `qemu-system-x86_64`.
- `make clean`: removes all generated build output before a clean rebuild.

The build requires `clang`, `clang++`, `ld.lld`, GRUB image tools, FAT image tools (`mkfs.fat`, `mcopy`), `sfdisk`, and QEMU. There is currently no automated test suite; validate behavior by rebuilding and booting the image after kernel or boot changes.

## Coding Style & Naming Conventions

Use four-space indentation and braces on the same line as control statements and function declarations, matching existing C++ sources. Prefer the project's fixed-width aliases from `include/common.h` (`ui8`, `ui32`, `ui64`, `uip`) for kernel-facing data. Use `snake_case` for functions, variables, filenames, and C-style types; use descriptive module names such as `scheduler_context_t`. Keep headers self-contained, declarations in `include/`, and implementation in the corresponding `kernel/` area.

This is freestanding code: do not introduce standard-library, runtime, dynamic-allocation, exceptions, or host OS dependencies without updating the build and boot design. Preserve required ABI details in assembly and naked context-switch routines.

## Testing Guidelines

For each change, run `make` and `make run`. Exercise the affected boot or kernel path in QEMU and check for successful startup, expected framebuffer output, and scheduler behavior where relevant. Add a small focused kernel test or diagnostic task when changing low-level algorithms; remove temporary diagnostics before submitting.

## Commit & Pull Request Guidelines

Recent commits use Conventional Commit-style prefixes, commonly `feat:` and `chore:`; write concise imperative summaries, for example `feat: add round-robin task cleanup`. Keep commits scoped to one logical change. Pull requests should describe the boot-visible behavior, link related issues when applicable, and include QEMU screenshots or serial output for graphics, console, scheduler, or boot-flow changes. Note required tooling or configuration changes explicitly.
