vpath %.c $(dir ../src)
vpath %.c $(dir ../hal)
vpath %.c $(dir $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src)

./wcs/%.o: $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<

