# configure.ac
#
# Copyright (C) 2018 wolfSSL Inc.
# All right reserved.
#
# This file is part of wolfPKCS11.
#
AC_COPYRIGHT([Copyright (C) 2014-2018 wolfSSL Inc.])
AC_PREREQ([2.63])
AC_INIT([wolfpkcs11],[1.0.0],[https://github.com/wolfssl/wolfpkcs11/issues],[wolfpkcs11],[http://www.wolfssl.com])
AC_CONFIG_AUX_DIR([build-aux])

# The following sets CFLAGS to empty if unset on command line.
: ${CFLAGS=""}

# Capture user C_EXTRA_FLAGS from configure line.
# Use of C_EXTRA_FLAGS is deprecated because CFLAGS was fixed but someone
# might still be using it.
CFLAGS="$CFLAGS $C_EXTRA_FLAGS $C_FLAGS"

# Test ar for the "U" option. Should be checked before the libtool macros.
xxx_ar_flags=$((ar --help) 2>&1)
AS_CASE([$xxx_ar_flags],[*'use actual timestamps and uids/gids'*],[: ${AR_FLAGS="Ucru"}])

AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([1.11 -Wall -Werror -Wno-portability foreign tar-ustar subdir-objects no-define color-tests])

AC_ARG_PROGRAM
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([wolfpkcs11/config.h])

WOLFPKCS11_LIBRARY_VERSION=1:0:0
#                        | | |
#                 +------+ | +---+
#                 |        |     |
#                current:revision:age
#                 |        |     |
#                 |        |     +- increment if interfaces have been added
#                 |        |        set to zero if interfaces have been removed
#                 |        |        or changed
#                 |        +- increment if source code has changed
#                 |           set to zero if current is incremented
#                 +- increment if interfaces have been added, removed or changed
AC_SUBST([WOLFPKCS11_LIBRARY_VERSION])


LT_PREREQ([2.2])
LT_INIT([disable-static], [win32-dll])
LT_LANG([C])

gl_VISIBILITY
AS_IF([test -n "$CFLAG_VISIBILITY"],
      [AM_CPPFLAGS="$AM_CPPFLAGS $CFLAG_VISIBILITY"
       CPPFLAGS="$CPPFLAGS $CFLAG_VISIBILITY"])

# silent
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_SIZEOF([long long], 8)
AC_CHECK_SIZEOF([long], 4)

# Check headers/libs
AC_CHECK_FUNCS([gethostbyname getaddrinfo gettimeofday inet_ntoa memset socket])
AC_CHECK_LIB([network],[socket])

# DEBUG
DEBUG_CFLAGS="-g -O0 -DDEBUG_WOLFPKCS11"

# Optimizations
OPTIMIZE_CFLAGS="-O2"

# Coverage
COVERAGE_CFLAGS="--coverage"

AX_DEBUG
AS_IF([test "x$ax_enable_debug" != "xno"],
      [AM_CFLAGS="$DEBUG_CFLAGS $AM_CFLAGS -DDEBUG"],
      [AM_CFLAGS="$AM_CFLAGS $OPTIMIZE_CFLAGS -DNDEBUG"])

#COVERAGE
AX_COVERAGE
AS_IF([test "$ax_enable_coverage" = "yes"],
      [AM_CFLAGS="$COVERAGE_CFLAGS $AM_CFLAGS"])


# Checks for typedefs, structures, and compiler characteristics.
if test "$ac_cv_sizeof_long" = "8"; then
   AM_CFLAGS="$AM_CFLAGS -DSIZEOF_LONG=8"
else
    if test "$ac_cv_sizeof_long_long" = "8"; then
        AM_CFLAGS="$AM_CFLAGS -DSIZEOF_LONG_LONG=8"
    fi
fi


# SINGLE THREADED
AC_ARG_ENABLE([singlethreaded],
    [AS_HELP_STRING([--enable-singlethreaded],[Enable wolfPKCS11 single threaded (default: disabled)])],
    [ ENABLED_SINGLETHREADED=$enableval ],
    [ ENABLED_SINGLETHREADED=no ])

AS_IF([ test "x$ENABLED_SINGLETHREADED" = "xno" ],[
       AX_PTHREAD([
                   AC_DEFINE([HAVE_PTHREAD], [1], [Define if you have POSIX threads libraries and header files.])
                   # If AX_PTHREAD is adding -Qunused-arguments, need to prepend with -Xcompiler libtool will use it. Newer
                   # versions of clang don't need the -Q flag when using pthreads.
                   AS_CASE([$PTHREAD_CFLAGS],[-Qunused-arguments*],[PTHREAD_CFLAGS="-Xcompiler $PTHREAD_CFLAGS"])
                   AM_CFLAGS="-D_POSIX_THREADS $AM_CFLAGS $PTHREAD_CFLAGS"
                   LIBS="$LIBS $PTHREAD_LIBS"
                   ],[
                      ENABLED_SINGLETHREADED=yes
                      ])
      ])

AS_IF([ test "x$ENABLED_SINGLETHREADED" = "xyes" ],[ AM_CFLAGS="-DSINGLE_THREADED $AM_CFLAGS" ])


AC_ARG_ENABLE([rsa],
    [AS_HELP_STRING([--enable-rsa],[Enable RSA (default: enabled)])],
    [ ENABLED_RSA=$enableval ],
    [ ENABLED_RSA=yes ]
    )
if test "$ENABLED_RSA" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_RSA"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_RSA"
fi

AC_ARG_ENABLE([rsaoaep],
    [AS_HELP_STRING([--enable-rsaoaep],[Enable RSA-OAEP (default: enabled)])],
    [ ENABLED_RSAOAEP=$enableval ],
    [ ENABLED_RSAOAEP=yes ]
    )
if test "$ENABLED_RSA" = "no" || test "$ENABLED_RSAOAEP" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DWC_NO_RSA_OAEP"
else
    DISABLE_DEFS="$DISABLE_DEFS -DWC_NO_RSA_OAEP"
fi

AC_ARG_ENABLE([rsapss],
    [AS_HELP_STRING([--enable-rsapss],[Enable RSA-PSS (default: enabled)])],
    [ ENABLED_RSAPSS=$enableval ],
    [ ENABLED_RSAPSS=yes ]
    )
if test "$ENABLED_RSA" = "yes" && test "$ENABLED_RSAPSS" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DWC_RSA_PSS"
else
    DISABLE_DEFS="$DISABLE_DEFS -DWC_RSA_PSS"
fi

AC_ARG_ENABLE([keygen],
    [AS_HELP_STRING([--enable-keygen],[Enable Key Generation (default: enabled)])],
    [ ENABLED_KEYGEN=$enableval ],
    [ ENABLED_KEYGEN=yes ]
    )
if test "$ENABLED_RSA" = "yes" && test "$ENABLED_KEYGEN" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DWOLFSSL_KEY_GEN"
else
    DISABLE_DEFS="$DISABLE_DEFS -DWOLFSSL_KEY_GEN"
fi

AC_ARG_ENABLE([ecc],
    [AS_HELP_STRING([--enable-ecc],[Enable ECC (default: enabled)])],
    [ ENABLED_ECC=$enableval ],
    [ ENABLED_ECC=yes ]
    )
if test "$ENABLED_ECC" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DHAVE_ECC"
else
    DISABLE_DEFS="$DISABLE_DEFS -DHAVE_ECC"
fi

AC_ARG_ENABLE([dh],
    [AS_HELP_STRING([--enable-dh],[Enable RSA (default: enabled)])],
    [ ENABLED_DH=$enableval ],
    [ ENABLED_DH=yes ]
    )
if test "$ENABLED_DH" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_DH"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_DH"
fi

AC_ARG_ENABLE([aes],
    [AS_HELP_STRING([--enable-aes],[Enable AES (default: enabled)])],
    [ ENABLED_AES=$enableval ],
    [ ENABLED_AES=yes ]
    )
if test "$ENABLED_AES" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_AES"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_AES"
fi

AC_ARG_ENABLE([aescbc],
    [AS_HELP_STRING([--enable-aescbc],[Enable AES-CBC (default: enabled)])],
    [ ENABLED_AESCBC=$enableval ],
    [ ENABLED_AESCBC=yes ]
    )
if test "$ENABLED_AES" = "no" || test "$ENABLED_AESCBC" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_AES_CBC"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_AES_CBC"
fi

AC_ARG_ENABLE([aesgcm],
    [AS_HELP_STRING([--enable-aesgcm],[Enable AES-GCM (default: enabled)])],
    [ ENABLED_AESGCM=$enableval ],
    [ ENABLED_AESGCM=yes ]
    )
if test "$ENABLED_AES" = "yes" && test "$ENABLED_AESGCM" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DHAVE_AESGCM"
else
    DISABLE_DEFS="$DISABLE_DEFS -DHAVE_AESGCM"
fi

AC_ARG_ENABLE([hmac],
    [AS_HELP_STRING([--enable-hmac],[Enable HMAC (default: enabled)])],
    [ ENABLED_HMAC=$enableval ],
    [ ENABLED_HMAC=yes ]
    )
if test "$ENABLED_HMAC" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_HMAC"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_HMAC"
fi

AC_ARG_ENABLE([md5],
    [AS_HELP_STRING([--enable-md5],[Enable MD5 (default: enabled)])],
    [ ENABLED_MD5=$enableval ],
    [ ENABLED_MD5=yes ]
    )
if test "$ENABLED_MD5" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_MD5"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_MD5"
fi

AC_ARG_ENABLE([sha],
    [AS_HELP_STRING([--enable-sha],[Enable SHA1 (default: enabled)])],
    [ ENABLED_SHA1=$enableval ],
    [ ENABLED_SHA1=yes ]
    )
AC_ARG_ENABLE([sha1],
    [AS_HELP_STRING([--enable-sha1],[Enable SHA1 (default: enabled)])],
    [ ENABLED_SHA1=$enableval ],
    [ ENABLED_SHA1=yes ]
    )
if test "$ENABLED_SHA1" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_SHA"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_SHA"
fi

AC_ARG_ENABLE([sha224],
    [AS_HELP_STRING([--enable-sha224],[Enable SHA-224 (default: enabled)])],
    [ ENABLED_SHA224=$enableval ],
    [ ENABLED_SHA224=yes ]
    )
if test "$ENABLED_SHA224" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DWOLFSSL_SHA224"
else
    DISABLE_DEFS="$DISABLE_DEFS -DWOLFSSL_SHA224"
fi

AC_ARG_ENABLE([sha256],
    [AS_HELP_STRING([--enable-sha256],[Enable SHA-256 (default: enabled)])],
    [ ENABLED_SHA256=$enableval ],
    [ ENABLED_SHA256=yes ]
    )
if test "$ENABLED_SHA256" = "no"
then
    AM_CFLAGS="$AM_CFLAGS -DNO_SHA256"
else
    DISABLE_DEFS="$DISABLE_DEFS -DNO_SHA256"
fi

AC_ARG_ENABLE([sha384],
    [AS_HELP_STRING([--enable-sha384],[Enable SHA-384 (default: enabled)])],
    [ ENABLED_SHA384=$enableval ],
    [ ENABLED_SHA384=yes ]
    )
if test "$ENABLED_SHA384" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DWOLFSSL_SHA384"
else
    DISABLE_DEFS="$DISABLE_DEFS -DWOLFSSL_SHA384"
fi

AC_ARG_ENABLE([sha512],
    [AS_HELP_STRING([--enable-sha512],[Enable SHA-512 (default: enabled)])],
    [ ENABLED_SHA512=$enableval ],
    [ ENABLED_SHA512=yes ]
    )
if test "$ENABLED_SHA512" = "yes"
then
    AM_CFLAGS="$AM_CFLAGS -DWOLFSSL_SHA512"
else
    DISABLE_DEFS="$DISABLE_DEFS -DWOLFSSL_SHA512"
fi

if test "$enable_shared" = "no"; then
    AM_CFLAGS="$AM_CFLAGS -DHAVE_PKCS11_STATIC"
fi

AM_CONDITIONAL([BUILD_STATIC],[test "x$enable_shared" = "xno"])



# HARDEN FLAGS
AX_HARDEN_CC_COMPILER_FLAGS

OPTION_FLAGS="$CFLAGS $CPPFLAGS $AM_CFLAGS"

LIBS="$LIBS -lwolfssl -ldl -lm"

CREATE_HEX_VERSION
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_LDFLAGS])

