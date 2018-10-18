# ===========================================================================
#      https://github.com/BrianAker/ddm4/
# ===========================================================================
#
# SYNOPSIS
#
#   AX_COVERAGE()
#
# DESCRIPTION
#
#   --enable-coverage
#
# LICENSE
#
#   Copyright (c) 2016 Sean Parkinson <sean@wolfssl.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

AC_DEFUN([AX_COVERAGE],
    [AC_PREREQ([2.63])dnl
    AC_ARG_ENABLE([coverage],
      [AS_HELP_STRING([--enable-coverage],
        [Build code to generate coverage statistics (yes|no) @<:@default=no@:>@])],
      [ax_enable_coverage=$enableval],
      [ax_enable_coverage=no])

	AS_IF([test "x$ax_enable_coverage" = xyes],
		[AC_DEFINE([COVERAGE],[1],[Define to 1 to enable coverage build.])],
		[AC_SUBST([MCHECK])
         AC_DEFINE([COVERAGE],[0],[Define to 1 to enable coverage build.])])

    AC_MSG_CHECKING([for coverage])
    AC_MSG_RESULT([$ax_enable_coverage])
    AM_CONDITIONAL([COVERAGE],[test "x${ax_enable_coverage}" = xyes])])
