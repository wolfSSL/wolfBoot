/* acpi_dsdt.asl
 *
 * Minimal DSDT for the NAI 68INT6: the PCI root bus with its _CRS and the
 * _PRT INTx routing, plus the board devices on LPSS I2C0 and GSPI0.
 * Compiled offline with iasl and committed as a C array.
 *
 * Regenerate after editing:
 *   iasl src/x86/acpi_dsdt.asl
 *   xxd -i -n acpi_dsdt_aml src/x86/acpi_dsdt.aml > include/x86/acpi_dsdt.h
 *   (then hand-add the wolfSSL GPL header to acpi_dsdt.h)
 *
 * _PRT (pin 0=INTA..3=INTD -> GSI) is the FSP's default PCH routing, as
 * measured on the stock BIOS. _CRS covers wolfBoot's PCI windows and buses.
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
                    Package () { 0x0006FFFF, 0, Zero, 16 },  /* PEG60 INTA */
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

            /* The OS gets every PCIe feature it asks for (native hot-plug,
             * PME, AER, capability structure, LTR): no firmware handler owns
             * them after handoff */
            Method (_OSC, 4, Serialized)
            {
                Local0 = Arg3
                CreateDWordField (Local0, Zero, CDW1)
                If ((Arg0 == ToUUID ("33db4d5b-1ff7-401c-9657-7441c03dd766")))
                {
                    If ((Arg1 != One))
                    {
                        CDW1 |= 0x08
                    }
                }
                Else
                {
                    CDW1 |= 0x04
                }
                Return (Local0)
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

                /* 64-bit window (as stock) so the kernel can move the LPSS BARs
                 * out of the FSP-reserved range; UART2 then binds as ttyS4. */
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000004000000000, 0x0000007FFFFFFFFF,
                    0x0000000000000000, 0x0000004000000000)
            })

            /* Root ports with the INTx routing of the devices behind them, as
             * the stock BIOS declares it; RP10's differs from a plain swizzle. */
            Device (PEG0)  /* 00:06.0 */
            {
                Name (_ADR, 0x00060000)
                Name (_PRT, Package ()
                {
                    Package () { 0xFFFF, 0, Zero, 16 },
                    Package () { 0xFFFF, 1, Zero, 17 },
                    Package () { 0xFFFF, 2, Zero, 18 },
                    Package () { 0xFFFF, 3, Zero, 19 },
                })
            }

            Device (RP05)  /* 00:1c.0, PCH port 5 */
            {
                Name (_ADR, 0x001C0000)
                Name (_PRT, Package ()
                {
                    Package () { 0xFFFF, 0, Zero, 16 },
                    Package () { 0xFFFF, 1, Zero, 17 },
                    Package () { 0xFFFF, 2, Zero, 18 },
                    Package () { 0xFFFF, 3, Zero, 19 },
                })
            }

            Device (RP09)  /* 00:1d.0, PCH port 9 */
            {
                Name (_ADR, 0x001D0000)
                Name (_PRT, Package ()
                {
                    Package () { 0xFFFF, 0, Zero, 16 },
                    Package () { 0xFFFF, 1, Zero, 17 },
                    Package () { 0xFFFF, 2, Zero, 18 },
                    Package () { 0xFFFF, 3, Zero, 19 },
                })
            }

            Device (RP10)  /* 00:1d.1, PCH port 10 */
            {
                Name (_ADR, 0x001D0001)
                Name (_PRT, Package ()
                {
                    Package () { 0xFFFF, 0, Zero, 17 },
                    Package () { 0xFFFF, 1, Zero, 18 },
                    Package () { 0xFFFF, 2, Zero, 19 },
                    Package () { 0xFFFF, 3, Zero, 16 },
                })
            }

            /* LPSS I2C0 and GSPI0 with the board devices the stock BIOS
             * declares, so NAI's Linux drivers (naigspi, at24) bind. */
            Device (I2C0)
            {
                Name (_ADR, 0x00150000)

                Device (EEP2)
                {
                    Name (_HID, "15AC0002")
                    Name (_CID, "PRP0001")
                    Name (_DDN, "NAI Motherboard EEPROM")
                    Name (_CRS, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x0056, ControllerInitiated, 100000,
                            AddressingMode7Bit, "\\_SB.PCI0.I2C0")
                    })
                    Name (_DSD, Package ()
                    {
                        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                        Package ()
                        {
                            Package () { "compatible", "atmel,24c02" },
                            Package () { "size", 256 },
                            Package () { "pagesize", 8 }
                        }
                    })
                }

                Device (EEP3)
                {
                    Name (_HID, "15AC0003")
                    Name (_CID, "PRP0001")
                    Name (_DDN, "NAI Motherboard EEPROM SN")
                    Name (_CRS, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x005E, ControllerInitiated, 100000,
                            AddressingMode7Bit, "\\_SB.PCI0.I2C0")
                    })
                    Name (_DSD, Package ()
                    {
                        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                        Package ()
                        {
                            Package () { "compatible", "atmel,24cs02" },
                            Package () { "size", 16 },
                            Package () { "pagesize", 8 },
                            Package () { "read-only", One }
                        }
                    })
                }

                Device (RTC1)
                {
                    Name (_HID, "15AC0004")
                    Name (_CID, "PRP0001")
                    Name (_DDN, "NAI Motherboard WDT RTC")
                    Name (_CRS, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x0068, ControllerInitiated, 100000,
                            AddressingMode7Bit, "\\_SB.PCI0.I2C0")
                    })
                    Name (_DSD, Package ()
                    {
                        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                        Package ()
                        {
                            Package () { "compatible", "dallas,ds1374" }
                        }
                    })
                }
            }

            Device (SPI0)
            {
                Name (_ADR, 0x001E0002)

                Device (PRC0)
                {
                    Name (_HID, "15AC0001")
                    Name (_STR, Unicode ("NAI GSPI Processor Register Controller"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        SpiSerialBusV2 (0x0000, PolarityLow, FourWireMode, 8,
                            ControllerInitiated, 500000, ClockPolarityLow,
                            ClockPhaseFirst, "\\_SB.PCI0.SPI0")
                    })
                }
            }
        }

        /* TPM 2.0 on its TIS (FIFO) interface; wolfBoot leaves its interrupt
         * unconfigured, so the OS driver polls */
        Device (TPM)
        {
            Name (_HID, "MSFT0101")
            Name (_STR, Unicode ("TPM 2.0 Device"))
            OperationRegion (TPMR, SystemMemory, 0xFED40000, 0x5000)
            Field (TPMR, AnyAcc, NoLock, Preserve)
            {
                ACC0,   8
            }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFED40000, 0x00005000)
            })
            Method (_STA, 0, NotSerialized)
            {
                If ((ACC0 == 0xFF))
                {
                    Return (Zero)
                }
                Return (0x0F)
            }
        }
    }
}
