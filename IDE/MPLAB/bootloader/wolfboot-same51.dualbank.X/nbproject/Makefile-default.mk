#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Include project Makefile
ifeq "${IGNORE_LOCAL}" "TRUE"
# do not include local makefile. User is passing all local related variables already
else
include Makefile
# Include makefile containing local settings
ifeq "$(wildcard nbproject/Makefile-local-default.mk)" "nbproject/Makefile-local-default.mk"
include nbproject/Makefile-local-default.mk
endif
endif

# Environment
MKDIR=mkdir -p
RM=rm -f 
MV=mv 
CP=cp 

# Macros
CND_CONF=default
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
IMAGE_TYPE=debug
OUTPUT_SUFFIX=elf
DEBUGGABLE_SUFFIX=elf
FINAL_IMAGE=${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}
else
IMAGE_TYPE=production
OUTPUT_SUFFIX=hex
DEBUGGABLE_SUFFIX=elf
FINAL_IMAGE=${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}
endif

ifeq ($(COMPARE_BUILD), true)
COMPARISON_BUILD=-mafrlcsj
else
COMPARISON_BUILD=
endif

# Object Directory
OBJECTDIR=build/${CND_CONF}/${IMAGE_TYPE}

# Distribution Directory
DISTDIR=dist/${CND_CONF}/${IMAGE_TYPE}

# Source Files Quoted if spaced
SOURCEFILES_QUOTED_IF_SPACED=../../../../lib/wolfssl/wolfcrypt/src/asn.c ../../../../lib/wolfssl/wolfcrypt/src/ecc.c ../../../../lib/wolfssl/wolfcrypt/src/hash.c ../../../../lib/wolfssl/wolfcrypt/src/memory.c ../../../../lib/wolfssl/wolfcrypt/src/sha256.c ../../../../lib/wolfssl/wolfcrypt/src/sp_c32.c ../../../../lib/wolfssl/wolfcrypt/src/sp_int.c ../../../../lib/wolfssl/wolfcrypt/src/wc_port.c ../../../../lib/wolfssl/wolfcrypt/src/wolfmath.c ../../../../src/boot_arm.c ../../../../src/image.c ../../../../src/libwolfboot.c ../../../../src/loader.c ../../../../src/string.c ../../../../src/update_flash_hwswap.c ../../../../hal/same51.c ../../test/keystore.c

# Object Files Quoted if spaced
OBJECTFILES_QUOTED_IF_SPACED=${OBJECTDIR}/_ext/1946630766/asn.o ${OBJECTDIR}/_ext/1946630766/ecc.o ${OBJECTDIR}/_ext/1946630766/hash.o ${OBJECTDIR}/_ext/1946630766/memory.o ${OBJECTDIR}/_ext/1946630766/sha256.o ${OBJECTDIR}/_ext/1946630766/sp_c32.o ${OBJECTDIR}/_ext/1946630766/sp_int.o ${OBJECTDIR}/_ext/1946630766/wc_port.o ${OBJECTDIR}/_ext/1946630766/wolfmath.o ${OBJECTDIR}/_ext/671464796/boot_arm.o ${OBJECTDIR}/_ext/671464796/image.o ${OBJECTDIR}/_ext/671464796/libwolfboot.o ${OBJECTDIR}/_ext/671464796/loader.o ${OBJECTDIR}/_ext/671464796/string.o ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o ${OBJECTDIR}/_ext/671475885/same51.o ${OBJECTDIR}/_ext/1853860402/keystore.o
POSSIBLE_DEPFILES=${OBJECTDIR}/_ext/1946630766/asn.o.d ${OBJECTDIR}/_ext/1946630766/ecc.o.d ${OBJECTDIR}/_ext/1946630766/hash.o.d ${OBJECTDIR}/_ext/1946630766/memory.o.d ${OBJECTDIR}/_ext/1946630766/sha256.o.d ${OBJECTDIR}/_ext/1946630766/sp_c32.o.d ${OBJECTDIR}/_ext/1946630766/sp_int.o.d ${OBJECTDIR}/_ext/1946630766/wc_port.o.d ${OBJECTDIR}/_ext/1946630766/wolfmath.o.d ${OBJECTDIR}/_ext/671464796/boot_arm.o.d ${OBJECTDIR}/_ext/671464796/image.o.d ${OBJECTDIR}/_ext/671464796/libwolfboot.o.d ${OBJECTDIR}/_ext/671464796/loader.o.d ${OBJECTDIR}/_ext/671464796/string.o.d ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o.d ${OBJECTDIR}/_ext/671475885/same51.o.d ${OBJECTDIR}/_ext/1853860402/keystore.o.d

