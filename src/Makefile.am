PKG_CONFIG ?= pkg-config

AM_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags jack libusb-1.0` -pthread $(SAMPLERATE_CFLAGS)
overwitch_LDFLAGS = `$(PKG_CONFIG) --libs jack libusb-1.0` $(SAMPLERATE_LIBS)

bin_PROGRAMS = overwitch
overwitch_SOURCES = main.c jclient.c jclient.h overwitch.c overwitch.h utils.c utils.h dll.c dll.h

AM_CPPFLAGS = -Wall -O3

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@
