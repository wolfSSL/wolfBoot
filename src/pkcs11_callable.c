/* pkcs11_callable.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include "wolfboot/wc_secure.h"
#include "wolfboot/wcs_pkcs11.h"
#include "wolfpkcs11/pkcs11.h"

#ifdef SECURE_PKCS11

#include "image.h"
#include <arm_cmse.h>
#include <stddef.h>                  /* offsetof */
#include <wolfssl/wolfcrypt/types.h> /* XMALLOC/XFREE/XMEMCPY, DYNAMIC_TYPE_* */

/*
 * TrustZone-M PKCS#11 non-secure-callable (NSC) layer with pointer
 * sanitization.
 *
 * The cmse_nonsecure_entry attribute only emits the SG trampoline and scrubs
 * the return state; it does NOT validate the pointer arguments supplied by the
 * non-secure (NS) caller. A hostile NS client can therefore hand us pointers
 * that resolve into Secure SRAM and trick the secure-world wolfPKCS11
 * implementation into reading from, or writing to, secure memory.
 *
 * Every veneer below therefore validates each NS-supplied pointer with
 * cmse_check_address_range(), recursively into nested pointers (argument
 * structs, attribute templates, mechanism parameters), and hands wolfPKCS11
 * only secure copies of NS data so it never dereferences an NS pointer. This
 * makes the hardening both deep AND free of time-of-check/time-of-use races.
 * See the ns_ok / ns_outlen_begin / nsc_mech_* / nsc_tmpl_* helpers below for
 * the per-case specifics.
 */

#define NSC_CHK(cond) do { if (!(cond)) return CKR_ARGUMENTS_BAD; } while (0)
/* Validate a pointer only when present; a NULL pointer cannot target secure
 * memory, so leave required-ness to wolfPKCS11 (e.g. phKey may be NULL for
 * CKM_TLS12_KEY_AND_MAC_DERIVE). */
#define NSC_CHK_OPT(p, len) \
    do { if ((p) != NULL && !ns_ok((p), (len))) return CKR_ARGUMENTS_BAD; } \
    while (0)

/* Largest representable CK_ULONG, used for multiply-overflow guards. */
#define NSC_ULONG_MAX (~(CK_ULONG)0)

/*
 * Return 1 if [p, p+len) is entirely non-secure and NS-accessible.
 * A zero length is always accepted (the region is never dereferenced); any
 * non-zero length requires a non-NULL pointer whose whole range is NS.
 */
static int ns_ok(const volatile void *p, CK_ULONG len)
{
    if (len == 0)
        return 1;
    if (p == NULL)
        return 0;
    return cmse_check_address_range((void *)p, (size_t)len, CMSE_NONSECURE)
            != NULL;
}

/*
 * Validate an array of 'count' elements of 'elemSize' bytes at 'p', guarding
 * against count*elemSize overflowing CK_ULONG. A zero count is accepted.
 */
static int ns_array_ok(const volatile void *p, CK_ULONG count, CK_ULONG elemSize)
{
    if (count == 0)
        return 1;
    if (count > NSC_ULONG_MAX / elemSize)
        return 0;
    return ns_ok(p, count * elemSize);
}

/*
 * Begin a length-bounded output operation (the PKCS#11 two-call convention).
 *
 * The length/count word 'pulLen' must be a valid NS CK_ULONG. Its value is
 * snapshotted into the secure local '*local', and the output buffer 'pBuf'
 * (which may be NULL for a length query) is validated against that snapshot
 * with element size 'elemSize' (1 for byte buffers). The caller MUST then pass
 * 'local' (not 'pulLen') to wolfPKCS11, so wolfPKCS11's own bounds check
 * reads the same value the buffer was validated against, and copy '*local'
 * back to '*pulLen' after the call. This defeats a racing NS caller that would
 * otherwise enlarge *pulLen after validation to reopen an out-of-bounds write.
 */
static int ns_outlen_begin(const volatile void *pBuf, CK_ULONG_PTR pulLen,
        CK_ULONG elemSize, CK_ULONG *local)
{
    if (!ns_ok(pulLen, sizeof(CK_ULONG)))
        return 0;
    *local = *pulLen; /* snapshot once into secure memory */
    if (pBuf != NULL && !ns_array_ok(pBuf, *local, elemSize))
        return 0;
    return 1;
}

/*
 * CK_MECHANISM deep-copy / bounce-buffer state.
 *
 * nsc_mech_prepare() builds 'mech', a secure copy of the NS CK_MECHANISM whose
 * pParameter points at a secure copy of the parameter blob, with every nested
 * pointer redirected at secure copies of the NS data. Input buffers are copied
 * in; output buffers are additionally recorded in cback[] and copied back to NS
 * by nsc_mech_finish(). All secure allocations are freed by finish()/free().
 *
 * Sizing: the worst case is CK_TLS12_KEY_MAT_PARAMS — blob + 2 random buffers +
 * CK_SSL3_KEY_MAT_OUT + 2 IV buffers = 6 allocs; key-material struct prefix + 2
 * IV buffers = 3 copy-backs.
 */
#define NSC_MECH_MAX_ALLOC 8
#define NSC_MECH_MAX_CBACK 4

struct nsc_mech {
    CK_MECHANISM mech;                  /* secure mechanism passed to wolfPKCS11 */
    void        *alloc[NSC_MECH_MAX_ALLOC];
    int          nAlloc;
    struct {
        void    *dst;                   /* NS destination */
        void    *src;                   /* secure source */
        CK_ULONG len;
    } cback[NSC_MECH_MAX_CBACK];
    int          nCback;
    int          isNull;                /* NS mechanism pointer was NULL */
};

