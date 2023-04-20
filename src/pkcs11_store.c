/* pkcs11_store.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */



#include "wolfpkcs11/pkcs11.h"
#include "wolfpkcs11/store.h"

int wolfPKCS11_Store_Open(int type, CK_ULONG id1, CK_ULONG id2, int read,
    void** store)
{
    /* Stub */
    return -1;
}

void wolfPKCS11_Store_Close(void* store)
{
    /* Stub */

}

int wolfPKCS11_Store_Read(void* store, unsigned char* buffer, int len)
{
    /* Stub */
    return -1;
}

int wolfPKCS11_Store_Write(void* store, unsigned char* buffer, int len)
{
    /* Stub */
    return -1;
}
