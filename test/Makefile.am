PKG_CONFIG ?= pkg-config

AM_CFLAGS = -Wall -O3

check_PROGRAMS = tests
TESTS = $(check_PROGRAMS)

TEST_LIBS = jack libusb-1.0 glib-2.0 json-glib-1.0 cunit

tests_CFLAGS = -DDATADIR='"$(datadir)/$(PACKAGE)"' -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(TEST_LIBS)` -pthread $(SAMPLERATE_CFLAGS) $(AM_CFLAGS)
tests_LDFLAGS = `$(PKG_CONFIG) --libs $(TEST_LIBS)` $(SAMPLERATE_LIBS)

tests_SOURCES = tests.c ../src/engine.c ../src/engine.h \
	../src/utils.c ../src/utils.h \
	../src/overwitch.c ../src/overwitch.h \
	../src/dll.c ../src/dll.h \
	../src/jclient.c ../src/jclient.h \
	../src/resampler.c ../src/resampler.h \
	../src/common.c ../src/common.h \
	../src/message.c ../src/message.h \
	../src/overwitch_device.c ../src/overwitch_device.h

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@