/* Allocate a tracked secure buffer; returns NULL if out of slots/memory. */
static void *nsc_alloc(struct nsc_mech *m, CK_ULONG len)
{
    void *p;

    if (m->nAlloc >= NSC_MECH_MAX_ALLOC)
        return NULL;
    p = XMALLOC((size_t)len, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (p != NULL)
        m->alloc[m->nAlloc++] = p;
    return p;
}

/*
 * Copy an NS input buffer [src,len) into a fresh secure buffer (validated).
 * NULL/zero-length yields *out == NULL. The pointer is read by value, so the
 * caller may pass the address of the very field being overwritten.
 */
static CK_RV nsc_in(struct nsc_mech *m, CK_VOID_PTR src, CK_ULONG len,
        CK_VOID_PTR *out)
{
    void *p;

    *out = NULL;
    if (src == NULL || len == 0)
        return CKR_OK;
    if (!ns_ok(src, len))
        return CKR_ARGUMENTS_BAD;
    p = nsc_alloc(m, len);
    if (p == NULL)
        return CKR_HOST_MEMORY;
    XMEMCPY(p, (void *)src, (size_t)len);
    *out = p;
    return CKR_OK;
}

/*
 * Like nsc_in() but the buffer is also an output: the current NS contents are
 * copied in (so in/out semantics work), and a copy-back of [dst,len) is queued
 * for nsc_mech_finish().
 */
static CK_RV nsc_inout(struct nsc_mech *m, CK_VOID_PTR dst, CK_ULONG len,
        CK_VOID_PTR *out)
{
    void *p;

    *out = NULL;
    if (dst == NULL || len == 0)
        return CKR_OK;
    if (!ns_ok(dst, len))
        return CKR_ARGUMENTS_BAD;
    if (m->nCback >= NSC_MECH_MAX_CBACK)
        return CKR_HOST_MEMORY;
    p = nsc_alloc(m, len);
    if (p == NULL)
        return CKR_HOST_MEMORY;
    XMEMCPY(p, (void *)dst, (size_t)len);
    m->cback[m->nCback].dst = (void *)dst;
    m->cback[m->nCback].src = p;
    m->cback[m->nCback].len = len;
    m->nCback++;
    *out = p;
    return CKR_OK;
}

/* Free all secure allocations without copying anything back (error path). */
static void nsc_mech_free(struct nsc_mech *m)
{
    int i;

    for (i = 0; i < m->nAlloc; i++)
        XFREE(m->alloc[i], NULL, DYNAMIC_TYPE_TMP_BUFFER);
    m->nAlloc = 0;
    m->nCback = 0;
}

/* Copy queued output buffers back to NS, then free all secure allocations. */
static void nsc_mech_finish(struct nsc_mech *m)
{
    int i;

    for (i = 0; i < m->nCback; i++)
        XMEMCPY(m->cback[i].dst, m->cback[i].src, (size_t)m->cback[i].len);
    nsc_mech_free(m);
}

/* The mechanism pointer to hand wolfPKCS11 (NULL if the caller passed NULL). */
static CK_MECHANISM_PTR nsc_mech_arg(struct nsc_mech *m)
{
    return m->isNull ? NULL : &m->mech;
}

/*
 * Deep-copy an NS CK_MECHANISM (and all pointers nested in its parameter blob)
 * into secure memory. On CKR_OK the caller invokes wolfPKCS11 with
 * nsc_mech_arg(m) and then nsc_mech_finish(m); on error nothing needs freeing.
 *
 * Each parameter case is gated by the same feature macro wolfPKCS11 uses, and
 * only interprets the blob when ulParameterLen is large enough for the struct
 * (otherwise wolfPKCS11 rejects it on the size check before any nested deref).
 */
static CK_RV nsc_mech_prepare(CK_MECHANISM_PTR ns, struct nsc_mech *m)
{
    CK_VOID_PTR q;
    void *blob;
    CK_ULONG len;
    CK_RV rv = CKR_OK;

    XMEMSET(m, 0, sizeof(*m));
    if (ns == NULL) {
        m->isNull = 1;
        return CKR_OK;
    }
    if (!ns_ok(ns, sizeof(CK_MECHANISM)))
        return CKR_ARGUMENTS_BAD;
    m->mech = *ns;                       /* secure snapshot of the mechanism */
    len = m->mech.ulParameterLen;
    if (m->mech.pParameter == NULL || len == 0) {
        m->mech.pParameter = NULL;
        return CKR_OK;
    }
    if (!ns_ok(m->mech.pParameter, len))
        return CKR_ARGUMENTS_BAD;
    blob = nsc_alloc(m, len);
    if (blob == NULL)
        return CKR_HOST_MEMORY;
    XMEMCPY(blob, (void *)m->mech.pParameter, (size_t)len);
    m->mech.pParameter = blob;           /* wolfPKCS11 reads the secure blob */

    switch (m->mech.mechanism) {
#ifndef WC_NO_RSA_OAEP
    case CKM_RSA_PKCS_OAEP:
        if (len >= sizeof(CK_RSA_PKCS_OAEP_PARAMS)) {
            CK_RSA_PKCS_OAEP_PARAMS *p = (CK_RSA_PKCS_OAEP_PARAMS *)blob;
            rv = nsc_in(m, p->pSourceData, p->ulSourceDataLen, &q);
            if (rv == CKR_OK) p->pSourceData = q;
        }
        break;
#endif
#ifdef HAVE_AESGCM
    case CKM_AES_GCM:
        if (len >= sizeof(CK_GCM_PARAMS)) {
            CK_GCM_PARAMS *p = (CK_GCM_PARAMS *)blob;
            rv = nsc_in(m, p->pIv, p->ulIvLen, &q);
            if (rv == CKR_OK) {
                p->pIv = (CK_BYTE_PTR)q;
                rv = nsc_in(m, p->pAAD, p->ulAADLen, &q);
            }
            if (rv == CKR_OK) p->pAAD = (CK_BYTE_PTR)q;
        }
        break;
#endif
#ifdef HAVE_AESCCM
    case CKM_AES_CCM:
        if (len >= sizeof(CK_CCM_PARAMS)) {
            CK_CCM_PARAMS *p = (CK_CCM_PARAMS *)blob;
            rv = nsc_in(m, p->pIv, p->ulIvLen, &q);
            if (rv == CKR_OK) {
                p->pIv = (CK_BYTE_PTR)q;
                rv = nsc_in(m, p->pAAD, p->ulAADLen, &q);
            }
            if (rv == CKR_OK) p->pAAD = (CK_BYTE_PTR)q;
        }
        break;
#endif
#ifdef HAVE_ECC
    case CKM_ECDH1_DERIVE:
        if (len >= sizeof(CK_ECDH1_DERIVE_PARAMS)) {
            CK_ECDH1_DERIVE_PARAMS *p = (CK_ECDH1_DERIVE_PARAMS *)blob;
            rv = nsc_in(m, p->pSharedData, p->ulSharedDataLen, &q);
            if (rv == CKR_OK) {
                p->pSharedData = (CK_BYTE_PTR)q;
                rv = nsc_in(m, p->pPublicData, p->ulPublicDataLen, &q);
            }
            if (rv == CKR_OK) p->pPublicData = (CK_BYTE_PTR)q;
        }
        break;
#endif
#if !defined(NO_AES) && defined(HAVE_AES_CBC)
    case CKM_AES_CBC_ENCRYPT_DATA:
        if (len >= sizeof(CK_AES_CBC_ENCRYPT_DATA_PARAMS)) {
            CK_AES_CBC_ENCRYPT_DATA_PARAMS *p =
                (CK_AES_CBC_ENCRYPT_DATA_PARAMS *)blob;
            rv = nsc_in(m, p->pData, p->length, &q);
            if (rv == CKR_OK) p->pData = (CK_BYTE_PTR)q;
        }
        break;
#endif
#ifdef WOLFPKCS11_HKDF
    case CKM_HKDF_DERIVE:
    case CKM_HKDF_DATA:
        if (len >= sizeof(CK_HKDF_PARAMS)) {
            CK_HKDF_PARAMS *p = (CK_HKDF_PARAMS *)blob;
            rv = nsc_in(m, p->pSalt, p->ulSaltLen, &q);
            if (rv == CKR_OK) {
                p->pSalt = (CK_BYTE_PTR)q;
                rv = nsc_in(m, p->pInfo, p->ulInfoLen, &q);
            }
            if (rv == CKR_OK) p->pInfo = (CK_BYTE_PTR)q;
        }
        break;
#endif
#ifdef WOLFSSL_HAVE_PRF
    case CKM_TLS12_KEY_AND_MAC_DERIVE:
        if (len >= sizeof(CK_TLS12_KEY_MAT_PARAMS)) {
            CK_TLS12_KEY_MAT_PARAMS *p = (CK_TLS12_KEY_MAT_PARAMS *)blob;
            CK_ULONG ivLen = p->ulIVSizeInBits / 8;

            rv = nsc_in(m, p->RandomInfo.pClientRandom,
                    p->RandomInfo.ulClientRandomLen, &q);
            if (rv == CKR_OK) {
                p->RandomInfo.pClientRandom = (CK_BYTE_PTR)q;
                rv = nsc_in(m, p->RandomInfo.pServerRandom,
                        p->RandomInfo.ulServerRandomLen, &q);
            }
            if (rv == CKR_OK) p->RandomInfo.pServerRandom = (CK_BYTE_PTR)q;
            /* pReturnedKeyMaterial: wolfPKCS11 writes the 4 key handles into
             * this struct and writes IV bytes through pIVClient/pIVServer. Copy
             * the struct in, queue copy-back of only the handle prefix (so the
             * NS pointer fields are preserved), and bounce the IV buffers. */
            if (rv == CKR_OK && p->pReturnedKeyMaterial != NULL) {
                if (!ns_ok(p->pReturnedKeyMaterial,
                        sizeof(CK_SSL3_KEY_MAT_OUT))) {
                    rv = CKR_ARGUMENTS_BAD;
                }
                else {
                    CK_SSL3_KEY_MAT_OUT *km =
                        (CK_SSL3_KEY_MAT_OUT *)nsc_alloc(m,
                            sizeof(CK_SSL3_KEY_MAT_OUT));
                    if (km == NULL) {
                        rv = CKR_HOST_MEMORY;
                    }
                    else if (m->nCback >= NSC_MECH_MAX_CBACK) {
                        rv = CKR_HOST_MEMORY;
                    }
                    else {
                        CK_BYTE_PTR nsIVc, nsIVs;

                        XMEMCPY(km, p->pReturnedKeyMaterial,
                                sizeof(CK_SSL3_KEY_MAT_OUT));
                        nsIVc = km->pIVClient;
                        nsIVs = km->pIVServer;
                        /* copy back only the output handle prefix */
                        m->cback[m->nCback].dst = p->pReturnedKeyMaterial;
                        m->cback[m->nCback].src = km;
                        m->cback[m->nCback].len =
                            offsetof(CK_SSL3_KEY_MAT_OUT, pIVClient);
                        m->nCback++;
                        rv = nsc_inout(m, nsIVc, ivLen, &q);
                        if (rv == CKR_OK) {
                            km->pIVClient = (CK_BYTE_PTR)q;
                            rv = nsc_inout(m, nsIVs, ivLen, &q);
                        }
                        if (rv == CKR_OK) {
                            km->pIVServer = (CK_BYTE_PTR)q;
                            p->pReturnedKeyMaterial = km;
                        }
                    }
                }
            }
        }
        break;
    case CKM_TLS12_MASTER_KEY_DERIVE:
    case CKM_TLS12_MASTER_KEY_DERIVE_DH:
        if (len >= sizeof(CK_TLS12_MASTER_KEY_DERIVE_PARAMS)) {
            CK_TLS12_MASTER_KEY_DERIVE_PARAMS *p =
                (CK_TLS12_MASTER_KEY_DERIVE_PARAMS *)blob;
            rv = nsc_in(m, p->RandomInfo.pClientRandom,
                    p->RandomInfo.ulClientRandomLen, &q);
            if (rv == CKR_OK) {
                p->RandomInfo.pClientRandom = (CK_BYTE_PTR)q;
                rv = nsc_in(m, p->RandomInfo.pServerRandom,
                        p->RandomInfo.ulServerRandomLen, &q);
            }
            if (rv == CKR_OK) { /* pVersion is read and may be written */
                p->RandomInfo.pServerRandom = (CK_BYTE_PTR)q;
                rv = nsc_inout(m, p->pVersion, sizeof(CK_VERSION), &q);
            }
            if (rv == CKR_OK) p->pVersion = (CK_VERSION_PTR)q;
        }
        break;
#endif
#ifdef WOLFPKCS11_NSS
    case CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE:
    case CKM_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_DH:
        if (len >= sizeof(CK_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_PARAMS)) {
            CK_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_PARAMS *p =
                (CK_NSS_TLS_EXTENDED_MASTER_KEY_DERIVE_PARAMS *)blob;
            rv = nsc_in(m, p->pSessionHash, p->ulSessionHashLen, &q);
            if (rv == CKR_OK) {
                p->pSessionHash = (CK_BYTE_PTR)q;
                rv = nsc_inout(m, p->pVersion, sizeof(CK_VERSION), &q);
            }
            if (rv == CKR_OK) p->pVersion = (CK_VERSION_PTR)q;
        }
        break;
    case CKM_NSS_PKCS12_PBE_SHA224_HMAC_KEY_GEN:
    case CKM_NSS_PKCS12_PBE_SHA256_HMAC_KEY_GEN:
    case CKM_NSS_PKCS12_PBE_SHA384_HMAC_KEY_GEN:
    case CKM_NSS_PKCS12_PBE_SHA512_HMAC_KEY_GEN:
        if (len >= sizeof(CK_PBE_PARAMS)) {
            CK_PBE_PARAMS *p = (CK_PBE_PARAMS *)blob;
            /* pInitVector is not used by wolfPKCS11; only password+salt. */
            rv = nsc_in(m, p->pPassword, p->ulPasswordLen, &q);
            if (rv == CKR_OK) {
                p->pPassword = (CK_UTF8CHAR_PTR)q;
                rv = nsc_in(m, p->pSalt, p->ulSaltLen, &q);
            }
            if (rv == CKR_OK) p->pSalt = (CK_BYTE_PTR)q;
        }
        break;
#endif
#ifndef NO_HMAC
    case CKM_PKCS5_PBKD2:
        if (len >= sizeof(CK_PKCS5_PBKD2_PARAMS)) {
            CK_PKCS5_PBKD2_PARAMS2 *p = (CK_PKCS5_PBKD2_PARAMS2 *)blob;
            CK_ULONG pwLen;

            rv = nsc_in(m, p->pSaltSourceData, p->ulSaltSourceDataLen, &q);
            if (rv == CKR_OK) {
                p->pSaltSourceData = q;
                rv = nsc_in(m, p->pPrfData, p->ulPrfDataLen, &q);
            }
            if (rv == CKR_OK) p->pPrfData = q;
            /* Password length matches wolfPKCS11's legacy heuristic: when the
             * field value exceeds the max, the older CK_PKCS5_PBKD2_PARAMS is
             * assumed, whose ulPasswordLen is itself a CK_ULONG_PTR. Bounce
             * that length word too so wolfPKCS11 never derefs an NS pointer. */
            if (rv == CKR_OK) {
                if (p->ulPasswordLen > CK_PKCS5_PBKD2_PARAMS_MAX_PWD_LEN) {
                    CK_PKCS5_PBKD2_PARAMS *legacy =
                        (CK_PKCS5_PBKD2_PARAMS *)blob;
                    CK_ULONG_PTR nsLen = legacy->ulPasswordLen;

                    if (!ns_ok(nsLen, sizeof(CK_ULONG))) {
                        rv = CKR_ARGUMENTS_BAD;
                    }
                    else {
                        pwLen = *nsLen;
                        rv = nsc_in(m, nsLen, sizeof(CK_ULONG), &q);
                        if (rv == CKR_OK)
                            legacy->ulPasswordLen = (CK_ULONG_PTR)q;
                    }
                }
                else {
                    pwLen = p->ulPasswordLen;
                }
            }
            if (rv == CKR_OK) {
                rv = nsc_in(m, p->pPassword, pwLen, &q);
            }
            if (rv == CKR_OK) p->pPassword = (CK_UTF8CHAR_PTR)q;
        }
        break;
#endif
#ifdef WOLFPKCS11_MLDSA
    case CKM_ML_DSA:
        if (len >= sizeof(CK_SIGN_ADDITIONAL_CONTEXT)) {
            CK_SIGN_ADDITIONAL_CONTEXT *p = (CK_SIGN_ADDITIONAL_CONTEXT *)blob;
            rv = nsc_in(m, p->pContext, p->ulContextLen, &q);
            if (rv == CKR_OK) p->pContext = (CK_BYTE_PTR)q;
        }
        break;
    case CKM_HASH_ML_DSA:
        if (len >= sizeof(CK_HASH_SIGN_ADDITIONAL_CONTEXT)) {
            CK_HASH_SIGN_ADDITIONAL_CONTEXT *p =
                (CK_HASH_SIGN_ADDITIONAL_CONTEXT *)blob;
            rv = nsc_in(m, p->pContext, p->ulContextLen, &q);
            if (rv == CKR_OK) p->pContext = (CK_BYTE_PTR)q;
        }
        break;
#endif
    default:
        break;
    }

    if (rv != CKR_OK)
        nsc_mech_free(m);
    return rv;
}

/*
 * CK_ATTRIBUTE template deep-copy / bounce-buffering.
 *
 * A template is an NS array of CK_ATTRIBUTE{type, pValue, ulValueLen}.
 * wolfPKCS11 re-reads the array and dereferences each pValue, so both the array
 * and every value buffer are copied into secure memory:
 *   - 'snap' is an immutable secure snapshot of the NS array (original pValue
 *     pointers + lengths), used for validation and copy-back.
 *   - 'work' is the secure array handed to wolfPKCS11, with each pValue
 *     redirected at a secure value buffer.
 * For input templates (CreateObject, GenerateKey, ...) value buffers are copied
 * in. For the in/out case (C_GetAttributeValue) the buffers are also copied
 * back to NS afterwards, along with each ulValueLen wolfPKCS11 set, using the
 * snapshot's NS pointers so a racing caller cannot redirect the destination.
 */
struct nsc_tmpl {
    CK_ATTRIBUTE_PTR nsBase;   /* original NS array (write lengths back here) */
    CK_ATTRIBUTE    *work;     /* secure array handed to wolfPKCS11 */
    CK_ATTRIBUTE    *snap;     /* secure snapshot: NS pValue + original length */
    CK_ULONG         count;
    int              isOut;    /* C_GetAttributeValue: copy values+lengths back */
    int              active;   /* a secure array was built */
};

static void nsc_tmpl_free(struct nsc_tmpl *t)
{
    CK_ULONG i;

    if (t->work != NULL) {
        for (i = 0; i < t->count; i++) {
            if (t->work[i].pValue != NULL)
                XFREE(t->work[i].pValue, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        }
        XFREE(t->work, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        t->work = NULL;
    }
    if (t->snap != NULL) {
        XFREE(t->snap, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        t->snap = NULL;
    }
}

/*
 * Deep-copy an NS attribute template into secure memory. 'isOut' selects the
 * C_GetAttributeValue semantics (allocate output buffers, copy back). On CKR_OK
 * the caller passes nsc_tmpl_arg() to wolfPKCS11 and then nsc_tmpl_finish().
 */
static CK_RV nsc_tmpl_prepare(CK_ATTRIBUTE_PTR ns, CK_ULONG count, int isOut,
        struct nsc_tmpl *t)
{
    CK_ULONG i;
    CK_RV rv = CKR_OK;

    XMEMSET(t, 0, sizeof(*t));
    t->nsBase = ns;
    t->count = count;
    t->isOut = isOut;
    if (count == 0)
        return CKR_OK;
    if (!ns_array_ok(ns, count, sizeof(CK_ATTRIBUTE)))
        return CKR_ARGUMENTS_BAD;
    t->snap = (CK_ATTRIBUTE *)XMALLOC((size_t)(count * sizeof(CK_ATTRIBUTE)),
            NULL, DYNAMIC_TYPE_TMP_BUFFER);
    t->work = (CK_ATTRIBUTE *)XMALLOC((size_t)(count * sizeof(CK_ATTRIBUTE)),
            NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (t->snap == NULL || t->work == NULL) {
        rv = CKR_HOST_MEMORY;
        goto fail;
    }
    XMEMCPY(t->snap, ns, (size_t)(count * sizeof(CK_ATTRIBUTE)));
    /* Initialise work with NULL pValue so a mid-loop failure frees cleanly. */
    for (i = 0; i < count; i++) {
        t->work[i].type = t->snap[i].type;
        t->work[i].pValue = NULL;
        t->work[i].ulValueLen = t->snap[i].ulValueLen;
    }
    t->active = 1;
    for (i = 0; i < count; i++) {
        CK_VOID_PTR nsVal = t->snap[i].pValue;
        CK_ULONG vlen = t->snap[i].ulValueLen;
        void *buf;

        if (nsVal == NULL)
            continue;                    /* length query / no value */
        if (!ns_ok(nsVal, vlen)) {
            rv = CKR_ARGUMENTS_BAD;
            goto fail;
        }
        /* >=1 byte so a non-NULL NS pValue stays non-NULL to wolfPKCS11. */
        buf = XMALLOC(vlen ? (size_t)vlen : 1, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (buf == NULL) {
            rv = CKR_HOST_MEMORY;
            goto fail;
        }
        if (vlen)
            XMEMCPY(buf, (void *)nsVal, (size_t)vlen);
        t->work[i].pValue = buf;
    }
    return CKR_OK;
fail:
    nsc_tmpl_free(t);
    return rv;
}

/* Copy back output values/lengths (in/out templates), then free. */
static void nsc_tmpl_finish(struct nsc_tmpl *t)
{
    CK_ULONG i;

    if (t->active && t->isOut) {
        for (i = 0; i < t->count; i++) {
            CK_VOID_PTR nsVal = t->snap[i].pValue;

            /* Write the ulValueLen wolfPKCS11 set back to the NS entry. */
            t->nsBase[i].ulValueLen = t->work[i].ulValueLen;
            /* Round-trip the value buffer to its original NS location. */
            if (nsVal != NULL && t->snap[i].ulValueLen != 0)
                XMEMCPY((void *)nsVal, t->work[i].pValue,
                        (size_t)t->snap[i].ulValueLen);
        }
    }
    nsc_tmpl_free(t);
}

/* The template pointer to hand wolfPKCS11 (the secure copy, or the original
 * when count==0 since wolfPKCS11 will not dereference it). */
static CK_ATTRIBUTE_PTR nsc_tmpl_arg(struct nsc_tmpl *t)
{
    return t->active ? t->work : t->nsBase;
}

#if (defined(WOLFPKCS11_NSS) && !defined(WOLFPKCS11_NO_STORE))
/* Maximum NSS LibraryParameters string we will bounce, including the NUL. */
#define NSC_NSS_PARAM_MAX 4096

/*
 * Deep-copy a NUL-terminated NSS configuration string from NS into secure
 * memory. The terminator is found by scanning at most NSC_NSS_PARAM_MAX bytes,
 * validating each byte is non-secure *before* reading it, so an unterminated
 * NS string can never make wolfPKCS11's parser walk into secure memory. The
 * caller frees *out with XFREE.
 */
static CK_RV nsc_dup_nss_string(const char *ns, char **out)
{
    CK_ULONG i;
    char *buf;

    *out = NULL;
    for (i = 0; i < NSC_NSS_PARAM_MAX; i++) {
        if (!ns_ok(ns + i, 1))
            return CKR_ARGUMENTS_BAD;
        if (ns[i] == '\0')
            break;
    }
    if (i == NSC_NSS_PARAM_MAX)              /* not terminated within bound */
        return CKR_ARGUMENTS_BAD;
    buf = (char *)XMALLOC((size_t)(i + 1), NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (buf == NULL)
        return CKR_HOST_MEMORY;
    XMEMCPY(buf, (const void *)ns, (size_t)(i + 1));
    *out = buf;
    return CKR_OK;
}
#endif

CK_RV CSME_NSE_API C_Initialize_nsc_call(CK_VOID_PTR pInitArgs)
{
    if (pInitArgs == NULL)
        return C_Initialize(NULL);
    /* pInitArgs, when present, points to a CK_C_INITIALIZE_ARGS. */
    NSC_CHK(ns_ok(pInitArgs, sizeof(CK_C_INITIALIZE_ARGS)));
#if (defined(WOLFPKCS11_NSS) && !defined(WOLFPKCS11_NO_STORE))
    {
        /* wolfPKCS11 ignores the mutex callbacks but, in NSS builds,
         * dereferences LibraryParameters as a NUL-terminated string. Snapshot
         * the struct and deep-copy that nested string into secure memory. */
        CK_C_INITIALIZE_ARGS args = *(CK_C_INITIALIZE_ARGS *)pInitArgs;
        char *secStr = NULL;
        CK_RV rv;

        if (args.LibraryParameters != NULL) {
            rv = nsc_dup_nss_string((const char *)args.LibraryParameters,
                    &secStr);
            if (rv != CKR_OK)
                return rv;
            args.LibraryParameters = (CK_CHAR_PTR *)secStr;
        }
        rv = C_Initialize(&args);
        if (secStr != NULL)
            XFREE(secStr, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return rv;
    }
#else
    /* Non-NSS: wolfPKCS11 dereferences no nested pointer in the struct. */
    return C_Initialize(pInitArgs);
#endif
}

CK_RV CSME_NSE_API C_Finalize_nsc_call(CK_VOID_PTR pReserved)
{
    /* pReserved must be NULL; the underlying call rejects anything else. */
    return C_Finalize(pReserved);
}

CK_RV CSME_NSE_API C_GetInfo_nsc_call(CK_INFO_PTR pInfo)
{
    NSC_CHK(ns_ok(pInfo, sizeof(CK_INFO)));
    return C_GetInfo(pInfo);
}

WP11_API CK_RV CSME_NSE_API C_GetFunctionList_nsc_call(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
    /*
     * The PKCS#11 function table is a non-secure concern. The NS client builds
     * its own CK_FUNCTION_LIST whose entries are NS wrappers around these
     * veneers (see test-app/wcs/pkcs11_stub.c) and resolves C_GetFunctionList
     * locally. The secure table (wolfpkcs11FunctionList) must never cross the
     * boundary: its entries are secure-world function addresses NS can neither
     * read nor call, the veneers use a different (args-struct) calling
     * convention, and returning it would leak secure pointers into NS memory.
     */
    (void)ppFunctionList;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV CSME_NSE_API C_GetSlotList_nsc_call(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList, CK_ULONG_PTR pulCount)
{
    CK_ULONG cnt;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pSlotList, pulCount, sizeof(CK_SLOT_ID), &cnt));
    rv = C_GetSlotList(tokenPresent, pSlotList, &cnt);
    *pulCount = cnt;
    return rv;
}

CK_RV CSME_NSE_API C_GetSlotInfo_nsc_call(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo)
{
    NSC_CHK(ns_ok(pInfo, sizeof(CK_SLOT_INFO)));
    return C_GetSlotInfo(slotID, pInfo);
}

CK_RV CSME_NSE_API C_GetTokenInfo_nsc_call(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
    NSC_CHK(ns_ok(pInfo, sizeof(CK_TOKEN_INFO)));
    return C_GetTokenInfo(slotID, pInfo);
}

CK_RV CSME_NSE_API C_GetMechanismList_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE_PTR pMechanismList, CK_ULONG_PTR pulCount)
{
    CK_ULONG cnt;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pMechanismList, pulCount, sizeof(CK_MECHANISM_TYPE), &cnt));
    rv = C_GetMechanismList(slotID, pMechanismList, &cnt);
    *pulCount = cnt;
    return rv;
}

CK_RV CSME_NSE_API C_GetMechanismInfo_nsc_call(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type, CK_MECHANISM_INFO_PTR pInfo)
{
    NSC_CHK(ns_ok(pInfo, sizeof(CK_MECHANISM_INFO)));
    return C_GetMechanismInfo(slotID, type, pInfo);
}

CK_RV CSME_NSE_API C_InitToken_nsc_call(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel)
{
    NSC_CHK(ns_ok(pPin, ulPinLen));
    NSC_CHK(ns_ok(pLabel, 32)); /* token label is a fixed 32-byte field */
    return C_InitToken(slotID, pPin, ulPinLen, pLabel);
}

CK_RV CSME_NSE_API C_InitPIN_nsc_call(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
    NSC_CHK(ns_ok(pPin, ulPinLen));
    return C_InitPIN(hSession, pPin, ulPinLen);
}


CK_RV CSME_NSE_API C_SetPIN_nsc_call(struct C_SetPIN_nsc_args *args)

{
    struct C_SetPIN_nsc_args a;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pOldPin, a.ulOldLen));
    NSC_CHK(ns_ok(a.pNewPin, a.ulNewLen));
    return C_SetPIN(a.hSession, a.pOldPin, a.ulOldLen, a.pNewPin, a.ulNewLen);
}

CK_RV CSME_NSE_API C_OpenSession_nsc_call(struct C_OpenSession_nsc_args *args) {
    struct C_OpenSession_nsc_args a;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    /* pApplication and Notify are opaque NS callback context, not dereferenced
     * by the secure side; only the session-handle output is written here. */
    NSC_CHK(ns_ok(a.phSession, sizeof(CK_SESSION_HANDLE)));
    return C_OpenSession(a.slotID, a.flags, a.pApplication, a.Notify, a.phSession);
}

CK_RV CSME_NSE_API C_CloseSession_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_CloseSession(hSession);
}

CK_RV CSME_NSE_API C_CloseAllSessions_nsc_call(CK_SLOT_ID slotID)
{
    return C_CloseAllSessions(slotID);
}

CK_RV CSME_NSE_API C_GetSessionInfo_nsc_call(CK_SESSION_HANDLE hSession, CK_SESSION_INFO_PTR pInfo)
{
    NSC_CHK(ns_ok(pInfo, sizeof(CK_SESSION_INFO)));
    return C_GetSessionInfo(hSession, pInfo);
}

CK_RV CSME_NSE_API C_GetOperationState_nsc_call(
        CK_SESSION_HANDLE hSession,
        CK_BYTE_PTR pOperationState,
        CK_ULONG_PTR pulOperationStateLen) {
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pOperationState, pulOperationStateLen, 1, &len));
    rv = C_GetOperationState(hSession, pOperationState, &len);
    *pulOperationStateLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_SetOperationState_nsc_call(struct C_SetOperationState_nsc_args *args) {
    struct C_SetOperationState_nsc_args a;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pOperationState, a.ulOperationStateLen));
    return C_SetOperationState(a.hSession, a.pOperationState, a.ulOperationStateLen, a.hEncryptionKey, a.hAuthenticationKey);
}

