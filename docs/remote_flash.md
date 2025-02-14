# Remote Flash Support via UART

## Overview
wolfBoot supports external partition emulation through UART communication, enabling:
- Remote flash storage
- Asynchronous updates
- Multi-processor architectures
- External update staging

## Configuration

### Build Options
```make
# Required settings
UART_FLASH=1   # Enable UART flash
EXT_FLASH=1    # Enable external flash API
```

### UART Driver Integration

#### Required HAL Functions
```c
/* Initialize UART with specified parameters */
int uart_init(
    uint32_t bitrate,  // Communication speed
    uint8_t data,      // Data bits
    char parity,       // Parity mode
    uint8_t stop       // Stop bits
);

/* Transmit single byte */
int uart_tx(
    const uint8_t c    // Byte to send
);

/* Receive single byte */
int uart_rx(
    uint8_t *c        // Buffer for received byte
);
```

#### Implementation Notes
- Example drivers: [hal/uart](hal/uart)
- Platform-specific adaptations needed
- Simple interface design
- Minimal dependencies


## Host Implementation

### UART Flash Server
```
Host System                      Target System
+----------------+   UART    +----------------+
| Flash Server   | <------> | wolfBoot       |
| - File storage |          | - Remote flash |
| - RPC handler  |          | - Update mgmt  |
+----------------+          +----------------+
```

#### Components
- Location: [tools/uart-flash-server](tools/uart-flash-server)
- Platform: GNU/Linux host
- Storage: Local file system
- Protocol: Custom UART messages

### Update Mechanism

#### Operation Model
| Operation | Implementation |
|-----------|---------------|
| Read | UART RPC → Host read |
| Write | UART RPC → Host write |
| Update | Standard wolfBoot flow |
| Rollback | Remote partition backup |

#### Key Features
- Transparent operation
- Standard update flow
- Remote storage support
- Automatic backup
- Rollback capability

#### Architecture Benefits
- Consistent API
- Flexible storage
- Reliable updates
- Host accessibility

## Related Documentation
- [Firmware Updates](firmware_update.md)
- [External Flash](flash_partitions.md)
- [HAL Integration](HAL.md)




