#ifndef WOLFBOOT_SECURE_CALLS_INCLUDED
#define WOLFBOOT_SECURE_CALLS_INCLUDED
#include <stdint.h>

/* Data types shared between wolfBoot and the non-secure application */

#ifdef WOLFBOOT_SECURE_CALLS

/* Secure calls prototypes for the non-secure world */

int __attribute__((cmse_nonsecure_call)) nsc_test(void);
int __attribute__((cmse_nonsecure_call)) wcsm_ecc_keygen(uint32_t key_size, int ecc_curve);




#endif


#endif