CK_RV CSME_NSE_API C_Login_nsc_call(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
              CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen) {
    NSC_CHK(ns_ok(pPin, ulPinLen));
    return C_Login(hSession, userType, pPin, ulPinLen);
}

CK_RV CSME_NSE_API C_Logout_nsc_call(CK_SESSION_HANDLE hSession) {
    return C_Logout(hSession);
}

CK_RV CSME_NSE_API C_CreateObject_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                     CK_OBJECT_HANDLE_PTR phObject) {
    struct nsc_tmpl tc;
    CK_RV rv;

    NSC_CHK(ns_ok(phObject, sizeof(CK_OBJECT_HANDLE)));
    rv = nsc_tmpl_prepare(pTemplate, ulCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = C_CreateObject(hSession, nsc_tmpl_arg(&tc), ulCount, phObject);
    nsc_tmpl_finish(&tc);
    return rv;
}

CK_RV CSME_NSE_API C_DestroyObject_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject) {
    return C_DestroyObject(hSession, hObject);
}

CK_RV CSME_NSE_API C_GetObjectSize_nsc_call(CK_SESSION_HANDLE hSession,
                      CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize) {
    NSC_CHK(ns_ok(pulSize, sizeof(CK_ULONG)));
    return C_GetObjectSize(hSession, hObject, pulSize);
}

