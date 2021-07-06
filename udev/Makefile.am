udevrulesdir = /etc/udev/rules.d
udevhwdbdir = /etc/udev/hwdb.d

if UDEV
udevrules_DATA = 20-usb-elektron.rules
udevhwdb_DATA = 20-usb-elektron.hwdb
else
udevrules_DATA =
udevhwdb_DATA =
endif

install-data-hook: update-udev

uninstall-hook: update-udev

update-udev:
	@udevadm control --reload
	@udevadm trigger
	@systemd-hwdb update