# Object Files
OBJECTFILES=${OBJECTDIR}/_ext/1946630766/asn.o ${OBJECTDIR}/_ext/1946630766/ecc.o ${OBJECTDIR}/_ext/1946630766/hash.o ${OBJECTDIR}/_ext/1946630766/memory.o ${OBJECTDIR}/_ext/1946630766/sha256.o ${OBJECTDIR}/_ext/1946630766/sp_c32.o ${OBJECTDIR}/_ext/1946630766/sp_int.o ${OBJECTDIR}/_ext/1946630766/wc_port.o ${OBJECTDIR}/_ext/1946630766/wolfmath.o ${OBJECTDIR}/_ext/671464796/boot_arm.o ${OBJECTDIR}/_ext/671464796/image.o ${OBJECTDIR}/_ext/671464796/libwolfboot.o ${OBJECTDIR}/_ext/671464796/loader.o ${OBJECTDIR}/_ext/671464796/string.o ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o ${OBJECTDIR}/_ext/671475885/same51.o ${OBJECTDIR}/_ext/1853860402/keystore.o

# Source Files
SOURCEFILES=../../../../lib/wolfssl/wolfcrypt/src/asn.c ../../../../lib/wolfssl/wolfcrypt/src/ecc.c ../../../../lib/wolfssl/wolfcrypt/src/hash.c ../../../../lib/wolfssl/wolfcrypt/src/memory.c ../../../../lib/wolfssl/wolfcrypt/src/sha256.c ../../../../lib/wolfssl/wolfcrypt/src/sp_c32.c ../../../../lib/wolfssl/wolfcrypt/src/sp_int.c ../../../../lib/wolfssl/wolfcrypt/src/wc_port.c ../../../../lib/wolfssl/wolfcrypt/src/wolfmath.c ../../../../src/boot_arm.c ../../../../src/image.c ../../../../src/libwolfboot.c ../../../../src/loader.c ../../../../src/string.c ../../../../src/update_flash_hwswap.c ../../../../hal/same51.c ../../test/keystore.c

# Pack Options 
PACK_COMMON_OPTIONS=-I "${CMSIS_DIR}/CMSIS/Core/Include"



CFLAGS=
ASFLAGS=
LDLIBSOPTIONS=

############# Tool locations ##########################################
# If you copy a project from one host to another, the path where the  #
# compiler is installed may be different.                             #
# If you open this project with MPLAB X in the new host, this         #
# makefile will be regenerated and the paths will be corrected.       #
#######################################################################
# fixDeps replaces a bunch of sed/cat/printf statements that slow down the build
FIXDEPS=fixDeps

.build-conf:  ${BUILD_SUBPROJECTS}
ifneq ($(INFORMATION_MESSAGE), )
	@echo $(INFORMATION_MESSAGE)
endif
	${MAKE}  -f nbproject/Makefile-default.mk ${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}