CK_RV CSME_NSE_API C_GetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    struct nsc_tmpl tc;
    CK_RV rv;

    rv = nsc_tmpl_prepare(pTemplate, ulCount, 1, &tc); /* in/out: copy back */
    if (rv != CKR_OK)
        return rv;
    rv = C_GetAttributeValue(hSession, hObject, nsc_tmpl_arg(&tc), ulCount);
    nsc_tmpl_finish(&tc);
    return rv;
}

CK_RV CSME_NSE_API C_SetAttributeValue_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_OBJECT_HANDLE hObject,
                          CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    struct nsc_tmpl tc;
    CK_RV rv;

    rv = nsc_tmpl_prepare(pTemplate, ulCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = C_SetAttributeValue(hSession, hObject, nsc_tmpl_arg(&tc), ulCount);
    nsc_tmpl_finish(&tc);
    return rv;
}

CK_RV CSME_NSE_API C_FindObjectsInit_nsc_call(CK_SESSION_HANDLE hSession,
                        CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount) {
    struct nsc_tmpl tc;
    CK_RV rv;

    rv = nsc_tmpl_prepare(pTemplate, ulCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = C_FindObjectsInit(hSession, nsc_tmpl_arg(&tc), ulCount);
    nsc_tmpl_finish(&tc);
    return rv;
}

CK_RV CSME_NSE_API C_FindObjects_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_OBJECT_HANDLE_PTR phObject,
                    CK_ULONG ulMaxObjectCount,
                    CK_ULONG_PTR pulObjectCount) {
    NSC_CHK(ns_array_ok(phObject, ulMaxObjectCount, sizeof(CK_OBJECT_HANDLE)));
    NSC_CHK(ns_ok(pulObjectCount, sizeof(CK_ULONG)));
    return C_FindObjects(hSession, phObject, ulMaxObjectCount, pulObjectCount);
}


