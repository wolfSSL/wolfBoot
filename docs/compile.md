# wolfBoot Build System Guide

## Overview
wolfBoot provides a flexible build system supporting multiple embedded platforms through a modular Hardware Abstraction Layer (HAL). This guide explains the build configuration options and compilation process.

## Build Configuration Methods

### 1. Command Line Options
Set build options directly via environment variables:
```bash
make CORTEX_M0=1
```

### 2. Configuration File (.config)
Create a persistent configuration in the root directory:
```make
# Example .config entry
WOLFBOOT_PARTITION_BOOT_ADDRESS?=0x14000
```

**Note**: Command line options take precedence over `.config` entries when using `?=` operator.

## Configuration Setup

### Generate New Configuration
```bash
# Create default configuration
make config

# Follow prompts to configure:
# - Press Enter to accept default values [shown in brackets]
# - Enter new value to override defaults
```

The generated `.config` file can be:
- Used as default for subsequent `make` commands
- Modified manually with a text editor
- Overridden by command line options

## Platform Configuration

### Target Selection
Set target platform using `TARGET` variable:
```bash
make TARGET=stm32f4  # Default if unspecified
```

For supported platforms, see:
- [HAL Documentation](./HAL.md)
- [Platform Support Matrix](./Targets.md)

### Architecture-Specific Options
Default architecture support:
- ARM Cortex-M3/M4/M7

For other architectures:
```bash
# Cortex-M0 support
make CORTEX_M0=1

# See HAL documentation for other architecture options
```

### Adding New Platforms
1. Create HAL driver in [hal/](../hal/) directory
2. Add corresponding linker script
3. Implement required HAL functions

## Optimization Options

### Size vs Speed Tradeoffs

#### Assembly Optimizations
```bash
# Disable all assembly optimizations (smallest code)
make NO_ASM=1

# Disable ARM-specific optimizations only
make NO_ARM_ASM=1  # Keeps SP math optimizations
```

#### Performance Impact Example
ECC256 + SHA256 on STM32H7 (100KB image verification):

| Optimization Level | Configuration | Size (B) | Time (s) | Notes |
|-------------------|---------------|-----------|-----------|--------|
| Maximum Speed | `SIGN=ECC256` | 21,836 | 0.583 | Full assembly optimizations |
| Balanced | `SIGN=ECC256 NO_ARM_ASM=1` | 18,624 | 0.760 | ECC/SP math only |
| Minimum Size | `SIGN=ECC256 NO_ASM=1` | 14,416 | 3.356 | No assembly code |

**Note**: Disabling optimizations significantly impacts boot time but reduces code size.


## Flash Memory Configuration

### Partition Configuration
The flash memory layout is defined in [include/target.h](../include/target.h), generated based on the following configuration parameters:

| Parameter | Description | Usage |
|-----------|-------------|--------|
| `WOLFBOOT_SECTOR_SIZE` | Physical flash sector size | - Minimum swap unit<br>- Must be largest sector size if partitions have different geometries |
| `WOLFBOOT_PARTITION_SIZE` | Size of BOOT/UPDATE partitions | - Same size for both partitions<br>- Must accommodate firmware + headers |
| `WOLFBOOT_PARTITION_BOOT_ADDRESS` | Boot partition start address | - Must be sector-aligned<br>- App starts after header (256B) |
| `WOLFBOOT_PARTITION_UPDATE_ADDRESS` | Update partition start address | - Physical address for internal flash<br>- Offset for external flash |
| `WOLFBOOT_PARTITION_SWAP_ADDRESS` | Swap area start address | - Used for reversible updates<br>- Size = one sector |

### Memory Layout Example
```
Flash Memory
+------------------------+ <- 0x00000000
|      wolfBoot         |
+------------------------+ <- WOLFBOOT_PARTITION_BOOT_ADDRESS
| Header |   Firmware   |    Boot Partition
+------------------------+ <- WOLFBOOT_PARTITION_UPDATE_ADDRESS
| Header |   Update     |    Update Partition
+------------------------+ <- WOLFBOOT_PARTITION_SWAP_ADDRESS
|        Swap           |    Swap Area (1 sector)
+------------------------+
```