MP_PROCESSOR_OPTION=ATSAME51J20A
MP_LINKER_FILE_OPTION=,--script="../../../../hal/same51.ld"
# ------------------------------------------------------------------------------------
# Rules for buildStep: assemble
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: assembleWithPreprocess
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compile
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${OBJECTDIR}/_ext/1946630766/asn.o: ../../../../lib/wolfssl/wolfcrypt/src/asn.c  .generated_files/flags/default/f28eedc420c224fc831c2e8146ec1fbf6a059e06 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/asn.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/asn.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/asn.o.d" -o ${OBJECTDIR}/_ext/1946630766/asn.o ../../../../lib/wolfssl/wolfcrypt/src/asn.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/ecc.o: ../../../../lib/wolfssl/wolfcrypt/src/ecc.c  .generated_files/flags/default/9980cd7fb08f9830fc16800f788926b3761ee333 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/ecc.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/ecc.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/ecc.o.d" -o ${OBJECTDIR}/_ext/1946630766/ecc.o ../../../../lib/wolfssl/wolfcrypt/src/ecc.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/hash.o: ../../../../lib/wolfssl/wolfcrypt/src/hash.c  .generated_files/flags/default/e9793b92361724c519042e91df438e4b8cc066a2 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/hash.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/hash.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/hash.o.d" -o ${OBJECTDIR}/_ext/1946630766/hash.o ../../../../lib/wolfssl/wolfcrypt/src/hash.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/memory.o: ../../../../lib/wolfssl/wolfcrypt/src/memory.c  .generated_files/flags/default/1d64ef964076f320d4074ad20345500c1ece6326 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/memory.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/memory.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/memory.o.d" -o ${OBJECTDIR}/_ext/1946630766/memory.o ../../../../lib/wolfssl/wolfcrypt/src/memory.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/sha256.o: ../../../../lib/wolfssl/wolfcrypt/src/sha256.c  .generated_files/flags/default/5e6ae4fe9477793479f3a94cb846174879eee7ed .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sha256.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sha256.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/sha256.o.d" -o ${OBJECTDIR}/_ext/1946630766/sha256.o ../../../../lib/wolfssl/wolfcrypt/src/sha256.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/sp_c32.o: ../../../../lib/wolfssl/wolfcrypt/src/sp_c32.c  .generated_files/flags/default/ced3e02d6aa7ba2274d54ba91e4157bf423f58aa .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_c32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_c32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/sp_c32.o.d" -o ${OBJECTDIR}/_ext/1946630766/sp_c32.o ../../../../lib/wolfssl/wolfcrypt/src/sp_c32.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/sp_int.o: ../../../../lib/wolfssl/wolfcrypt/src/sp_int.c  .generated_files/flags/default/421f3b00f78ba51d7b6ae7d4e42b10fca51e266 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_int.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_int.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/sp_int.o.d" -o ${OBJECTDIR}/_ext/1946630766/sp_int.o ../../../../lib/wolfssl/wolfcrypt/src/sp_int.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/wc_port.o: ../../../../lib/wolfssl/wolfcrypt/src/wc_port.c  .generated_files/flags/default/5a986bbea5f14b9ea38f4193bc2f343d089b3679 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wc_port.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wc_port.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/wc_port.o.d" -o ${OBJECTDIR}/_ext/1946630766/wc_port.o ../../../../lib/wolfssl/wolfcrypt/src/wc_port.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/wolfmath.o: ../../../../lib/wolfssl/wolfcrypt/src/wolfmath.c  .generated_files/flags/default/4e20b0d0e58bc07d129dddf5f672e144511d170d .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wolfmath.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wolfmath.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/wolfmath.o.d" -o ${OBJECTDIR}/_ext/1946630766/wolfmath.o ../../../../lib/wolfssl/wolfcrypt/src/wolfmath.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/boot_arm.o: ../../../../src/boot_arm.c  .generated_files/flags/default/17d299757e94e02281f843b2d8c6feef0d39a855 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/boot_arm.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/boot_arm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/boot_arm.o.d" -o ${OBJECTDIR}/_ext/671464796/boot_arm.o ../../../../src/boot_arm.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/image.o: ../../../../src/image.c  .generated_files/flags/default/ab485ee46917c707f4018f5d9ffb434c4ac9f06d .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/image.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/image.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/image.o.d" -o ${OBJECTDIR}/_ext/671464796/image.o ../../../../src/image.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/libwolfboot.o: ../../../../src/libwolfboot.c  .generated_files/flags/default/80a9201f0aaf43700b832d5419f74de0fa69ffd3 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/libwolfboot.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/libwolfboot.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/libwolfboot.o.d" -o ${OBJECTDIR}/_ext/671464796/libwolfboot.o ../../../../src/libwolfboot.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/loader.o: ../../../../src/loader.c  .generated_files/flags/default/b5777e5e3af5ea6e7440860725c626fd20692099 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/loader.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/loader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/loader.o.d" -o ${OBJECTDIR}/_ext/671464796/loader.o ../../../../src/loader.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/string.o: ../../../../src/string.c  .generated_files/flags/default/6ffb4717243d6421976e004be688210a4d03aa5a .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/string.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/string.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/string.o.d" -o ${OBJECTDIR}/_ext/671464796/string.o ../../../../src/string.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o: ../../../../src/update_flash_hwswap.c  .generated_files/flags/default/22ddcba4029509d4d5222aaf8ae2fb48e35d5896 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o.d" -o ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o ../../../../src/update_flash_hwswap.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671475885/same51.o: ../../../../hal/same51.c  .generated_files/flags/default/606416e42313d4ffea725a25297be71257861de3 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671475885" 
	@${RM} ${OBJECTDIR}/_ext/671475885/same51.o.d 
	@${RM} ${OBJECTDIR}/_ext/671475885/same51.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671475885/same51.o.d" -o ${OBJECTDIR}/_ext/671475885/same51.o ../../../../hal/same51.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1853860402/keystore.o: ../../test/keystore.c  .generated_files/flags/default/b733d3503c6dd06f92e4f7f5befc3e4471283bf2 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1853860402" 
	@${RM} ${OBJECTDIR}/_ext/1853860402/keystore.o.d 
	@${RM} ${OBJECTDIR}/_ext/1853860402/keystore.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1853860402/keystore.o.d" -o ${OBJECTDIR}/_ext/1853860402/keystore.o ../../test/keystore.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