CK_RV CSME_NSE_API C_CopyObject_nsc_call(struct C_CopyObject_nsc_args *args) {
    struct C_CopyObject_nsc_args a;
    struct nsc_tmpl tc;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.phNewObject, sizeof(CK_OBJECT_HANDLE)));
    rv = nsc_tmpl_prepare(a.pTemplate, a.ulCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = C_CopyObject(a.hSession, a.hObject, nsc_tmpl_arg(&tc), a.ulCount, a.phNewObject);
    nsc_tmpl_finish(&tc);
    return rv;
}


CK_RV CSME_NSE_API C_FindObjectsFinal_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_FindObjectsFinal(hSession);
}


CK_RV CSME_NSE_API C_EncryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_EncryptInit(hSession, nsc_mech_arg(&mc), hKey);
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_Encrypt_nsc_call(struct C_Encrypt_nsc_args *args) {
    struct C_Encrypt_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pData, a.ulDataLen));
    NSC_CHK(ns_outlen_begin(a.pEncryptedData, a.pulEncryptedDataLen, 1, &len));
    rv = C_Encrypt(a.hSession, a.pData, a.ulDataLen, a.pEncryptedData, &len);
    *a.pulEncryptedDataLen = len;
    return rv;
}



CK_RV CSME_NSE_API C_EncryptUpdate_nsc_call(struct C_EncryptUpdate_nsc_args *args) {
    struct C_EncryptUpdate_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pPart, a.ulPartLen));
    NSC_CHK(ns_outlen_begin(a.pEncryptedPart, a.pulEncryptedPartLen, 1, &len));
    rv = C_EncryptUpdate(a.hSession, a.pPart, a.ulPartLen, a.pEncryptedPart, &len);
    *a.pulEncryptedPartLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_EncryptFinal_nsc_call(CK_SESSION_HANDLE hSession,
                     CK_BYTE_PTR pLastEncryptedPart,
                     CK_ULONG_PTR pulLastEncryptedPartLen)
{
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pLastEncryptedPart, pulLastEncryptedPartLen, 1, &len));
    rv = C_EncryptFinal(hSession, pLastEncryptedPart, &len);
    *pulLastEncryptedPartLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_DecryptInit_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_DecryptInit(hSession, nsc_mech_arg(&mc), hKey);
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_Decrypt_nsc_call(struct C_Decrypt_nsc_args *args) {
    struct C_Decrypt_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pEncryptedData, a.ulEncryptedDataLen));
    NSC_CHK(ns_outlen_begin(a.pData, a.pulDataLen, 1, &len));
    rv = C_Decrypt(a.hSession, a.pEncryptedData, a.ulEncryptedDataLen, a.pData, &len);
    *a.pulDataLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_DecryptUpdate_nsc_call(struct C_DecryptUpdate_nsc_args *args) {
    struct C_DecryptUpdate_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pEncryptedPart, a.ulEncryptedPartLen));
    NSC_CHK(ns_outlen_begin(a.pPart, a.pulPartLen, 1, &len));
    rv = C_DecryptUpdate(a.hSession, a.pEncryptedPart, a.ulEncryptedPartLen, a.pPart, &len);
    *a.pulPartLen = len;
    return rv;
}



