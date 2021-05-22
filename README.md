# Overwitch

Overwitch is an Overbridge device client for JACK (JACK Audio Connection Kit).

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

At the moment, it provides 12 input ports and 2 output ports for Elektron Digitakt and Digitone but no MIDI support. Overwitch works only with the first found device at the moment, so only a single Overwitch instance can be run for now.

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

After, you need to allow user permission to use the devices so create a file in `/etc/udev/rules.d/elektron.rules` with the following content.

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="1935", MODE="0666"
SUBSYSTEM=="usb_device", ATTRS{idVendor}=="1935", MODE="0666"
```

And do not forget to activate these rules.

```
$ sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Usage

Simply, run `overwitch`. Press `Ctrl+C` to stop. You'll see an oputput like this.

```
$ overwitch
DEBUG:overbridge.c:337:(overbridge_init_priv): Device: Digitakt
DEBUG:overwitch.c:102:(overwitch_sample_rate_cb): JACK sample rate: 48000
DEBUG:overbridge.c:449:(overbridge_run): Starting device...
DEBUG:overwitch.c:130:(overwitch_buffer_size_cb): JACK buffer size: 32
DEBUG:overbridge.c:431:(run): Running device...
^C
DEBUG:overwitch.c:452:(overwitch_exit): Max. latencies (ms): 4.8, 4.7
DEBUG:overwitch.c:590:(overwitch_run): Exiting...
```

To limit latency to the lowest possible value, audio is not sent through during the first seconds.

## Latency

Device to JACK latency is different from JACK to device latency though they are very close.

The minimum theoretical latency is the device frames (168 frames, 3.5 ms) plus the JACK buffer frames. Thus, the minimum theoretical latency is 4.8 ms for 64 frames buffer. Notice that some additional buffer frames are used in the resamplers but it is unknown how many. In practice, both latencies are below 6 ms in this scenario and below 5 ms if using 32 frames.

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

While using a RT kernel, with `rtirq-init` package installed I simply let the script reconfigure the priorities of IRQ handling threads. After, starting Ardour, I change the RT priorities of the audio related processes.

```
$ sudo /etc/init.d/rtirq start
$ sudo chrt -f -p 40 -a $(pidof jackd)
$ sudo chrt -f -p 35 -a $(pidof overwitch)
$ sudo chrt -f -p 30 -a $(pidof ardour-5.12.0)
```

Currently I'm using this RT kernel.

```
$ uname -v
#1 SMP PREEMPT_RT Debian 5.10.28-1 (2021-04-09)
```

With this configuration I get no JACK xruns with 64 frames buffer (2 periods) and occasional xruns with 32 frames buffer (3 periods) with network enabled and under normal usage conditions.

Although you can run Overwitch with verbose output this is **not recommended** unless you are debugging the application.

## Adding devices

Hopefully, Overbridge 2 devices could be easily added in the `overbridge.c` file. However, since Overwitch works only with the first found device at the moment, if you have several devices, connect only the one that you want to use for now.

To define a new device, just add a new struct like this and add the new constant to the array. USB PIDs are already defined there. Try to use the same names as in the device and capitalize the first letter or use acronyms.

For instance, if you are adding the Analog Rytm MKII, you could do it like this. Naming style might subject to change.

Notice that the definition of the device must match the device itself, so outputs and inputs amount must match the ones the device has and must be in the same order. As this struct defines the device, an input is a port the device will read data from and an output is a port the device will write data to.

```
static const struct overbridge_device_desc ARMK2_DESC = {
  .pid = ARMK2_PID,
  .name = "Analog Rytm MKII",
  .inputs = 12,
  .outputs = 12,
  .input_track_names =
    {"Main L", "Main R", "Main FX L", "Main FX R", "BD", "SD", "RS/CP", "BT",
     "LT", "MT/HT", "CH/OH", "CY/CB"},
  .output_track_names = {"Main L", "Main R", "BD", "SD", "RS/CP",
			 "BT", "LT", "MT/HT", "CH/OH", "CY/CB", "Input L",
			 "Input R"}
};

static const struct overbridge_device_desc OB_DEVICE_DESCS[] = {
  DIGITAKT_DESC, DIGITONE_DESC, ARMK2_DESC
};
```

It is unknown if Overbridge 1 devices will work.
