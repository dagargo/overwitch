PKG_CONFIG ?= pkg-config

overwitch_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags jack libusb-1.0` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
overwitch_LDFLAGS = `$(PKG_CONFIG) --libs jack libusb-1.0` $(SAMPLERATE_LIBS) $(SNDFILE_LIBS)

overwitch_dump_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags libusb-1.0` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
overwitch_dump_LDFLAGS = `$(PKG_CONFIG) --libs libusb-1.0` $(SAMPLERATE_LIBS) $(SNDFILE_LIBS)

bin_PROGRAMS = overwitch overwitch-dump
overwitch_SOURCES = main.c jclient.c jclient.h engine.c engine.h utils.c utils.h dll.c dll.h resampler.c resampler.h overwitch.c overwitch.h common.c common.h
overwitch_dump_SOURCES = main-dump.c engine.c engine.h dll.c dll.h utils.c utils.h overwitch.c overwitch.h common.c common.h

AM_CPPFLAGS = -Wall -O3

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@
