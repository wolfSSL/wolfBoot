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
echo " * Copyright (C) 2006-2018 wolfSSL Inc." >> $OPTION_FILE
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