else
${OBJECTDIR}/_ext/1946630766/asn.o: ../../../../lib/wolfssl/wolfcrypt/src/asn.c  .generated_files/flags/default/9c6a6ec6093e86d901ddb49cf2d84fbcbe28ed55 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/asn.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/asn.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/asn.o.d" -o ${OBJECTDIR}/_ext/1946630766/asn.o ../../../../lib/wolfssl/wolfcrypt/src/asn.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/ecc.o: ../../../../lib/wolfssl/wolfcrypt/src/ecc.c  .generated_files/flags/default/9c52fc07ee576f2655e60deb35a3deb7d12474a5 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/ecc.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/ecc.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/ecc.o.d" -o ${OBJECTDIR}/_ext/1946630766/ecc.o ../../../../lib/wolfssl/wolfcrypt/src/ecc.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/hash.o: ../../../../lib/wolfssl/wolfcrypt/src/hash.c  .generated_files/flags/default/7725e6ea1e372e9a3e3755b57411564936f2e473 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/hash.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/hash.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/hash.o.d" -o ${OBJECTDIR}/_ext/1946630766/hash.o ../../../../lib/wolfssl/wolfcrypt/src/hash.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/memory.o: ../../../../lib/wolfssl/wolfcrypt/src/memory.c  .generated_files/flags/default/c00c61cba5809b7afc92d5c3b73f50819ccf8c18 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/memory.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/memory.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/memory.o.d" -o ${OBJECTDIR}/_ext/1946630766/memory.o ../../../../lib/wolfssl/wolfcrypt/src/memory.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/sha256.o: ../../../../lib/wolfssl/wolfcrypt/src/sha256.c  .generated_files/flags/default/71bc61287ac9959a5b8ea59af4d0281e2f676f6d .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sha256.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sha256.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/sha256.o.d" -o ${OBJECTDIR}/_ext/1946630766/sha256.o ../../../../lib/wolfssl/wolfcrypt/src/sha256.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/sp_c32.o: ../../../../lib/wolfssl/wolfcrypt/src/sp_c32.c  .generated_files/flags/default/baea2952ab6b37eb6e92b8ef2417917e336b86b8 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_c32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_c32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/sp_c32.o.d" -o ${OBJECTDIR}/_ext/1946630766/sp_c32.o ../../../../lib/wolfssl/wolfcrypt/src/sp_c32.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/sp_int.o: ../../../../lib/wolfssl/wolfcrypt/src/sp_int.c  .generated_files/flags/default/2ebf72c3f667e9dbeb3bc7523b17b1df9750d860 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_int.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/sp_int.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/sp_int.o.d" -o ${OBJECTDIR}/_ext/1946630766/sp_int.o ../../../../lib/wolfssl/wolfcrypt/src/sp_int.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/wc_port.o: ../../../../lib/wolfssl/wolfcrypt/src/wc_port.c  .generated_files/flags/default/3ff2284389b562474fe7e0ae3f67384eaf363d06 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wc_port.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wc_port.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/wc_port.o.d" -o ${OBJECTDIR}/_ext/1946630766/wc_port.o ../../../../lib/wolfssl/wolfcrypt/src/wc_port.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1946630766/wolfmath.o: ../../../../lib/wolfssl/wolfcrypt/src/wolfmath.c  .generated_files/flags/default/8dd1576c1da4fa28af0b4fffbea6e87903e504da .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1946630766" 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wolfmath.o.d 
	@${RM} ${OBJECTDIR}/_ext/1946630766/wolfmath.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1946630766/wolfmath.o.d" -o ${OBJECTDIR}/_ext/1946630766/wolfmath.o ../../../../lib/wolfssl/wolfcrypt/src/wolfmath.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/boot_arm.o: ../../../../src/boot_arm.c  .generated_files/flags/default/9e223f514d85604cc70f84aeb5c7e6ed5de8049f .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/boot_arm.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/boot_arm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/boot_arm.o.d" -o ${OBJECTDIR}/_ext/671464796/boot_arm.o ../../../../src/boot_arm.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/image.o: ../../../../src/image.c  .generated_files/flags/default/31b30afe69d73463d858b26e56fec2eab871301b .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/image.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/image.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/image.o.d" -o ${OBJECTDIR}/_ext/671464796/image.o ../../../../src/image.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/libwolfboot.o: ../../../../src/libwolfboot.c  .generated_files/flags/default/aeb1a54bb763f1bd5e7d11efd3adc9be1dfdfabf .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/libwolfboot.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/libwolfboot.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/libwolfboot.o.d" -o ${OBJECTDIR}/_ext/671464796/libwolfboot.o ../../../../src/libwolfboot.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/loader.o: ../../../../src/loader.c  .generated_files/flags/default/28e2276d70e6f317b823554c2e660f51957c3a1f .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/loader.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/loader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/loader.o.d" -o ${OBJECTDIR}/_ext/671464796/loader.o ../../../../src/loader.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/string.o: ../../../../src/string.c  .generated_files/flags/default/c4378132ca7cb44a57ff6fad1a7651c9859159f5 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/string.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/string.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/string.o.d" -o ${OBJECTDIR}/_ext/671464796/string.o ../../../../src/string.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o: ../../../../src/update_flash_hwswap.c  .generated_files/flags/default/4b5af4ba0742a8a1d247fa5010d591eabd1a9c02 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671464796" 
	@${RM} ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o.d 
	@${RM} ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o.d" -o ${OBJECTDIR}/_ext/671464796/update_flash_hwswap.o ../../../../src/update_flash_hwswap.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/671475885/same51.o: ../../../../hal/same51.c  .generated_files/flags/default/e4c0da3a201f4947e16f6df2afb2384696c12dfc .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/671475885" 
	@${RM} ${OBJECTDIR}/_ext/671475885/same51.o.d 
	@${RM} ${OBJECTDIR}/_ext/671475885/same51.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/671475885/same51.o.d" -o ${OBJECTDIR}/_ext/671475885/same51.o ../../../../hal/same51.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
