#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([overwitch],[0.3],[dagargo@gmail.com])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])
AM_PROG_LIBTOOL
AC_SEARCH_LIBS([sqrt], [m])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(libusb, libusb, HAVE_LIBUSB=1, HAVE_LIBUSB=0)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)

# Checks for header files.
AC_CHECK_HEADERS([stdio.h unistd.h stdlib.h errno.h string.h signal.h libusb.h limits.h])

PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.2, ac_cv_sndfile=1, ac_cv_sndfile=0)
AC_DEFINE_UNQUOTED([HAVE_SNDFILE],${ac_cv_sndfile}, [Set to 1 if you have libsndfile.])
AC_SUBST(SNDFILE_CFLAGS)
AC_SUBST(SNDFILE_LIBS)

PKG_CHECK_MODULES(SAMPLERATE, samplerate >= 0.1.9, ac_cv_samplerate=1, ac_cv_samplerate=0)
AC_DEFINE_UNQUOTED([HAVE_SAMPLERATE],${ac_cv_samplerate}, [Set to 1 if you have libsamplerate.])
AC_SUBST(SAMPLERATE_CFLAGS)
AC_SUBST(SAMPLERATE_LIBS)

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_MALLOC

etc_udev="/etc/udev"
AC_CHECK_FILE([$etc_udev],
udev="yes",
udev="no"
)
AM_CONDITIONAL([UDEV], [test $udev = "yes"])

AC_CONFIG_FILES([Makefile src/Makefile udev/Makefile])
AC_OUTPUT
