# Overwitch

Overwitch is an Overbridge 2 device client for JACK (JACK Audio Connection Kit).

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

At the moment, it provides support for all Overbridge 2 devices, which are Analog Four MKII, Analog Rytm MKII, Digitakt, Digitone, Digitone Keys, Analog Heat and Analog Heat MKII.

Overwitch consists of 3 different binaries: `overwitch`, which is a GUI application, `overwitch-cli` which offers the same functionality for the command line; and `overwitch-record` which  does not integrate with JACK at all but streams all the tracks to a WAVE file.

## Installation

As with other autotools project, you need to run the following commands. If you just want to compile the command line applications, pass `CLI_ONLY=yes` to `/configure`.

```
autoreconf --install
./configure
make
sudo make install
```

Some udev rules might need to be installed manually with `sudo make install` from the `udev` directory as they are not part of the `install` target. This is not needed when packaging or when distributions already provide them.

The package dependencies for Debian based distributions are:
- automake
- libtool
- libusb-1.0-0-dev
- libjack-jackd2-dev
- libsamplerate0-dev
- libjson-glib-dev
- libgtk-3-dev
- libsndfile1-dev

You can easily install them by running `sudo apt install automake libtool libusb-1.0-0-dev libjack-jackd2-dev libsamplerate0-dev libjson-glib-dev libgtk-3-dev libsndfile1-dev`.

As this will install `jackd2`, you would be asked to configure it to be run with real time priority. Be sure to answer yes. With this, the `audio` group would be able to run processes with real time priority. Be sure to be in the `audio` group too.

## Usage

Overwitch contains two JACK clients, one for the desktop and one for the command line, and a simple recording utility for the command line.

Regarding the JACK clients, latency needs to be under control and it can be tuned with the following parameters.

- Blocks, which controls the amount of data sent in a single USB operation. The higher, the higher latency but the lower CPU usage. 4 blocks keeps the latency quite low and does not impact on the CPU.
- Quality, which controls the resampler accuracy. The higher, the more CPU consuming. A medium value is recommended. Notice that in `overwitch-cli`, a value of 0 means the highest quality while a value of 4 means the lowest.

### overwitch

The GUI is self explanatory and does not requiere any parameter passed from the command line.

```
$ overwitch -h
overwitch 1.0
Usage: overwitch [options]
Options:
  --verbose, -v
  --help, -h
```

Notice that once an Overbridge device is running, neither the blocks nor the resampling quality can be changed so you will need to stop the running instances and refresh the list.

It is possible to rename Overbridge devices by simply editing its name on the list. Still, as JACK devices can not be renamed while running, the device will be restarted.

### overwitch-cli

First, list the available devices. The first element is an internal ID that allows to identify the devices.

```
$ overwitch-cli -l
0: Digitakt (ID 1935:000c) at bus 001, address 005
```

Then, you can choose which device you want to use by using one of these options. Notice that the second option will only work for the first device found with that name.

```
$ overwitch -n 0
$ overwitch -d Digitakt
```

To stop, just press `Ctrl+C`. You'll see an oputput like the one below. Notice that we are using the verbose option here but it is **not recommended** to use it and it is showed here for illustrative purposes only.

```
$ overwitch-cli -d Digitakt -v -b 4
DEBUG:overwitch.c:206:(ow_get_devices): Found Digitakt (bus 001, address 005, ID 1935:000c)
DEBUG:jclient.c:112:(jclient_set_sample_rate_cb): JACK sample rate: 48000
DEBUG:jclient.c:471:(jclient_run): Using RT priority 5...
DEBUG:engine.c:1001:(ow_engine_activate): Starting p2o MIDI thread...
DEBUG:engine.c:1015:(ow_engine_activate): Starting audio and o2p MIDI thread...
DEBUG:jclient.c:112:(jclient_set_sample_rate_cb): JACK sample rate: 48000
DEBUG:jclient.c:102:(jclient_set_buffer_size_cb): JACK buffer size: 64
DEBUG:jclient.c:102:(jclient_set_buffer_size_cb): JACK buffer size: 64
Digitakt@001,005: o2j latency: -1.0 ms, max. -1.0 ms; j2o latency: -1.0 ms, max. -1.0 ms, o2j ratio: 0.999888, avg. 0.999904
Digitakt@001,005: o2j latency: -1.0 ms, max. -1.0 ms; j2o latency: -1.0 ms, max. -1.0 ms, o2j ratio: 0.999901, avg. 0.999903
Digitakt@001,005: o2j latency:  1.4 ms, max.  1.8 ms; j2o latency: -1.0 ms, max. -1.0 ms, o2j ratio: 0.999903, avg. 0.999905
^C
DEBUG:jclient.c:579:(jclient_run): Exiting...
```

To limit latency to the lowest possible value, audio is not sent through during the first seconds.

You can list all the available options with `-h`.

```
$ overwitch-cli -h
overwitch 1.0
Usage: overwitch-cli [options]
Options:
  --use-device-number, -n value
  --use-device, -d value
  --resampling-quality, -q value
  --transfer-blocks, -b value
  --rt-priority, -p value
  --list-devices, -l
  --verbose, -v
  --help, -h
```

### overwitch-record

This small utility let the user record the audio output from the Overbridge devices into a WAVE file with the following command. To stop, just press `Ctrl+C`.

```
$ overwitch-record -d Digitakt
^C
2106720 frames written
Digitakt_dump_2022-04-20T19:20:19.wav file created
```