${OBJECTDIR}/_ext/1853860402/keystore.o: ../../test/keystore.c  .generated_files/flags/default/3086ea5bce91a81ee3681489448ee5a0551e7bf0 .generated_files/flags/default/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1853860402" 
	@${RM} ${OBJECTDIR}/_ext/1853860402/keystore.o.d 
	@${RM} ${OBJECTDIR}/_ext/1853860402/keystore.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -fno-common -D__WOLFBOOT -DWOLFBOOT_SIGN_ECC384 -DDUALBANK_SWAP=1 -DWOLFBOOT_HASH_SHA256 -DIMAGE_HEADER_SIZE=512 -DWOLFBOOT_ARCH_ARM -DTARGET_same51 -DWOLFBOOT_DUALBOOT -DWOLFSSL_USER_SETTINGS -I"../src" -I"../src/config/default" -I"../src/packs/ATSAME51J20A_DFP" -I"../src/packs/CMSIS/" -I"../src/packs/CMSIS/CMSIS/Core/Include" -I"../../../../include/wolfboot" -I"../../../../include/MPLAB" -I"../../../../lib/wolfssl" -I"../../../../include" -MP -MMD -MF "${OBJECTDIR}/_ext/1853860402/keystore.o.d" -o ${OBJECTDIR}/_ext/1853860402/keystore.o ../../test/keystore.c    -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}" ${PACK_COMMON_OPTIONS} 
	
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compileCPP
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: link
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk    ../../../../hal/same51.ld
	@${MKDIR} ${DISTDIR} 
	${MP_CC} $(MP_EXTRA_LD_PRE) -g   -mprocessor=$(MP_PROCESSOR_OPTION)  -mno-device-startup-code -o ${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}          -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=__ICD2RAM=1,--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,-D=__DEBUG_D,--defsym=_min_heap_size=512,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	
else
${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk   ../../../../hal/same51.ld
	@${MKDIR} ${DISTDIR} 
	${MP_CC} $(MP_EXTRA_LD_PRE)  -mprocessor=$(MP_PROCESSOR_OPTION)  -mno-device-startup-code -o ${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}          -DXPRJ_default=$(CND_CONF)    $(COMPARISON_BUILD)  -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=_min_heap_size=512,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	${MP_CC_DIR}/xc32-bin2hex ${DISTDIR}/wolfboot-same51.dualbank.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} 
endif


# Subprojects
.build-subprojects:


# Subprojects
.clean-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${OBJECTDIR}
	${RM} -r ${DISTDIR}

# Enable dependency checking
.dep.inc: .depcheck-impl

DEPFILES=$(wildcard ${POSSIBLE_DEPFILES})
ifneq (${DEPFILES},)
include ${DEPFILES}
endif
