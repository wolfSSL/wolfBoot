/* error.h
 *
 * Minimal PSA error definitions for wolfBoot Zephyr integration.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WOLFBOOT_PSA_ERROR_H_
#define WOLFBOOT_PSA_ERROR_H_

#include <stdint.h>

typedef int32_t psa_status_t;

#ifndef PSA_SUCCESS
#define PSA_SUCCESS                    ((psa_status_t)0)
#endif
#ifndef PSA_ERROR_PROGRAMMER_ERROR
#define PSA_ERROR_PROGRAMMER_ERROR     ((psa_status_t)-129)
#endif
#ifndef PSA_ERROR_CONNECTION_REFUSED
#define PSA_ERROR_CONNECTION_REFUSED   ((psa_status_t)-130)
#endif
#ifndef PSA_ERROR_CONNECTION_BUSY
#define PSA_ERROR_CONNECTION_BUSY      ((psa_status_t)-131)
#endif
#ifndef PSA_ERROR_INVALID_ARGUMENT
#define PSA_ERROR_INVALID_ARGUMENT     ((psa_status_t)-135)
#endif
#ifndef PSA_ERROR_INVALID_HANDLE
#define PSA_ERROR_INVALID_HANDLE       ((psa_status_t)-136)
#endif
#ifndef PSA_ERROR_BAD_STATE
#define PSA_ERROR_BAD_STATE            ((psa_status_t)-137)
#endif
#ifndef PSA_ERROR_BUFFER_TOO_SMALL
#define PSA_ERROR_BUFFER_TOO_SMALL     ((psa_status_t)-138)
#endif
#ifndef PSA_ERROR_ALREADY_EXISTS
#define PSA_ERROR_ALREADY_EXISTS       ((psa_status_t)-139)
#endif
#ifndef PSA_ERROR_DOES_NOT_EXIST
#define PSA_ERROR_DOES_NOT_EXIST       ((psa_status_t)-140)
#endif
#ifndef PSA_ERROR_INSUFFICIENT_MEMORY
#define PSA_ERROR_INSUFFICIENT_MEMORY  ((psa_status_t)-141)
#endif
#ifndef PSA_ERROR_INSUFFICIENT_STORAGE
#define PSA_ERROR_INSUFFICIENT_STORAGE ((psa_status_t)-142)
#endif
#ifndef PSA_ERROR_INSUFFICIENT_DATA
#define PSA_ERROR_INSUFFICIENT_DATA    ((psa_status_t)-143)
#endif
#ifndef PSA_ERROR_SERVICE_FAILURE
#define PSA_ERROR_SERVICE_FAILURE      ((psa_status_t)-144)
#endif
#ifndef PSA_ERROR_COMMUNICATION_FAILURE
#define PSA_ERROR_COMMUNICATION_FAILURE ((psa_status_t)-145)
#endif
#ifndef PSA_ERROR_STORAGE_FAILURE
#define PSA_ERROR_STORAGE_FAILURE      ((psa_status_t)-146)
#endif
#ifndef PSA_ERROR_HARDWARE_FAILURE
#define PSA_ERROR_HARDWARE_FAILURE     ((psa_status_t)-147)
#endif
#ifndef PSA_ERROR_INVALID_SIGNATURE
#define PSA_ERROR_INVALID_SIGNATURE    ((psa_status_t)-149)
#endif
#ifndef PSA_ERROR_NOT_SUPPORTED
#define PSA_ERROR_NOT_SUPPORTED        ((psa_status_t)-134)
#endif
#ifndef PSA_ERROR_GENERIC_ERROR
#define PSA_ERROR_GENERIC_ERROR        ((psa_status_t)-132)
#endif

#endif /* WOLFBOOT_PSA_ERROR_H_ */
