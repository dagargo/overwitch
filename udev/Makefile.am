EXTRA_DIST = 91-usb-elektron.rules 91-usb-elektron.hwdb

if UDEV_AVAIL

udevhome = $$(env pkg-config --variable=udevdir udev)

udevrulesdir = $(udevhome)/rules.d
udevhwdbdir = $(udevhome)/hwdb.d

udevrules_DATA = 91-usb-elektron.rules
udevhwdb_DATA = 91-usb-elektron.hwdb

install-data-hook: update-udev

uninstall-hook: update-udev

update-udev:
	@udevadm control --reload
	@udevadm trigger
	@systemd-hwdb update

uninstall-local:
	rm -f $(DESTDIR)$(udevrulesdir)/91-usb-elektron.rules
	rm -f $(DESTDIR)$(udevhwdbdir)/91-usb-elektron.hwdb

endif