### Configuration Methods
Set parameters via:
1. Command line:
```bash
make WOLFBOOT_SECTOR_SIZE=0x1000 WOLFBOOT_PARTITION_SIZE=0x20000
```

2. .config file:
```make
WOLFBOOT_SECTOR_SIZE?=0x1000
WOLFBOOT_PARTITION_SIZE?=0x20000
```

## Bootloader Configuration

### Digital Signature Algorithms

wolfBoot supports multiple signature algorithms with different security and performance characteristics.

#### Available Algorithms

| Algorithm | Command | Key Size | Performance | Size Impact |
|-----------|---------|-----------|-------------|-------------|
| Ed25519 (default) | `SIGN=ED25519` | 256-bit | Good | Smallest |
| ECDSA P-256 | `SIGN=ECC256` | 256-bit | Better | Medium |
| ECDSA P-384 | `SIGN=ECC384` | 384-bit | Good | Medium |
| ECDSA P-521 | `SIGN=ECC521` | 521-bit | Good | Larger |
| RSA-2048 | `SIGN=RSA2048` | 2048-bit | Good | Large |
| RSA-3072 | `SIGN=RSA3072` | 3072-bit | Moderate | Larger |
| RSA-4096 | `SIGN=RSA4096` | 4096-bit | Moderate | Largest |
| Ed448 | `SIGN=ED448` | 448-bit | Good | Medium |
| No Authentication | `SIGN=NONE` | N/A | Best | Minimal |

#### Usage Examples
```bash
# Use Ed25519 (default)
make

# Use ECDSA P-256
make SIGN=ECC256

# Use RSA-2048
make SIGN=RSA2048

# Disable authentication
make SIGN=NONE
```

#### Key Generation
- Each algorithm requires specific key generation tools
- Tools available in [tools/](../tools/) directory
- See [Signing.md](Signing.md) for detailed key management instructions

**Warning**: Using `SIGN=NONE` disables secure boot authentication

### Advanced Features

#### Incremental Updates
Enable delta-based firmware updates to minimize update size and flash operations.

```bash
# Enable incremental updates
make DELTA_UPDATES=1

# Generate delta update
./tools/keytools/sign --delta old.bin new.bin key.der version
```

For detailed information, see [Firmware Updates](firmware_update.md).

#### Debug Support
Enable debugging features and symbols:
```bash
make DEBUG=1
```

**Important**: Debug build increases bootloader size significantly. Ensure sufficient flash space before `WOLFBOOT_PARTITION_BOOT_ADDRESS`.

#### Interrupt Vector Configuration
Control interrupt vector table (IVT) relocation:

| Option | Description | Use Case |
|--------|-------------|----------|
| Default | Relocates IVT via VTOR | Standard operation |
| `VTOR=0` | Disables IVT relocation | - External IVT management<br>- Platforms without VTOR<br>- Custom relocation schemes |

```bash
# Disable vector table relocation
make VTOR=0
```

### Memory Management

#### Stack Configuration Options

wolfBoot provides flexible stack management options to accommodate different memory constraints:

| Option | Description | Use Case |
|--------|-------------|----------|
| Default | Stack-only operation | - No dynamic allocation<br>- Predictable memory usage |
| `WOLFBOOT_SMALL_STACK=1` | Static memory pool | - Limited RAM systems<br>- Dedicated bootloader RAM |
| `WOLFBOOT_HUGE_STACK=1` | Large stack allowed | - Systems with ample RAM<br>- Complex crypto operations |

#### Memory Usage Characteristics

1. **Default Mode**
   - Uses stack for all operations
   - No dynamic memory allocation
   - Stack usage varies by algorithm
   - Predictable at compile time

2. **Small Stack Mode**
   ```bash
   make WOLFBOOT_SMALL_STACK=1
   ```
   - Creates fixed-size memory pool
   - Reduces runtime stack usage
   - Uses static allocation
   - Simulates dynamic allocation
   - Suitable for RAM-constrained systems

3. **Large Stack Mode**
   ```bash
   make WOLFBOOT_HUGE_STACK=1
   ```
   - Bypasses stack size checks
   - Required for some crypto configs
   - Use when RAM is abundant
   - Enables complex operations

