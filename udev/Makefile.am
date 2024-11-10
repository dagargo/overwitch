EXTRA_DIST = 20-usb-elektron.rules 20-usb-elektron.hwdb

if UDEV_AVAIL

udevhome = $$(env pkg-config --variable=udevdir udev)

udevrulesdir = $(udevhome)/rules.d
udevhwdbdir = $(udevhome)/hwdb.d

udevrules_DATA = 20-usb-elektron.rules
udevhwdb_DATA = 20-usb-elektron.hwdb

install-data-hook: update-udev

uninstall-hook: update-udev

update-udev:
	@udevadm control --reload
	@udevadm trigger
	@systemd-hwdb update

uninstall-local:
	rm -f $(DESTDIR)$(udevrulesdir)/20-usb-elektron.rules
	rm -f $(DESTDIR)$(udevhwdbdir)/20-usb-elektron.hwdb

endif
