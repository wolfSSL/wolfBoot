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
	-DPCR_INDEX=$(PCR_INDEX) \
endif

all: preseal

preseal:
	$(Q)$(CC) $(CFLAGS) -o $@ $@.c -lwolftpm

debug:
	$(Q)$(CC) $(CFLAGS) -o preseal preseal.c -g -lwolftpm -lwolfssl

clean:
	rm -f preseal
