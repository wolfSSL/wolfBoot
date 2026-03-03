# Hooks

wolfBoot provides a hooks framework that allows users to inject custom logic at
well-defined points in the boot process without modifying wolfBoot source code.
Each hook is independently enabled via its own build-time macro and compiled in
from a single user-provided source file.

Typical use cases include board-specific hardware setup not covered in the
default HAL, debug logging, watchdog management, setting a safe state on boot
failure, etc.

## Available Hooks

| Hook | Macro | Signature | When Called |
|------|-------|-----------|------------|
| Preinit | `WOLFBOOT_HOOK_LOADER_PREINIT` | `void wolfBoot_hook_preinit(void)` | Before `hal_init()` in the loader |
| Postinit | `WOLFBOOT_HOOK_LOADER_POSTINIT` | `void wolfBoot_hook_postinit(void)` | After all loader initialization, just before `wolfBoot_start()` |
| Boot | `WOLFBOOT_HOOK_BOOT` | `void wolfBoot_hook_boot(struct wolfBoot_image *boot_img)` | After `hal_prepare_boot()` but before `do_boot()` |
| Panic | `WOLFBOOT_HOOK_PANIC` | `void wolfBoot_hook_panic(void)` | Inside `wolfBoot_panic()`, before halt |

## Boot Flow

The following diagram shows where each hook fires in the wolfBoot boot sequence:

```
loader main()
  |
  +-- [HOOK: wolfBoot_hook_preinit()]     <-- WOLFBOOT_HOOK_LOADER_PREINIT
  |
  +-- hal_init()
  +-- other initialization...
  |
  +-- [HOOK: wolfBoot_hook_postinit()]    <-- WOLFBOOT_HOOK_LOADER_POSTINIT
  |
  +-- wolfBoot_start()
        |
        +-- (image verification, update logic)
        |
        +-- hal_prepare_boot()
        |
        +-- [HOOK: wolfBoot_hook_boot()]  <-- WOLFBOOT_HOOK_BOOT
        |
        +-- do_boot()

wolfBoot_panic()  (called on any fatal error)
  |
  +-- [HOOK: wolfBoot_hook_panic()]      <-- WOLFBOOT_HOOK_PANIC
  |
  +-- halt / infinite loop
```

## Build Configuration

First, enable hooks in your `.config`:

```makefile
# Path to a single .c file containing your hook implementations
WOLFBOOT_HOOKS_FILE=path/to/my_hooks.c

# Enable individual hooks (each is independent)
WOLFBOOT_HOOK_LOADER_PREINIT=1
WOLFBOOT_HOOK_LOADER_POSTINIT=1
WOLFBOOT_HOOK_BOOT=1
WOLFBOOT_HOOK_PANIC=1
```

Or pass them on the `make` command line:

```bash
make WOLFBOOT_HOOKS_FILE=my_hooks.c WOLFBOOT_HOOK_LOADER_PREINIT=1
```

## Notes

- `WOLFBOOT_HOOKS_FILE` tells the build system to compile and link your hooks
  source file. The resulting object file is added to the wolfBoot binary.
- Hook prototypes are declared in `include/hooks.h`.
- Each hook is independently enabled. You only need to implement the hooks you
  wish to enable in your build. Additionally, all hooks are optional. If no
  `WOLFBOOT_HOOK_*` macros are defined, the boot flow is unchanged.
- The preinit hook runs before any hardware initialization. Be careful to only
  use functionality that does not depend on `hal_init()` having been called.
- The boot hook receives a pointer to the verified `wolfBoot_image` struct,
  allowing inspection of firmware version, type, and other metadata before boot.
- The panic hook fires inside `wolfBoot_panic()` just before the system halts.
  Use it to set the system to a safe state, log errors, toggle GPIOs, notify
  external systems, etc.
- **Note (WOLFBOOT_ARMORED):** When armored glitch protection is enabled, the
  panic hook is called on a best-effort basis before the glitch-hardened halt
  loop. The hook itself is not glitch-protected — a fault during the hook call
  could skip it entirely. The halt loop remains hardened regardless.
