PKG_CONFIG ?= pkg-config

LIB_LIBS = libusb-1.0

if JSON_DEVS_FILE
LIB_LIBS += json-glib-1.0
endif

CLI_LIBS = jack $(LIB_LIBS)
SRV_LIBS = glib-2.0 $(CLI_LIBS)
GUI_LIBS = gtk4 $(CLI_LIBS)

if ! JSON_DEVS_FILE
GUI_LIBS += json-glib-1.0
endif

overwitch_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(GUI_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
overwitch_LDFLAGS = `$(PKG_CONFIG) --libs $(GUI_LIBS)` $(SAMPLERATE_LIBS)

overwitch_service_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(SRV_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
overwitch_service_LDFLAGS = `$(PKG_CONFIG) --libs $(SRV_LIBS)` $(SAMPLERATE_LIBS)

overwitch_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` -pthread $(SAMPLERATE_CFLAGS)
overwitch_cli_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SAMPLERATE_LIBS)

overwitch_play_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(LIB_LIBS)` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
overwitch_play_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SAMPLERATE_LIBS) $(SNDFILE_LIBS)

overwitch_record_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(LIB_LIBS)` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
overwitch_record_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SAMPLERATE_LIBS) $(SNDFILE_LIBS)

CLI_UTILS = overwitch-cli overwitch-service overwitch-record overwitch-play

if CLI_ONLY
bin_PROGRAMS = $(CLI_UTILS)
else
bin_PROGRAMS = overwitch $(CLI_UTILS)
endif

lib_LTLIBRARIES = liboverwitch.la
liboverwitch_la_SOURCES = engine.c engine.h dll.c dll.h utils.c utils.h overwitch.c overwitch.h resampler.c resampler.h
liboverwitch_la_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(LIB_LIBS)` -pthread $(SAMPLERATE_CFLAGS) $(SNDFILE_CFLAGS)
liboverwitch_la_LDFLAGS = `$(PKG_CONFIG) --libs $(LIB_LIBS)` $(SAMPLERATE_LIBS)
include_HEADERS = overwitch.h

overwitch_SOURCES = main.c overwitch_device.c overwitch_device.h jclient.c jclient.h config.c config.h
overwitch_service_SOURCES = main-service.c jclient.c jclient.h config.c config.h
overwitch_cli_SOURCES = main-cli.c jclient.c jclient.h common.c common.h
overwitch_play_SOURCES = main-play.c common.c common.h
overwitch_record_SOURCES = main-record.c common.c common.h

overwitch_LDADD = liboverwitch.la
overwitch_service_LDADD = liboverwitch.la
overwitch_cli_LDADD = liboverwitch.la
overwitch_play_LDADD = liboverwitch.la
overwitch_record_LDADD = liboverwitch.la

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)/$(PACKAGE)"' -DLOCALEDIR='"$(localedir)"'
