PKG_CONFIG ?= pkg-config

AM_CFLAGS = -Wall

check_PROGRAMS = test_usb_blocks
TESTS = $(check_PROGRAMS)

CLI_LIBS = jack libusb-1.0 cunit

test_usb_blocks_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
test_usb_blocks_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SAMPLERATE_LIBS)

test_usb_blocks_SOURCES = test_usb_blocks.c ../src/engine.c ../src/engine.h ../src/utils.c ../src/utils.h ../src/overwitch.c ../src/overwitch.h ../src/dll.c ../src/dll.h

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@
