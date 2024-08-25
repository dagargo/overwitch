resdir = $(datadir)/overwitch
res_DATA = overwitch.ui

if JSON_DEVS_FILE
res_DATA += devices.json
endif

desktopdir = $(datadir)/applications
desktop_DATA = overwitch.desktop

svgicondir = $(datarootdir)/icons/hicolor/scalable/apps
svgicon_DATA = overwitch.svg overwitch-symbolic.svg

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datarootdir)/icons/hicolor

install-data-hook: update-icon-cache

uninstall-hook: update-icon-cache

update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

EXTRA_DIST = \
        $(res_DATA) \
        $(desktop_DATA) \
        $(svgicon_DATA)
