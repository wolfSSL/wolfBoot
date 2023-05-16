CC = gcc
CFLAGS:=

ifneq ($(NO_FILESYSTEM),)
CFLAGS+= \
	-DNO_FILESYSTEM \
	-DPUBKEY=\"$(PUBKEY)\" \
	-DPOLICY_PUBKEY=\"$(POLICY_PUBKEY)\" \
	-DPOLICY_SIGNED=\"$(POLICY_SIGNED)\" \
	-DIMAGE_DIGEST=\"$(IMAGE_DIGEST)\" \
	-DSEAL_NV_INDEX=$(SEAL_NV_INDEX) \
	-DPOLICY_DIGEST_NV_INDEX=$(POLICY_DIGEST_NV_INDEX) \
	-DPCR_INDEX=$(PCR_INDEX)
endif

ifneq ($(SIM),)
CFLAGS+=-DSIM
endif

all: preseal keygen sign

preseal:
	$(Q)$(CC) $(CFLAGS) -o $@ $@.c -l:libwolftpm.a -l:libwolfssl.a -lm

keygen:
	$(Q)$(CC) $(CFLAGS) -o $@ $@.c -lwolftpm -lwolfssl

sign:
	$(Q)$(CC) $(CFLAGS) -o $@ $@.c -lwolftpm -lwolfssl

debug:
	$(Q)$(CC) $(CFLAGS) -o preseal preseal.c -g -l:libwolftpm.a -l:libwolfssl.a -lm
	$(Q)$(CC) $(CFLAGS) -o keygen keygen.c -g -lwolftpm -lwolfssl
	$(Q)$(CC) $(CFLAGS) -o sign sign.c -g -lwolftpm -lwolfssl

clean:
	rm -f preseal
	rm -f keygen
	rm -f sign
