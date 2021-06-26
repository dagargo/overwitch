# Overwitch

Overwitch is an Overbridge device client for JACK (JACK Audio Connection Kit).

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

At the moment, it provides 12 input ports and 2 output ports for Elektron Digitakt and Digitone but no MIDI support. In order to work with several devices, different instances of Overwitch must be run.

## Installation

As with other autotools project, you need to run the following commands.

```
autoreconf --install
./configure
make
sudo make install
```

The package dependencies for Debian based distributions are:
- automake
- libusb-1.0-0-dev
- libjack-jackd2-dev
- libsamplerate0-dev

You can easily install them by running `sudo apt install automake libusb-1.0-0-dev libjack-jackd2-dev libsamplerate0-dev`.

As this will install `jackd2`, you would be asked to configure it to be run with real time priority. Be sure to answer yes. With this, the `audio` group would be able to run processes with real time priority. Be sure to be in the `audio` group too.

After, you need to allow users to use the USB devices. For doing so, create the file `/etc/udev/rules.d/elektron.rules` with the following content. This will allows us to use any device from Elektron.

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="1935", MODE="0666"
SUBSYSTEM=="usb_device", ATTRS{idVendor}=="1935", MODE="0666"
```

And do not forget to activate these rules.

```
$ sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Usage

First, list the available devices.

```
$ overwitch -l
Bus 001 Port 003 Device 006: ID 1935:000c Digitakt
```

Then, you can indicate which device you want to use like this. It is recommended to run `overwitch` with real time priority, so run it with `chrt`. To stop, just press `Ctrl+C`. You'll see an oputput like the one below. Notice that we are using the verbose option here but it is **not recommended** to use it and it is showed here for illustrative pursoses only.

```
$ chrt -f 35 overwitch -d Digitakt -v
Device: Digitakt (outputs: 12, inputs: 2)
JACK sample rate: 48000
JACK buffer size: 64
DEBUG:overbridge.c:480:(overbridge_run): Starting device thread...
DEBUG:overwitch.c:379:(overwitch_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999791, 1.000209; curr. ratios: 0.999762, 1.000238
DEBUG:overwitch.c:379:(overwitch_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999847, 1.000153; curr. ratios: 0.999918, 1.000082
DEBUG:overwitch.c:379:(overwitch_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999899, 1.000101; curr. ratios: 0.999931, 1.000069
DEBUG:overwitch.c:379:(overwitch_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999912, 1.000088; curr. ratios: 0.999888, 1.000112
DEBUG:overwitch.c:379:(overwitch_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999911, 1.000089; curr. ratios: 0.999876, 1.000124
DEBUG:overwitch.c:379:(overwitch_compute_ratios): Max. latencies (ms): 5.3, 5.1; avg. ratios: 0.999883, 1.000117; curr. ratios: 0.999892, 1.000108
^C
Max. latencies (ms): 5.6, 5.6
DEBUG:overwitch.c:607:(overwitch_run): Exiting...
```

To limit latency to the lowest possible value, audio is not sent through during the first seconds.

You can list all the available options with `-h`.

```
$ overwitch -h
overwitch 0.2
Usage: overwitch [options]
Options:
  --use-device, -d value
  --resampling-quality, -q value
  --transfer-blocks, -b value
  --list-devices, -l
  --verbose, -v
  --help, -h
```

## Latency

Device to JACK latency is different from JACK to device latency though they are very close. These latencies are the transferred frames to and from the device and, by default, these are performed in 24 groups (blocks) of 7 frames (168 frames).

Thus, the minimum theoretical latency is the device frames plus the JACK buffer frames plus some additional buffer frames are used in the resamplers but it is unknown how many.

But looks like this block amount can be changed. With the option `-b` we can override this value indicating how many blocks are processed at a time. The default value is 24 but values between 2 and 32 can be used. Notice that this option is **highly experimental**.

## Tuning

Although this is a matter of JACK, Ardour and OS tuning, I'm leaving here some tips I use.

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

With `rtirq-init` package installed I simply let the script reconfigure the priorities of IRQ handling threads. After, I run the audio related processes with real time priorities. This helps a lot to reduce latency and xruns.

```
$ sudo /etc/init.d/rtirq start
$ chrt -f -p 40 -a $(pidof jackd)
$ chrt -f -p 35 -a $(pidof overwitch)
$ chrt -f -p 30 -a $(pidof ardour-5.12.0)
```

Currently I'm using this RT kernel. You don't need an RT kernel but it will help even more to reduce latency and xruns.

```
$ uname -v
#1 SMP PREEMPT_RT Debian 5.10.28-1 (2021-04-09)
```

With all this configuration I get no JACK xruns with 64 frames buffer (2 periods) and occasional xruns with 32 frames buffer (3 periods) with network enabled and under normal usage conditions.

Although you can run Overwitch with verbose output this is **not recommended** unless you are debugging the application.

## Adding devices

Hopefully, Overbridge 2 devices could be easily added in the `overbridge.c` file.

To define a new device, just add a new struct like this and add the new constant to the array. USB PIDs are already defined there. For instance, if you are adding the Analog Rytm MKII, you could do it like in the example below.

Notice that the definition of the device must match the device itself, so outputs and inputs amount must match the ones the device has and must be in the same order. As this struct defines the device, an input is a port the device will read data from and an output is a port the device will write data to.

Naming style might be subject to change.

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
  DIGITAKT_DESC, DIGITONE_DESC, ARMK2_DESC
};
```

It is unknown if Overbridge 1 devices will work.