# FINAL
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([wolfpkcs11/version.h])
AC_CONFIG_FILES([wolfpkcs11/options.h])

AX_CREATE_GENERIC_CONFIG
AX_AM_JOBSERVER([yes])

AC_OUTPUT

# force make clean
echo "---"
echo "Running make clean..."
make clean >/dev/null 2>&1
echo

# generate user options header
echo "---"
echo "Generating user options header..."


OPTION_FILE="wolfpkcs11/options.h"
rm -f $OPTION_FILE

echo "/* wolfpkcs11 options.h" > $OPTION_FILE
echo " * generated from configure options" >> $OPTION_FILE
echo " *" >> $OPTION_FILE
echo " * Copyright (C) 2006-2021 wolfSSL Inc." >> $OPTION_FILE
echo " *" >> $OPTION_FILE
echo " * * This file is part of wolfPKCS11." >> $OPTION_FILE
echo " *" >> $OPTION_FILE
echo " */" >> $OPTION_FILE

echo "" >> $OPTION_FILE
echo "#ifndef WOLFPKCS11_OPTIONS_H" >> $OPTION_FILE
echo "#define WOLFPKCS11_OPTIONS_H" >> $OPTION_FILE
echo "" >> $OPTION_FILE
echo "" >> $OPTION_FILE
echo "#ifdef __cplusplus" >> $OPTION_FILE
echo "extern \"C\" {" >> $OPTION_FILE
echo "#endif" >> $OPTION_FILE
echo "" >> $OPTION_FILE

