/* acpi_dsdt.asl
 *
 * Minimal DSDT for the NAI 68INT6 (Tiger Lake UP3), handed to Linux by
 * wolfBoot's ACPI generator. Declares the PCI root bus so the OS enumerates
 * it under ACPI, with a _PRT that routes PCI INTx to the IOAPIC GSIs the
 * platform actually uses. The MADT/MCFG/FADT are built in C (acpi.c); only
 * this AML needs the iasl toolchain, so it is compiled offline and committed
 * as a C array.
 *
 * Hand-authored (not derived from another firmware source); the _HID PNP0A08 /
 * _CID PNP0A03 host bridge, _CRS and _PRT objects follow the ACPI
 * Specification (6.x) section 6.2 and the PCI Firmware Specification.
 *
 * Regenerate after editing:
 *   iasl src/x86/acpi_dsdt.asl
 *   xxd -i -n acpi_dsdt_aml src/x86/acpi_dsdt.aml > include/x86/acpi_dsdt.h
 *   (then hand-add the wolfSSL GPL header to acpi_dsdt.h)
 *
 * _PRT routing (pin 0=INTA..3=INTD, GSI in the source-index field) is the
 * applied routing measured on the golden bank (lspci -vvnn): fixed-function
 * PCH devices swizzle INTA->GSI16 / INTB->GSI17, while the SerialIO devices
 * (0x15/0x19/0x1e) use dedicated GSIs. _CRS advertises wolfBoot's own PCI
 * windows (PCI_MMIO32_BASE upward, ECAM at 0xC0000000 via MCFG, IO from
 * PCI_IO32_BASE) and the 0..0x17 bus range that the MCFG covers.
 */
DefinitionBlock ("", "DSDT", 2, "WOLFBT", "NAITGL", 0x00000001)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))  /* PCIe host bridge */
            Name (_CID, EisaId ("PNP0A03"))  /* PCI host bridge compat */
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Name (_UID, Zero)

            Method (_PRT, 0, NotSerialized)
            {
                Return (Package ()
                {
                    Package () { 0x0002FFFF, 0, Zero, 16 },  /* iGPU  INTA */
                    Package () { 0x0004FFFF, 0, Zero, 16 },  /* DTT   INTA */
                    Package () { 0x000DFFFF, 0, Zero, 16 },  /* TBT   INTA */
                    Package () { 0x0014FFFF, 0, Zero, 16 },  /* xHCI  INTA */
                    Package () { 0x0014FFFF, 1, Zero, 17 },  /* xHCI  INTB */
                    Package () { 0x0015FFFF, 0, Zero, 27 },  /* I2C0  INTA */
                    Package () { 0x0015FFFF, 1, Zero, 40 },  /* I2C1  INTB */
                    Package () { 0x0016FFFF, 0, Zero, 16 },  /* HECI  INTA */
                    Package () { 0x0017FFFF, 0, Zero, 16 },  /* SATA  INTA */
                    Package () { 0x0019FFFF, 0, Zero, 31 },  /* I2C4  INTA */
                    Package () { 0x0019FFFF, 2, Zero, 33 },  /* UART2 INTC */
                    Package () { 0x001CFFFF, 0, Zero, 16 },  /* RP1   INTA */
                    Package () { 0x001CFFFF, 1, Zero, 17 },  /* RP    INTB */
                    Package () { 0x001CFFFF, 2, Zero, 18 },  /* RP    INTC */
                    Package () { 0x001CFFFF, 3, Zero, 19 },  /* RP    INTD */
                    Package () { 0x001DFFFF, 0, Zero, 16 },  /* RP9   INTA */
                    Package () { 0x001DFFFF, 1, Zero, 17 },  /* RP10  INTB */
                    Package () { 0x001DFFFF, 2, Zero, 18 },  /* RP    INTC */
                    Package () { 0x001DFFFF, 3, Zero, 19 },  /* RP    INTD */
                    Package () { 0x001EFFFF, 0, Zero, 16 },  /* UART0 INTA */
                    Package () { 0x001EFFFF, 1, Zero, 17 },  /* UART1 INTB */
                    Package () { 0x001EFFFF, 2, Zero, 36 },  /* GSPI  INTC */
                    Package () { 0x001FFFFF, 0, Zero, 16 },  /* PCH   INTA */
                })
            }

            Name (_CRS, ResourceTemplate ()
            {
                /* Bus range covered by the MCFG (bus 0..0x17). */
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0x0000, 0x0000, 0x0017, 0x0000, 0x0018)

                /* Legacy IO below the PCI config ports, then the rest of IO
                 * space; wolfBoot assigns device IO from PCI_IO32_BASE. */
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0000, 0x0CF7, 0x0000, 0x0CF8)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0D00, 0xFFFF, 0x0000, 0xF300)

                /* Low 1 MB (VGA/legacy). */
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite,
                    0x00000000, 0x000A0000, 0x000BFFFF, 0x00000000, 0x00020000)

                /* 32-bit device MMIO window: PCI_MMIO32_BASE (0x80000000) up
                 * to just below the ECAM region at 0xC0000000. */
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0x80000000, 0xBFFFFFFF, 0x00000000, 0x40000000)

                /* 64-bit high MMIO window for reassignable device BARs. The FSP
                 * assigns the LPSS BARs low, inside an FSP-reserved MMIO range
                 * (0xFC800000-0xFE7FFFFF) that the kernel refuses to claim a BAR
                 * over, so the LPSS UART never binds and there is no real ttyS.
                 * Advertising this window - the same 0x40_00000000-0x7F_FFFFFFFF
                 * range the stock BIOS exposes - lets the kernel reassign those
                 * BARs high, matching stock, so UART2 enumerates as ttyS4. */
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000004000000000, 0x0000007FFFFFFFFF,
                    0x0000000000000000, 0x0000004000000000)
            })
        }
    }
}
