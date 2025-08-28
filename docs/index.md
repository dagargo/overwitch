---
layout: home
---

Overwitch is a set of JACK (JACK Audio Connection Kit) clients for Overbridge 2 devices. Since PipeWire is ABI compatible with JACK, Overwitch works with PipeWire too.

![Overwitch GUI screenshot](images/screenshot.png "Overwitch GUI")

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

At the moment, it provides support for all Overbridge 2 devices, which are Analog Four MKII, Analog Rytm MKII, Digitakt, Digitakt II, Digitone, Digitone II, Digitone Keys, Analog Heat, Analog Heat MKII, Analog Heat +FX and Syntakt.

Overbridge 1 devices, which are Analog Four MKI, Analog Keys and Analog Rytm MKI, are not supported yet.

Overwitch consists of 5 different binaries divided in 2 categories: multi-device applications (they can **not** be used simultaneously) and single-device utilities.

Multi-device applications:

* `overwitch`, which is a GUI application that uses the below D-Bus service under the hood.
* `overwitch-service`, which is a D-Bus and CLI application that will create a JACK client for every device detected or plugged in later.

Single-device utilities:

* `overwitch-cli`, which is a single-device JACK client program.
* `overwitch-play`, which plays multitrack audio thru Overbridge devices.
* `overwitch-record`, which records multitrack audio from Overbridge devices.

For a device manager application for Elektron devices, check [Elektroid](https://dagargo.github.io/elektroid/).