By default, it records all the output tracks from the Overbridge device but it is possible to select which ones to record. First, list the devices in verbose mode to see all the available tracks.

```
$ overwitch-record -l -v
DEBUG:overwitch.c:206:(ow_get_devices): Found Digitakt (bus 001, address 005, ID 1935:000c)
0: Digitakt (ID 1935:000c) at bus 001, address 005
  Inputs:
    Main L Input
    Main R Input
  Outputs:
    Main L
    Main R
    Track 1
    Track 2
    Track 3
    Track 4
    Track 5
    Track 6
    Track 7
    Track 8
    Input L
    Input R
```

Then, just select the output tracks to record as this. `0` means that the track is not recorded while any other character means it will. In this example, we are recording tracks 1, 2, 5 and 6.

```
$ overwitch-record -d Digitakt -m 001100110000
^C
829920 frames written
Digitakt_dump_2022-04-20T19:33:30.wav file created
```

It is not neccessary to provide all tracks, meaning that using `00110011` as the mask will behave exactly as the example above.

You can list all the available options with `-h`.

```
$ overwitch-record -h
overwitch 1.0
Usage: overwitch-record [options]
Options:
  --use-device-number, -n value
  --use-device, -d value
  --list-devices, -l
  --track-mask, -m value
  --track-buffer-kilobytes, -b value
  --verbose, -v
  --help, -h
```

## Latency

Device to JACK latency is different from JACK to device latency though they are very close. These latencies are the transferred frames to and from the device and, by default, these are performed in 24 blocks of 7 frames (168 frames).

Thus, the minimum theoretical latency is the device frames plus the JACK buffer frames plus some additional buffer frames are used in the resamplers but it is unknown how many.

To keep latency as low as possible, the amount of blocks can be configured in the JACK clients. Values between 2 and 32 can be used.

## Tunning

Although this is a matter of JACK, Ardour and OS tuning, Here you have some tips.

First and foremost, real time applications work much better without SMT/HyperThreading activated. This script might be handy. It also changes the CPU governor to performance.

```
#!/bin/bash

function disable() {
  for c in $(seq 0 $(cat /sys/devices/system/cpu/present | awk -F- '{print $2}')); do
    cpu_sibling_file=/sys/devices/system/cpu/cpu$c/topology/thread_siblings_list
    if [ -f $cpu_sibling_file ]; then
      id=$(cat $cpu_sibling_file | awk -F, '{print $1}')
      if [ "$c" != "$id" ]; then
        echo 0 | sudo tee /sys/devices/system/cpu/cpu$c/online > /dev/null
      else
        sudo cpufreq-set -c $c -g performance
      fi
    fi
  done
}

function enable() {
  for c in $(seq 1 $(cat /sys/devices/system/cpu/present | awk -F- '{print $2}')); do
    echo 1  | sudo tee /sys/devices/system/cpu/cpu$c/online > /dev/null
    sudo cpufreq-set -c $c -g ondemand
  done
}

function status() {
  grep -E '^model name|^cpu MHz' /proc/cpuinfo
}

if [ "$1" == "enable" ] || [ "$1" == "disable" ] || [ "$1" == "status" ]; then
  $1
  [ "$1" != "status" ] && status
  exit 0
else
  echo "Unknown smt setup" >&2
  exit -1
fi
```

With `rtirq-init` package installed I simply let the script reconfigure the priorities of IRQ handling threads and then I run JACK, Overwitch and the desired applications. Notice that real time priorities are set by default so there is **no need** to set explicitely these.

```
$ sudo /etc/init.d/rtirq start
$ jackd ...
$ overwitch -b 8 -q 2 -d Digitakt
$ ardour
```

Currently I'm using this RT kernel. You don't need an RT kernel but it will help even more to reduce latency and xruns.

```
$ uname -v
#1 SMP PREEMPT_RT Debian 5.10.28-1 (2021-04-09)
```

With all this configuration I get no JACK xruns with 64 frames buffer (2 periods) and occasional xruns with 32 frames buffer (3 periods) with network enabled and under normal usage conditions.

Although you can run Overwitch with verbose output this is **not recommended** unless you are debugging the application.

## Adding devices

New Overbridge 2 devices could be easily added in the `overbridge.c` file as they all share the same protocol.

To define a new device, just add a new struct like this and add the new constant to the array. USB PIDs are already defined just above. For instance, if you were adding the Analog Rytm MKII, you could do it like in the example below.

Notice that the definition of the device must match the device itself, so outputs and inputs must match the ones the device has and must be in the same order. As this struct defines the device, an input is a port the device will read data from and an output is a port the device will write data to.

```
static const struct overbridge_device_desc ARMK2_DESC = {
  .pid = ARMK2_PID,
  .name = "Analog Rytm MKII",
  .inputs = 12,
  .outputs = 12,
  .input_track_names =
    {"Main L Input", "Main R Input", "Main FX L Input", "Main FX R Input",
     "BD Input", "SD Input", "RS/CP Input", "BT Input",
     "LT Input", "MT/HT Input", "CH/OH Input", "CY/CB Input"},
  .output_track_names = {"Main L", "Main R", "BD", "SD", "RS/CP",
			 "BT", "LT", "MT/HT", "CH/OH", "CY/CB", "Input L",
			 "Input R"}
};

static const struct overbridge_device_desc OB_DEVICE_DESCS[] = {
  DIGITAKT_DESC, DIGITONE_DESC, ARMK2_DESC, NULL
};
```

Overbridge 1 devices, which are Analog Four MKI, Analog Keys and Analog Rytm MKI, are not supported yet.