for option in $OPTION_FLAGS; do
    defonly=`echo $option | sed 's/-D//'`
    if test "$defonly" != "$option"
    then
        noequalsign=`echo $defonly | sed 's/=/ /'`
        if test "$noequalsign" = "NDEBUG" || test "$noequalsign" = "DEBUG"
        then
            echo "not outputting (N)DEBUG to $OPTION_FILE"
            continue
        fi

        # allow user to igonore system options
        ignoresys=no
        if [[[ $noequalsign == _* ]]] ;
        then
            ignoresys=yes
            echo "#ifndef WOLFSSL_OPTIONS_IGNORE_SYS" >> $OPTION_FILE
        fi

        noarg=`echo $defonly | sed 's/=.*//'`
        echo "#undef  $noarg" >> $OPTION_FILE
        echo "#define $noequalsign" >> $OPTION_FILE

        if test "$ignoresys" = "yes"
        then
            echo "#endif" >> $OPTION_FILE
        fi

        echo "" >> $OPTION_FILE
#    else
#        echo "option w/o begin -D is $option, not saving to $OPTION_FILE"
    fi
done

for option in $DISABLE_DEFS; do
    defonly=`echo $option | sed 's/-D//'`
    if test "$defonly" != "$option"
    then
        noequalsign=`echo $defonly | sed 's/=/ /'`
        if test "$noequalsign" = "NDEBUG" || test "$noequalsign" = "DEBUG"
        then
            echo "not outputting (N)DEBUG to $OPTION_FILE"
            continue
        fi

        # allow user to igonore system options
        ignoresys=no
        if [[[ $noequalsign == _* ]]] ;
        then
            ignoresys=yes
            echo "#ifndef WOLFSSL_OPTIONS_IGNORE_SYS" >> $OPTION_FILE
        fi

        noarg=`echo $defonly | sed 's/=.*//'`
        echo "#undef  $noarg" >> $OPTION_FILE

        if test "$ignoresys" = "yes"
        then
            echo "#endif" >> $OPTION_FILE
        fi

        echo "" >> $OPTION_FILE
    else
        echo "option w/o begin -D is $option, not saving to $OPTION_FILE"
    fi
