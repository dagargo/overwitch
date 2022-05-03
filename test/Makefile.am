PKG_CONFIG ?= pkg-config

AM_CFLAGS = -Wall

check_PROGRAMS = tests
TESTS = $(check_PROGRAMS)

CLI_LIBS = jack libusb-1.0 cunit

tests_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
tests_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SAMPLERATE_LIBS)

tests_SOURCES = tests.c ../src/engine.c ../src/engine.h ../src/utils.c ../src/utils.h ../src/overwitch.c ../src/overwitch.h ../src/dll.c ../src/dll.h ../src/jclient.c ../src/jclient.h ../src/resampler.c ../src/resampler.h

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@
