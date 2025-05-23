#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([overwitch],[2.1],[dagargo@gmail.com])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])
LT_INIT
AC_SEARCH_LIBS([sqrt], [m])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([subdir-objects])

AC_DEFINE([PACKAGE_APP_DBUS_NAME], ["io.github.dagargo.Overwitch"], "Application name for D-Bus")
AC_DEFINE([PACKAGE_SERVICE_DBUS_NAME], ["io.github.dagargo.OverwitchService"], "Service name for D-Bus")

# Checks for programs.
AC_PROG_CC

# Define conditional prior to package checks
AM_CONDITIONAL([CLI_ONLY], [test "${CLI_ONLY}" == yes])
AM_CONDITIONAL([GUI], [test "${CLI_ONLY}" != yes])

AM_COND_IF(GUI, [PKG_CHECK_MODULES(GTK4, gtk4 >= 4.14)])

# Checks for libraries.
PKG_CHECK_MODULES(libusb, libusb, HAVE_LIBUSB=1, HAVE_LIBUSB=0)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)

AC_CHECK_LIB([cunit], [CU_initialize_registry])

# Checks for header files.
AC_CHECK_HEADERS([stdio.h unistd.h stdlib.h errno.h string.h signal.h libusb.h limits.h time.h])

PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.2, ac_cv_sndfile=1, ac_cv_sndfile=0)
AC_DEFINE_UNQUOTED([HAVE_SNDFILE],${ac_cv_sndfile}, [Set to 1 if you have libsndfile.])
AC_SUBST(SNDFILE_CFLAGS)
AC_SUBST(SNDFILE_LIBS)

PKG_CHECK_MODULES(SAMPLERATE, samplerate >= 0.1.9, ac_cv_samplerate=1, ac_cv_samplerate=0)
AC_DEFINE_UNQUOTED([HAVE_SAMPLERATE],${ac_cv_samplerate}, [Set to 1 if you have libsamplerate.])
AC_SUBST(SAMPLERATE_CFLAGS)
AC_SUBST(SAMPLERATE_LIBS)

AM_COND_IF(GUI, [
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19])
])

AC_ARG_WITH([systemduserconfdir],
     [AS_HELP_STRING([--with-systemduserconfdir=DIR], [Directory for systemd service files])],,
     [with_systemduserconfdir=auto])
AS_IF([test "x$with_systemduserconfdir" = "xyes" -o "x$with_systemduserconfdir" = "xauto"], [
     def_systemduserconfdir=$($PKG_CONFIG --variable=systemduserconfdir systemd)

     AS_IF([test "x$def_systemduserconfdir" = "x"],
   [AS_IF([test "x$with_systemduserconfdir" = "xyes"],
    [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
    with_systemduserconfdir=no],
   [with_systemduserconfdir="$def_systemduserconfdir"])])
AS_IF([test "x$with_systemduserconfdir" != "xno"],
      [AC_SUBST([systemduserconfdir], [$with_systemduserconfdir])])
AM_CONDITIONAL([HAVE_SYSTEMD], [test "x$with_systemduserconfdir" != "xno"])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_MALLOC

AM_CONDITIONAL([UDEV_AVAIL], [test "`pkg-config --variable=udevdir udev`" != ""])

AC_CONFIG_FILES([Makefile src/Makefile udev/Makefile res/Makefile test/Makefile po/Makefile.in po/Makefile systemd/Makefile])
AC_OUTPUT
