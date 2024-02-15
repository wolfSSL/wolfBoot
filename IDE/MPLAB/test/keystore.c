/* Keystore file for wolfBoot, automatically generated. Do not edit.  */
/*
 * This file has been generated and contains the public keys
 * used by wolfBoot to verify the updates.
 */
#include <stdint.h>
#include "wolfboot/wolfboot.h"
#include "keystore.h"


#warning "*** * THIS IS THE EXAMPLE KEYSTORE.C FILE * ***"
#warning " DO NOT USE IN PRODUCTION "
#warning "This file is part of the open source distribution of wolfBoot."
#warning "Replace with a new key created using keytools."
#warning " SECURE BOOT ENABLED JUST FOR TESTING "

    

#ifdef WOLFBOOT_NO_SIGN
	#define NUM_PUBKEYS 0
#else

#if !defined(KEYSTORE_ANY) && (KEYSTORE_PUBKEY_SIZE != KEYSTORE_PUBKEY_SIZE_ECC384)
	#error Key algorithm mismatch. Remove old keys via 'make keysclean'
#else

#if defined(__APPLE__) && defined(__MACH__)
#define KEYSTORE_SECTION __attribute__((section ("__KEYSTORE,__keystore")))
#elif defined(__CCRX__)
#define KEYSTORE_SECTION
#else
#define KEYSTORE_SECTION __attribute__((section (".keystore")))
#endif

#define NUM_PUBKEYS 1
const KEYSTORE_SECTION struct keystore_slot PubKeys[NUM_PUBKEYS] = {

	/* Key associated to file 'wolfboot_signing_private_key.der' */
	{
		.slot_id = 0,
		.key_type = AUTH_KEY_ECC384,
		.part_id_mask = 0xFFFFFFFF,
		.pubkey_size = KEYSTORE_PUBKEY_SIZE_ECC384,
		.pubkey = {
			
			0x24, 0x46, 0xf8, 0x0e, 0x33, 0xed, 0xdd, 0x5b,
			0x3b, 0x01, 0xea, 0xcf, 0x89, 0x2e, 0x56, 0xe8,
			0x41, 0x73, 0xc8, 0x2f, 0xe1, 0x57, 0x68, 0x51,
			0x4f, 0x0d, 0xa6, 0x86, 0xa1, 0x92, 0xa2, 0x92,
			0xdf, 0xac, 0x31, 0x30, 0xa7, 0x15, 0xb7, 0x99,
			0xd7, 0x05, 0x2e, 0x20, 0x87, 0x1a, 0x19, 0x93,
			0xaa, 0x2f, 0xcb, 0xd6, 0x23, 0x68, 0xda, 0x00,
			0x1b, 0x4e, 0x4f, 0x63, 0x95, 0x80, 0xb7, 0x56,
			0xde, 0xfc, 0x8b, 0x73, 0x8d, 0xd1, 0x81, 0xe4,
			0x53, 0xfc, 0x61, 0x88, 0xfa, 0xef, 0x2b, 0xcb,
			0x62, 0x63, 0x8d, 0xb1, 0x98, 0x06, 0x3d, 0x29,
			0xe1, 0xb5, 0xe9, 0xa9, 0x07, 0xa2, 0xaf, 0x48
		},
	},


};

int keystore_num_pubkeys(void)
{
    return NUM_PUBKEYS;
}

uint8_t *keystore_get_buffer(int id)
{
    if (id >= keystore_num_pubkeys())
        return (uint8_t *)0;
    return (uint8_t *)PubKeys[id].pubkey;
}

int keystore_get_size(int id)
{
    if (id >= keystore_num_pubkeys())
        return -1;
    return (int)PubKeys[id].pubkey_size;
}

uint32_t keystore_get_mask(int id)
{
    if (id >= keystore_num_pubkeys())
        return -1;
    return (int)PubKeys[id].part_id_mask;
}

uint32_t keystore_get_key_type(int id)
{
   return PubKeys[id].key_type;
}

#endif /* Keystore public key size check */
#endif /* WOLFBOOT_NO_SIGN */