CK_RV CSME_NSE_API C_DecryptFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart,
                     CK_ULONG_PTR pulLastPartLen)
{
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pLastPart, pulLastPartLen, 1, &len));
    rv = C_DecryptFinal(hSession, pLastPart, &len);
    *pulLastPartLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_DigestInit_nsc_call(CK_SESSION_HANDLE hSession,
                                                                   CK_MECHANISM_PTR pMechanism)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_DigestInit(hSession, nsc_mech_arg(&mc));
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_Digest_nsc_call(struct C_Digest_nsc_args *args) {
    struct C_Digest_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pData, a.ulDataLen));
    NSC_CHK(ns_outlen_begin(a.pDigest, a.pulDigestLen, 1, &len));
    rv = C_Digest(a.hSession, a.pData, a.ulDataLen, a.pDigest, &len);
    *a.pulDigestLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_DigestUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
    NSC_CHK(ns_ok(pPart, ulPartLen));
    return C_DigestUpdate(hSession, pPart, ulPartLen);
}

CK_RV CSME_NSE_API C_DigestKey_nsc_call(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey)
{
    return C_DigestKey(hSession, hKey);
}

CK_RV CSME_NSE_API C_DigestFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen)
{
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pDigest, pulDigestLen, 1, &len));
    rv = C_DigestFinal(hSession, pDigest, &len);
    *pulDigestLen = len;
    return rv;
}


