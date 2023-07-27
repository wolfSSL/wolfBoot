SIGN_TOOL?=../tools/keytools/sign
SIGN_OPTIONS?=--ecc256 --sha256
SIGN_KEY?=../wolfboot_signing_private_key.der
X86FSP_PATH?=../src/x86


$(LSCRIPT_IN): $(WOLFBOOT_ROOT)/hal/$(LSCRIPT_IN).in FORCE
	@cat $(WOLFBOOT_ROOT)/hal/$(LSCRIPT_IN).in | \
		sed -e "s/@FSP_T_BASE@/$(FSP_T_BASE)/g" | \
		sed -e "s/@FSP_M_BASE@/$(FSP_M_BASE)/g" | \
		sed -e "s/@FSP_S_BASE@/$(FSP_S_BASE)/g" | \
		sed -e "s/@WOLFBOOT_LOAD_BASE@/$(WOLFBOOT_LOAD_BASE)/g" | \
		sed -e "s/@UCODE0_BASE@/$(UCODE0_BASE)/g" | \
		sed -e "s/@UCODE1_BASE@/$(UCODE1_BASE)/g" | \
		sed -e "s/@FSP_S_UPD_DATA_BASE@/$(FSP_S_UPD_DATA_BASE)/g" \
		> $@

./boot_x86_fsp_start.o: boot_x86_fsp_start.S
	@echo "\t[NASM-$(ARCH)] $@"
	$(Q)nasm -f elf32 -o $@ $^

fsp_t.o: ../$(FSP_T_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_t $^ $@

fsp_m.o: ../$(FSP_M_BIN)
	$(SIGN_TOOL) $(SIGN_OPTIONS) $^ $(SIGN_KEY) 1
	@dd if=$(X86FSP_PATH)/fsp_m_v1_signed.bin of=$(X86FSP_PATH)/fsp_m_signature.bin bs=256 count=1
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.sig_fsp_m $(X86FSP_PATH)/fsp_m_signature.bin sig_fsp_m.o
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_m $^ $@
	@rm -f $(X86FSP_PATH)/fsp_m_v1_signed.bin $(X86FSP_PATH)/fsp_m_signature.bin

fsp_s.o: ../$(FSP_S_BIN)
	$(SIGN_TOOL) $(SIGN_OPTIONS) $^ $(SIGN_KEY) 1
	@dd if=$(X86FSP_PATH)/fsp_s_v1_signed.bin of=$(X86FSP_PATH)/fsp_s_signature.bin bs=256 count=1
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.sig_fsp_s $(X86FSP_PATH)/fsp_s_signature.bin sig_fsp_s.o
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsp_s $^ $@
	@rm -f $(X86FSP_PATH)/fsp_s_v1_signed.bin $(X86FSP_PATH)/fsp_s_signature.bin

wolfboot_raw.bin: ../wolfboot.elf
	$(Q)$(OBJCOPY) -j .text -O binary $^ $@

wolfboot_raw.o: wolfboot_raw.bin
	$(SIGN_TOOL) $(SIGN_OPTIONS) $^ $(SIGN_KEY) 1
	@dd if=wolfboot_raw_v1_signed.bin of=wolfboot_raw_signature.bin bs=256 count=1
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.sig_wolfboot_raw wolfboot_raw_signature.bin sig_wolfboot_raw.o
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.wolfboot $^ $@

fsp_tgl_s_upd.o: ../$(FSP_S_UPD_DATA_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.fsps_upd $^ $@

ucode0.o: ../$(UCODE0_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.ucode0 $^ $@

ucode1.o: ../$(UCODE1_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.ucode1 $^ $@