**Warning**: Large stack mode may cause issues on RAM-constrained systems

### Safety and Recovery Features

#### Firmware Backup Control
Control backup behavior during updates:

```bash
# Disable firmware backup
make DISABLE_BACKUP=1
```

| Mode | Description | Use Case | Risk Level |
|------|-------------|----------|------------|
| Default | Creates backup copy | Normal operation | Safe |
| `DISABLE_BACKUP=1` | No backup created | - Limited flash space<br>- Read-only update partition | High |

**Warning**: Disabling backup removes fallback protection against failed updates

#### Flash Memory Handling

##### Write-Once Flash Support
For flash that doesn't support multiple writes between erases:

```bash
# Enable write-once flash support
make NVM_FLASH_WRITEONCE=1
```

**Important Notes**:
- Required for some microcontrollers
- Affects flag field operations
- Compromises fail-safe swap guarantee
- Power loss during swap may corrupt firmware

##### Version Control

Control firmware version enforcement:

| Option | Command | Behavior | Security Impact |
|--------|---------|----------|-----------------|
| Default | N/A | Prevents downgrades | Secure |
| `ALLOW_DOWNGRADE=1` | `make ALLOW_DOWNGRADE=1` | Allows older versions | Vulnerable to downgrade attacks |

**Security Warning**: 
- Enabling downgrades bypasses version checks
- May expose system to malicious downgrades
- Use only in development/testing

### External Flash Support

#### Overview
wolfBoot supports external flash memory for storing firmware updates and swap partitions, with configurable partition mapping and access methods.

#### Configuration Options

| Option | Description | Default | Usage |
|--------|-------------|---------|--------|
| `EXT_FLASH=1` | Enable external flash | No | Basic external flash support |
| `PART_UPDATE_EXT` | Map update to external | Yes* | Store updates externally |
| `PART_SWAP_EXT` | Map swap to external | Yes* | Use external swap space |
| `PART_BOOT_EXT` | Map boot to external | No** | For non-XIP systems |
| `NO_XIP=1` | Disable execute-in-place | No | For MMU systems |

\* When `EXT_FLASH=1`
\** Unless `NO_XIP=1`

#### Memory Configurations

1. **Standard External Flash**
   ```bash
   make EXT_FLASH=1
   ```
   - Updates and swap in external flash
   - Boot partition in internal flash
   - Requires HAL extension

2. **Non-XIP Systems** (e.g., Cortex-A)
   ```bash
   make EXT_FLASH=1 NO_XIP=1
   ```
   - All partitions in external flash
   - Images loaded to RAM for execution
   - Supports position-independent code

#### Implementation Requirements

1. **HAL Extension**
   - Implement `ext_flash_*` API
   - See [HAL Documentation](HAL.md)
   - Handle custom memory access

2. **Compatibility Notes**
   - Incompatible with `NVM_FLASH_WRITEONCE`
   - Use HAL layer for special flash handling
   - Consider word size requirements

For an example of using `EXT_FLASH` to bypass read restrictions, (in this case, the inability to read from
erased flash due to ECC errors) on a platform with write-once flash, see the [infineon tricore port](../hal/aurix_tc3xx.c).

### External Memory Interfaces

#### SPI Flash Support

Enable SPI flash memory integration:
```bash
make EXT_FLASH=1 SPI_FLASH=1
```

**Features**:
- Automatic ext_flash API mapping
- Platform-specific SPI drivers
- Multiple platform support

**Implementation**:
1. Define SPI functions
2. See examples in [hal/spi/](../hal/spi/)
3. No manual ext_flash_* implementation needed

#### UART Bridge Support

Enable UART-based external memory access:
```bash
make EXT_FLASH=1 UART_FLASH=1
```

**Capabilities**:
- Remote flash access
- Neighbor system integration
- Protocol-based communication

**Requirements**:
- Compatible neighbor service
- wolfBoot protocol support
- UART interface availability

For detailed UART implementation, see:
- [Remote Flash Support](remote_flash.md)
- [Protocol Specification](firmware_update.md)

### Advanced Memory Features

