vpath %.c $(dir ../src)
vpath %.c $(dir ../hal)
vpath %.c $(dir ../lib/wolfssl/wolfcrypt/src)

./wcs/%.o: ./lib/wolfssl/wolfcrypt/src/%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<

