  # 1) Strip stale Cube includes/macros that may come from options.mk
  CFLAGS := $(filter-out -I/home/%/STM32Cube/Repository/%,$(CFLAGS))
  CFLAGS := $(filter-out -DSTM32L4A6xx,$(CFLAGS))

  # 2) Inject the correct VisualGDB include paths + MCU macro
  CFLAGS += \
    -I$(VISUALGDB_BASE)/STM32L4xx_HAL_Driver/Inc \
    -I$(VISUALGDB_BASE)/STM32L4xx_HAL_Driver/Inc/Legacy \
    -I$(VISUALGDB_BASE)/CMSIS_HAL/Core/Include \
    -I$(VISUALGDB_BASE)/CMSIS_HAL/Device/ST/STM32L4xx/Include \
    -DUSE_HAL_DRIVER -D$(STM32L4_PART)

  # --- Finalize crypto objects (dedupe & add once) ----------------------------
  # Normalize paths so filter-out can match both "x.o" and "./x.o"
  override WOLFCRYPT_OBJS := $(patsubst ./%,%,$(WOLFCRYPT_OBJS))
  override MATH_OBJS      := $(patsubst ./%,%,$(MATH_OBJS))

  # Remove any pre-existing copies in OBJS, then add the union once
  override OBJS := $(filter-out $(WOLFCRYPT_OBJS) $(MATH_OBJS),$(OBJS))
  override OBJS += $(sort $(WOLFCRYPT_OBJS) $(MATH_OBJS))
  # ---------------------------------------------------------------------------

  # Where to read sources from in the VisualGDB pack:
  VISUALGDB_HAL_SRC    ?= $(VISUALGDB_BASE)/STM32L4xx_HAL_Driver/Src
  VISUALGDB_CMSIS_SRC  ?= $(VISUALGDB_BASE)/CMSIS_HAL/Device/ST/STM32L4xx/Source/Templates

  # Local object output dirs (inside this repo; not touching VisualGDB files):
  HAL_LOCAL_OBJ_DIR    ?= build/vis_hal
  CMSIS_LOCAL_OBJ_DIR  ?= build/vis_cmsis

  # options.mk added classic-Cube object paths under $(STM32CUBE)/Drivers/.../Src/*.o
  # Rewrite those object targets to our local obj dirs:
  override OBJS := $(patsubst $(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/%.o,\
                            $(HAL_LOCAL_OBJ_DIR)/%.o,$(OBJS))
  override OBJS := $(patsubst $(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32L4xx/Source/Templates/%.o,\
                            $(CMSIS_LOCAL_OBJ_DIR)/%.o,$(OBJS))

  # Pattern rules to actually build them from the VisualGDB sources:
  $(HAL_LOCAL_OBJ_DIR)/%.o: $(VISUALGDB_HAL_SRC)/%.c
	@mkdir -p $(HAL_LOCAL_OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

  $(CMSIS_LOCAL_OBJ_DIR)/%.o: $(VISUALGDB_CMSIS_SRC)/%.c
	@mkdir -p $(CMSIS_LOCAL_OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