CK_RV CSME_NSE_API C_SignInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_SignInit(hSession, nsc_mech_arg(&mc), hKey);
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_Sign_nsc_call(struct C_Sign_nsc_args *args) {
    struct C_Sign_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pData, a.ulDataLen));
    NSC_CHK(ns_outlen_begin(a.pSignature, a.pulSignatureLen, 1, &len));
    rv = C_Sign(a.hSession, a.pData, a.ulDataLen, a.pSignature, &len);
    *a.pulSignatureLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_SignUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
    NSC_CHK(ns_ok(pPart, ulPartLen));
    return C_SignUpdate(hSession, pPart, ulPartLen);
}

CK_RV CSME_NSE_API C_SignFinal_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_outlen_begin(pSignature, pulSignatureLen, 1, &len));
    rv = C_SignFinal(hSession, pSignature, &len);
    *pulSignatureLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_SignRecoverInit_nsc_call(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_SignRecoverInit(hSession, nsc_mech_arg(&mc), hKey);
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_SignRecover_nsc_call(struct C_SignRecover_nsc_args *args) {
    struct C_SignRecover_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pData, a.ulDataLen));
    NSC_CHK(ns_outlen_begin(a.pSignature, a.pulSignatureLen, 1, &len));
    rv = C_SignRecover(a.hSession, a.pData, a.ulDataLen, a.pSignature, &len);
    *a.pulSignatureLen = len;
    return rv;
}

CK_RV CSME_NSE_API C_VerifyInit_nsc_call(CK_SESSION_HANDLE hSession,
                   CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_VerifyInit(hSession, nsc_mech_arg(&mc), hKey);
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_Verify_nsc_call(struct C_Verify_nsc_args *args) {
    struct C_Verify_nsc_args a;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pData, a.ulDataLen));
    NSC_CHK(ns_ok(a.pSignature, a.ulSignatureLen));
    return C_Verify(a.hSession, a.pData, a.ulDataLen, a.pSignature, a.ulSignatureLen);
}


CK_RV CSME_NSE_API C_VerifyUpdate_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart,
                     CK_ULONG ulPartLen)
{
    NSC_CHK(ns_ok(pPart, ulPartLen));
    return C_VerifyUpdate(hSession, pPart, ulPartLen);
}

CK_RV CSME_NSE_API C_VerifyFinal_nsc_call(CK_SESSION_HANDLE hSession,
                    CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
    NSC_CHK(ns_ok(pSignature, ulSignatureLen));
    return C_VerifyFinal(hSession, pSignature, ulSignatureLen);
}

CK_RV CSME_NSE_API C_VerifyRecoverInit_nsc_call(CK_SESSION_HANDLE hSession,
                          CK_MECHANISM_PTR pMechanism,
                          CK_OBJECT_HANDLE hKey)
{
    struct nsc_mech mc;
    CK_RV rv;

    rv = nsc_mech_prepare(pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_VerifyRecoverInit(hSession, nsc_mech_arg(&mc), hKey);
    nsc_mech_finish(&mc);
    return rv;
}

CK_RV CSME_NSE_API C_VerifyRecover_nsc_call(struct C_VerifyRecover_nsc_args *args) {
    struct C_VerifyRecover_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pSignature, a.ulSignatureLen));
    NSC_CHK(ns_outlen_begin(a.pData, a.pulDataLen, 1, &len));
    rv = C_VerifyRecover(a.hSession, a.pSignature, a.ulSignatureLen, a.pData, &len);
    *a.pulDataLen = len;
    return rv;
}



CK_RV CSME_NSE_API C_DigestEncryptUpdate_nsc_call(struct C_DigestEncryptUpdate_nsc_args *args) {
    struct C_DigestEncryptUpdate_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pPart, a.ulPartLen));
    NSC_CHK(ns_outlen_begin(a.pEncryptedPart, a.pulEncryptedPartLen, 1, &len));
    rv = C_DigestEncryptUpdate(a.hSession, a.pPart, a.ulPartLen, a.pEncryptedPart, &len);
    *a.pulEncryptedPartLen = len;
    return rv;
}


CK_RV CSME_NSE_API C_DecryptDigestUpdate_nsc_call(struct C_DecryptDigestUpdate_nsc_args *args) {
    struct C_DecryptDigestUpdate_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pEncryptedPart, a.ulEncryptedPartLen));
    NSC_CHK(ns_outlen_begin(a.pPart, a.pulPartLen, 1, &len));
    rv = C_DecryptDigestUpdate(a.hSession, a.pEncryptedPart, a.ulEncryptedPartLen, a.pPart, &len);
    *a.pulPartLen = len;
    return rv;
}



CK_RV CSME_NSE_API C_SignEncryptUpdate_nsc_call(struct C_SignEncryptUpdate_nsc_args *args) {
    struct C_SignEncryptUpdate_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pPart, a.ulPartLen));
    NSC_CHK(ns_outlen_begin(a.pEncryptedPart, a.pulEncryptedPartLen, 1, &len));
    rv = C_SignEncryptUpdate(a.hSession, a.pPart, a.ulPartLen, a.pEncryptedPart, &len);
    *a.pulEncryptedPartLen = len;
    return rv;
}



