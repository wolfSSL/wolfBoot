SIGN_TOOL?=../tools/keytools/sign
SIGN_OPTIONS?=--ecc384 --sha384
SIGN_KEY?=../wolfboot_signing_private_key.der
X86FSP_PATH?=../`dirname $(FSP_M_BIN)`


$(LSCRIPT_IN): $(WOLFBOOT_ROOT)/hal/$(LSCRIPT_IN).in FORCE
	@cat $(WOLFBOOT_ROOT)/hal/$(LSCRIPT_IN).in | \
		sed -e "s/@FSP_T_BASE@/$(FSP_T_BASE)/g" | \
		sed -e "s/@FSP_M_BASE@/$(FSP_M_BASE)/g" | \
		sed -e "s/@FSP_S_BASE@/$(FSP_S_BASE)/g" | \
		sed -e "s/@WOLFBOOT_LOAD_BASE@/$(WOLFBOOT_LOAD_BASE)/g" | \
		sed -e "s/@UCODE0_BASE@/$(UCODE0_BASE)/g" | \
		sed -e "s/@UCODE1_BASE@/$(UCODE1_BASE)/g" \
		> $@

./boot_x86_fsp_start.o: boot_x86_fsp_start.S
	@echo "\t[NASM-$(ARCH)] $@"
	$(Q)nasm -f elf32 -o $@ $^

fsp_t.o: ../$(FSP_T_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_t $^ $@

fsp_m.o: ../$(FSP_M_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_m $^ $@


wolfboot_raw.bin: ../wolfboot.elf
	$(Q)$(OBJCOPY) -j .text -O binary $^ $@

wolfboot_raw.o: wolfboot_raw.bin
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.wolfboot $^ $@

sig_wolfboot_raw.o: wolfboot_raw.bin $(SIGN_KEY)
	$(SIGN_TOOL) $(SIGN_OPTIONS) wolfboot_raw.bin $(SIGN_KEY) 1
	@dd if=wolfboot_raw_v1_signed.bin of=wolfboot_raw_signature.bin bs=$(IMAGE_HEADER_SIZE) count=1
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.sig_wolfboot_raw wolfboot_raw_signature.bin sig_wolfboot_raw.o

ucode0.o: ../$(UCODE0_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.ucode0 $^ $@

ucode1.o: ../$(UCODE1_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.ucode1 $^ $@
