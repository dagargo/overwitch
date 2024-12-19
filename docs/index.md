---
layout: home
---

Overwitch is an Overbridge 2 device client for JACK (JACK Audio Connection Kit).

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

At the moment, it provides support for all Overbridge 2 devices, which are Analog Four MKII, Analog Rytm MKII, Digitakt, Digitone, Digitone Keys, Analog Heat, Analog Heat MKII and Syntakt.

Overbridge 1 devices, which are Analog Four MKI, Analog Keys and Analog Rytm MKI, are not supported yet.

Overwitch consists of 4 different binaries: `overwitch`, which is a GUI application, `overwitch-cli` which offers the same functionality for the command line; and `overwitch-play` and `overwitch-record` which do not integrate with JACK at all but stream the audio from and to a WAVE file.

For a device manager application for Elektron devices, check [Elektroid](https://dagargo.github.io/elektroid/).
