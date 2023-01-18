hal/x86_fsp.ld: hal/x86_fsp.ld.in FORCE
	@cat hal/x86_fsp.ld.in | \
		sed -e "s/@FSP_T_BASE@/$(FSP_T_BASE)/g" | \
		sed -e "s/@FSP_M_BASE@/$(FSP_M_BASE)/g" | \
		sed -e "s/@FSP_S_BASE@/$(FSP_S_BASE)/g" | \
		sed -e "s/@WOLFBOOT_LOAD_BASE@/$(WOLFBOOT_LOAD_BASE)/g" | \
		sed -e "s/@BOOTLOADER_START@/$(BOOTLOADER_START)/g" \
		> $@

app_v1_signed.bin: app.bin
	./tools/keytools/sign --ed25519 app.bin wolfboot_signing_private_key.der 1

x86_qemu_flash.bin: wolfboot.bin app_v1_signed.bin $(BINASSEMBLE)
	$(Q)$(BINASSEMBLE) $@ 0 app_v1_signed.bin $$((8*1024*1024-$(BOOTLOADER_PARTITION_SIZE))) wolfboot.bin

src/fsp_t.o: $(FSP_T_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_t $^ $@

src/fsp_m.o: $(FSP_M_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_m $^ $@

src/fsp_s.o: $(FSP_S_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_s $^ $@

src/boot_x86_fsp_start.o: src/boot_x86_fsp_start.S
	@echo "\t[NASM-$(ARCH)] $@"
	$(Q)nasm -f elf32 -o $@ $^
