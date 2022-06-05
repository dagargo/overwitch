PKG_CONFIG ?= pkg-config

CLI_LIBS = jack libusb-1.0 json-glib-1.0
GUI_LIBS = gtk+-3.0

overwitch_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS) $(GUI_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
overwitch_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS) $(GUI_LIBS)` $(SAMPLERATE_LIBS)

overwitch_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
overwitch_cli_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SAMPLERATE_LIBS)

overwitch_play_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags libusb-1.0` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
overwitch_play_LDFLAGS = `$(PKG_CONFIG) --libs libusb-1.0` $(SAMPLERATE_LIBS) $(SNDFILE_LIBS)

overwitch_record_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags libusb-1.0` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
overwitch_record_LDFLAGS = `$(PKG_CONFIG) --libs libusb-1.0` $(SAMPLERATE_LIBS) $(SNDFILE_LIBS)

if CLI_ONLY
bin_PROGRAMS = overwitch-cli overwitch-record overwitch-play
else
bin_PROGRAMS = overwitch overwitch-cli overwitch-record overwitch-play
endif

overwitch_SOURCES = main.c jclient.c jclient.h engine.c engine.h utils.c utils.h dll.c dll.h resampler.c resampler.h overwitch.c overwitch.h common.c common.h
overwitch_cli_SOURCES = main-cli.c jclient.c jclient.h engine.c engine.h utils.c utils.h dll.c dll.h resampler.c resampler.h overwitch.c overwitch.h common.c common.h
overwitch_play_SOURCES = main-play.c engine.c engine.h dll.c dll.h utils.c utils.h overwitch.c overwitch.h common.c common.h
overwitch_record_SOURCES = main-record.c engine.c engine.h dll.c dll.h utils.c utils.h overwitch.c overwitch.h common.c common.h

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)"'