#### External Partition Encryption

Enable ChaCha20 encryption for external partitions:
```bash
make EXT_FLASH=1 [SPI_FLASH=1|UART_FLASH=1]
```

**Requirements**:
- External flash configuration
- Pre-encrypted update images
- ChaCha20 symmetric key setup

**Compatibility**:
- Works with SPI flash
- Works with UART bridge
- Supports custom mappings

For implementation details, see [Encrypted Partitions](encrypted_partitions.md).

#### RAM-Based Flash Operations

Execute flash access code from RAM:
```bash
make RAM_CODE=1
```

**Use Cases**:
- Avoid flash conflicts
- Self-modifying operations
- Flash configuration changes

**Important**: Required for some hardware configurations to enable flash write access.

#### Hardware-Assisted Updates

Enable dual-bank swap support:
```bash
make DUALBANK_SWAP=1
```

**Features**:
- Hardware-assisted updates
- Efficient bank switching
- Reduced update time

**Support**:
- STM32F76x series
- STM32F77x series
- Future platform support planned


### Store UPDATE partition flags in a sector in the BOOT partition

By default, wolfBoot keeps track of the status of the update procedure to the single sectors in a specific area at the end of each partition, dedicated
to store and retrieve a set of flags associated to the partition itself.

In some cases it might be helpful to store the status flags related to the UPDATE partition and its sectors in the internal flash, alongside with
the same set of flags used for the BOOT partition. By compiling wolfBoot with the `FLAGS_HOME=1` makefile option, the flags
associated to the UPDATE partition are stored in the BOOT partition itself.

While on one hand this option slightly reduces the space available in the BOOT partition to store the firmware image, it keeps all the flags in
the BOOT partition.

### Flash Erase value / Flag logic inversion

By default, most NVMs set the content of erased pages to `0xFF` (all ones).

Some FLASH memory models use inverted logic for erased page, setting the content to `0x00` (all zeroes) after erase.

For these special cases, the option `FLAGS_INVERT = 1` can be used to modify the logic of the partition/sector flags used in wolfBoot.

You can also manually override the fill bytes using `FILL_BYTE=` at build-time. It default to `0xFF`, but will use `0x00` if `FLAGS_INVERT` is set.

Note: if you are using an external FLASH (e.g. SPI) in combination with a flash with inverted logic, ensure that you store all the flags in one partition, by using the `FLAGS_HOME=1` option described above.

### Using One-time programmable (OTP) flash as keystore

By default, keys are directly incorporated in the firmware image. To store the keys in a separate, one-time programmable (OTP) flash memory, use the `FLASH_OTP_KEYSTORE=1` option.
For more information, see [/docs/OTP-keystore.md](/docs/OTP-keystore.md).

### Prefer multi-sector flash erase operations

wolfBoot HAL flash erase function must be able to handle erase lengths larger than `WOLFBOOT_SECTOR_SIZE`, even if the underlying flash controller does not. However, in some cases, wolfBoot defaults to
iterating over a range of flash sectors and erasing them one at a time. Setting the `FLASH_MULTI_SECTOR_ERASE=1` config option prevents this behavior when possible, configuring wolfBoot to instead prefer a
single HAL flash erase invocation with a larger erase length versus the iterative approach. On targets where multi-sector erases are more performant, this option can be used to dramatically speed up the
image swap procedure.

### Platform-Specific Notes

#### macOS Build Environment

##### Unicode Character Issues
When building on macOS, you may encounter Unicode-related issues in binary files:
- Symptom: 0xC3 0xBF (C3BF) patterns in factory.bin
- Cause: macOS Unicode locale settings
- Affects: Binary padding operations

##### Solution
Set C locale in terminal:
```bash
# Required environment variables
export LANG=
export LC_COLLATE="C"
export LC_CTYPE="C"
export LC_MESSAGES="C"
export LC_MONETARY="C"
export LC_NUMERIC="C"
export LC_TIME="C"
export LC_ALL=
```

**Workflow**:
1. Set environment variables
2. Run make commands normally
3. Verify binary output

For more platform-specific details, see:
- [Build System](HAL.md)
- [Troubleshooting](firmware_update.md)

