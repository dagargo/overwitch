#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([overwitch],[0.1],[dagargo@gmail.com])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(libusb, libusb, HAVE_LIBUSB=1, HAVE_LIBUSB=0)

# Checks for header files.
AC_CHECK_HEADERS([stdio.h unistd.h stdlib.h errno.h string.h signal.h libusb.h limits.h])

PKG_CHECK_MODULES(SAMPLERATE, samplerate >= 0.1.9, ac_cv_samplerate=1, ac_cv_samplerate=0)
AC_DEFINE_UNQUOTED([HAVE_SAMPLERATE],${ac_cv_samplerate}, [Set to 1 if you have libsamplerate.])
AC_SUBST(SAMPLERATE_CFLAGS)
AC_SUBST(SAMPLERATE_LIBS)

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_MALLOC

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