done

echo "" >> $OPTION_FILE
echo "#ifdef __cplusplus" >> $OPTION_FILE
echo "}" >> $OPTION_FILE
echo "#endif" >> $OPTION_FILE
echo "" >> $OPTION_FILE
echo "" >> $OPTION_FILE
echo "#endif /* WOLFPKCS11_OPTIONS_H */" >> $OPTION_FILE
echo "" >> $OPTION_FILE
echo


# output config summary
echo "---"
echo "Configuration summary for $PACKAGE_NAME version $VERSION"
echo ""
echo "   * Installation prefix:       $prefix"
echo "   * System type:               $host_vendor-$host_os"
echo "   * Host CPU:                  $host_cpu"
echo "   * C Compiler:                $CC"
echo "   * C Flags:                   $CFLAGS"
echo "   * CPP Flags:                 $CPPFLAGS"
echo "   * Linker Flags:              $LDFLAGS"
echo "   * LIB Flags:                 $LIB"
echo "   Features "
echo "   * Single threaded:            $ENABLED_SINGLETHREADED"
echo "   * AES:                        $ENABLED_AES"
echo "   * AES-CBC:                    $ENABLED_AESCBC"
echo "   * AES-GCM:                    $ENABLED_AESGCM"
echo "   * MD5:                        $ENABLED_MD5"
echo "   * SHA:                        $ENABLED_SHA"
echo "   * SHA-224:                    $ENABLED_SHA224"
echo "   * SHA-256:                    $ENABLED_SHA256"
echo "   * SHA-384:                    $ENABLED_SHA384"
echo "   * SHA-512:                    $ENABLED_SHA512"
echo "   * HMAC:                       $ENABLED_HMAC"
echo "   * RSA:                        $ENABLED_RSA"
echo "   * RSA-OAEP:                   $ENABLED_RSAOAEP"
echo "   * RSA-PSS:                    $ENABLED_RSAPSS"
echo "   * DH:                         $ENABLED_DH"
echo "   * ECC:                        $ENABLED_ECC"