CK_RV CSME_NSE_API C_DecryptVerifyUpdate_nsc_call(struct C_DecryptVerifyUpdate_nsc_args *args) {
    struct C_DecryptVerifyUpdate_nsc_args a;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pEncryptedPart, a.ulEncryptedPartLen));
    NSC_CHK(ns_outlen_begin(a.pPart, a.pulPartLen, 1, &len));
    rv = C_DecryptVerifyUpdate(a.hSession, a.pEncryptedPart, a.ulEncryptedPartLen, a.pPart, &len);
    *a.pulPartLen = len;
    return rv;
}



CK_RV CSME_NSE_API C_GenerateKey_nsc_call(struct C_GenerateKey_nsc_args *args) {
    struct C_GenerateKey_nsc_args a;
    struct nsc_mech mc;
    struct nsc_tmpl tc;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.phKey, sizeof(CK_OBJECT_HANDLE)));
    rv = nsc_tmpl_prepare(a.pTemplate, a.ulCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = nsc_mech_prepare(a.pMechanism, &mc);
    if (rv != CKR_OK) {
        nsc_tmpl_free(&tc);
        return rv;
    }
    rv = C_GenerateKey(a.hSession, nsc_mech_arg(&mc), nsc_tmpl_arg(&tc), a.ulCount, a.phKey);
    nsc_mech_finish(&mc);
    nsc_tmpl_finish(&tc);
    return rv;
}



CK_RV CSME_NSE_API C_GenerateKeyPair_nsc_call(struct C_GenerateKeyPair_nsc_args *args) {
    struct C_GenerateKeyPair_nsc_args a;
    struct nsc_mech mc;
    struct nsc_tmpl tpub, tpriv;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.phPublicKey, sizeof(CK_OBJECT_HANDLE)));
    NSC_CHK(ns_ok(a.phPrivateKey, sizeof(CK_OBJECT_HANDLE)));
    rv = nsc_tmpl_prepare(a.pPublicKeyTemplate, a.ulPublicKeyAttributeCount, 0, &tpub);
    if (rv != CKR_OK)
        return rv;
    rv = nsc_tmpl_prepare(a.pPrivateKeyTemplate, a.ulPrivateKeyAttributeCount, 0, &tpriv);
    if (rv != CKR_OK) {
        nsc_tmpl_free(&tpub);
        return rv;
    }
    rv = nsc_mech_prepare(a.pMechanism, &mc);
    if (rv != CKR_OK) {
        nsc_tmpl_free(&tpriv);
        nsc_tmpl_free(&tpub);
        return rv;
    }
    rv = C_GenerateKeyPair(a.hSession, nsc_mech_arg(&mc), nsc_tmpl_arg(&tpub), a.ulPublicKeyAttributeCount, nsc_tmpl_arg(&tpriv), a.ulPrivateKeyAttributeCount, a.phPublicKey, a.phPrivateKey);
    nsc_mech_finish(&mc);
    nsc_tmpl_finish(&tpriv);
    nsc_tmpl_finish(&tpub);
    return rv;
}



CK_RV CSME_NSE_API C_WrapKey_nsc_call(struct C_WrapKey_nsc_args *args) {
    struct C_WrapKey_nsc_args a;
    struct nsc_mech mc;
    CK_ULONG len;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_outlen_begin(a.pWrappedKey, a.pulWrappedKeyLen, 1, &len));
    rv = nsc_mech_prepare(a.pMechanism, &mc);
    if (rv != CKR_OK)
        return rv;
    rv = C_WrapKey(a.hSession, nsc_mech_arg(&mc), a.hWrappingKey, a.hKey, a.pWrappedKey, &len);
    *a.pulWrappedKeyLen = len;
    nsc_mech_finish(&mc);
    return rv;
}



CK_RV CSME_NSE_API C_UnwrapKey_nsc_call(struct C_UnwrapKey_nsc_args *args) {
    struct C_UnwrapKey_nsc_args a;
    struct nsc_mech mc;
    struct nsc_tmpl tc;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    NSC_CHK(ns_ok(a.pWrappedKey, a.ulWrappedKeyLen));
    NSC_CHK(ns_ok(a.phKey, sizeof(CK_OBJECT_HANDLE)));
    rv = nsc_tmpl_prepare(a.pTemplate, a.ulAttributeCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = nsc_mech_prepare(a.pMechanism, &mc);
    if (rv != CKR_OK) {
        nsc_tmpl_free(&tc);
        return rv;
    }
    rv = C_UnwrapKey(a.hSession, nsc_mech_arg(&mc), a.hUnwrappingKey, a.pWrappedKey, a.ulWrappedKeyLen, nsc_tmpl_arg(&tc), a.ulAttributeCount, a.phKey);
    nsc_mech_finish(&mc);
    nsc_tmpl_finish(&tc);
    return rv;
}



CK_RV CSME_NSE_API C_DeriveKey_nsc_call(struct C_DeriveKey_nsc_args *args) {
    struct C_DeriveKey_nsc_args a;
    struct nsc_mech mc;
    struct nsc_tmpl tc;
    CK_RV rv;

    NSC_CHK(ns_ok(args, sizeof(*args)));
    a = *args;
    /* phKey may legitimately be NULL (e.g. CKM_TLS12_KEY_AND_MAC_DERIVE); only
     * validate it when the caller actually supplies an output location. */
    NSC_CHK_OPT(a.phKey, sizeof(CK_OBJECT_HANDLE));
    rv = nsc_tmpl_prepare(a.pTemplate, a.ulAttributeCount, 0, &tc);
    if (rv != CKR_OK)
        return rv;
    rv = nsc_mech_prepare(a.pMechanism, &mc);
    if (rv != CKR_OK) {
        nsc_tmpl_free(&tc);
        return rv;
    }
    rv = C_DeriveKey(a.hSession, nsc_mech_arg(&mc), a.hBaseKey, nsc_tmpl_arg(&tc), a.ulAttributeCount, a.phKey);
    nsc_mech_finish(&mc);
    nsc_tmpl_finish(&tc);
    return rv;
}

CK_RV CSME_NSE_API C_SeedRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed, CK_ULONG ulSeedLen)
{
    NSC_CHK(ns_ok(pSeed, ulSeedLen));
    return C_SeedRandom(hSession, pSeed, ulSeedLen);
}

CK_RV CSME_NSE_API C_GenerateRandom_nsc_call(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen)
{
    NSC_CHK(ns_ok(pRandomData, ulRandomLen));
    return C_GenerateRandom(hSession, pRandomData, ulRandomLen);
}

CK_RV CSME_NSE_API C_GetFunctionStatus_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_GetFunctionStatus(hSession);
}

CK_RV CSME_NSE_API C_CancelFunction_nsc_call(CK_SESSION_HANDLE hSession)
{
    return C_CancelFunction(hSession);
}

CK_RV CSME_NSE_API C_WaitForSlotEvent_nsc_call(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot, CK_VOID_PTR pReserved)
{
    /* pReserved must be NULL; the underlying call rejects anything else. */
    NSC_CHK(ns_ok(pSlot, sizeof(CK_SLOT_ID)));
    return C_WaitForSlotEvent(flags, pSlot, pReserved);
}

#endif /* SECURE_PKCS11 */
