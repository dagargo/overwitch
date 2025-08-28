---
layout: base
title: Installation
permalink: /installation/
order: 1
---

## Installation

As with other autotools project, you need to run the commands below. There is a compilation option available.

* If you just want to compile the command line applications, pass `CLI_ONLY=yes` to `./configure`.

```
autoreconf --install
./configure
make
sudo make install
sudo ldconfig
```

Some udev rules might need to be installed manually with `sudo make install` from the `udev` directory as they are not part of the `install` target. This is not needed when packaging or when distributions already provide them.

The package dependencies for Debian based distributions are:
- automake
- libtool
- libusb-1.0-0-dev
- libjack-jackd2-dev
- libsamplerate0-dev
- libsndfile1-dev
- autopoint
- gettext
- libsystemd-dev
- libjson-glib-dev
- libgtk-4-dev (only if `CLI_ONLY=yes` is not used)
- systemd-dev (only used to install the udev rules)

You can easily install all them by running this.

```
sudo apt install automake libtool libusb-1.0-0-dev libjack-jackd2-dev libsamplerate0-dev libsndfile1-dev autopoint gettext libsystemd-dev libjson-glib-dev libgtk-4-dev systemd-dev
```

For Fedora, run this to install the build dependencies.

```
sudo yum install automake libtool libusb1-devel jack-audio-connection-kit-devel libsamplerate-devel libsndfile-devel gettext-devel json-glib-devel gtk4-devel systemd-devel
```

For Arch, no additional dependencies are needed.

As this will install `jackd2`, you would be asked to configure it to be run with real time priority. Be sure to answer yes. With this, the `audio` group would be able to run processes with real time priority. Be sure to be in the `audio` group too.

### systemd service

For embedded systems or users not wanting to use the GUI, it is recommended to install the systemd service unit by running `sudo make install` from the `systemd` directory. Notice `overwitch.service` is installed as a user service.

As with any other systemd services, it needs to be started with `systemctl start overwitch --user`. Commands `stop` and `restart` are also available.

To allow the service to be started at boot, running `systemctl --user enable overwitch.service` is needed.
