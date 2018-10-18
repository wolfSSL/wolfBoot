/* visibility.h
 *
 * Copyright (C) 2006-2017 wolfSSL Inc.
 *
 * This file is part of wolfPKCS11.
 *
 * wolfPKCS11 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfPKCS11 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */


/* Visibility control macros */

#ifndef WOLFPKCS11_VISIBILITY_H
#define WOLFPKCS11_VISIBILITY_H


/* WP11_API is used for the public API symbols.
        It either imports or exports (or does nothing for static builds)

   WP11_LOCAL is used for non-API symbols (private).
*/

#if defined(BUILDING_WOLFPKCS11)
    #if defined(HAVE_VISIBILITY) && HAVE_VISIBILITY
        #define WP11_API   __attribute__ ((visibility("default")))
        #define WP11_LOCAL __attribute__ ((visibility("hidden")))
    #elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x550)
        #define WP11_API   __global
        #define WP11_LOCAL __hidden
    #elif defined(_MSC_VER) || defined(__MINGW32__)
        #if defined(WP11_DLL)
            #define WP11_API __declspec(dllexport)
        #else
            #define WP11_API
        #endif
        #define WP11_LOCAL
    #else
        #define WP11_API
        #define WP11_LOCAL
    #endif /* HAVE_VISIBILITY */
#else /* BUILDING_WOLFPKCS11 */
    #if defined(_MSC_VER) || defined(__MINGW32__)
        #if defined(WP11_DLL)
            #define WP11_API __declspec(dllimport)
        #else
            #define WP11_API
        #endif
        #define WP11_LOCAL
    #else
        #define WP11_API
        #define WP11_LOCAL
    #endif
#endif /* BUILDING_WOLFPKCS11 */

#endif /* WOLFPKCS11_VISIBILITY_H */

