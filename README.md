# Overwitch

Overwitch is an Overbridge device client for JACK (JACK Audio Connection Kit).

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

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

After installing Overwitch, we could let it adjust its own priority to realtime with this command.

```
$ sudo setcap 'cap_sys_nice=eip' $(which overwitch)
```

## Usage

Simply, run `overwitch`. Press `Ctrl+C` to stop. You'll see an oputput like this.

```
$ overwitch
DEBUG:overbridge.c:348:(overbridge_init_priv): Device: Digitakt
DEBUG:overwitch.c:100:(overwitch_sample_rate_cb): JACK sample rate: 48000
DEBUG:overbridge.c:463:(overbridge_run): Starting device...
DEBUG:overwitch.c:122:(overwitch_buffer_size_cb): JACK buffer size: 64
DEBUG:overbridge.c:433:(run): Calibrating device...
DEBUG:overwitch.c:397:(overwitch_cal_cb): Calibration value: 0.999885
DEBUG:overbridge.c:438:(run): Calibration finished
DEBUG:overbridge.c:445:(run): Running device...
^C
DEBUG:overwitch.c:423:(overwitch_exit): Maximum measured buffer latencies: 5.3 ms, 5.4 ms
DEBUG:overwitch.c:590:(overwitch_run): Exiting...
```

Overwitch needs to calibrate itself, which takes up to one minute. So you'll hear nothing until you see the running device message.
The reason we need to perform some calibration is to keep latency under control at startup.

You can skip this calibration if you use the calibration value above like this.

```
$ overwitch -r 0.999885
DEBUG:overbridge.c:348:(overbridge_init_priv): Device: Digitakt
DEBUG:overwitch.c:100:(overwitch_sample_rate_cb): JACK sample rate: 48000
DEBUG:overbridge.c:463:(overbridge_run): Starting device...
DEBUG:overwitch.c:122:(overwitch_buffer_size_cb): JACK buffer size: 64
DEBUG:overbridge.c:445:(run): Running device...
[...]
```

The calibration value depends on your setup, your audio interface, your sampling rate and your Overbridge device so use only the value you got from the calibration mode. Remember to run Overwitch in calibration mode every time you change anything on your setup.

## Latency

Device to JACK latency is different from JACK to device one. Here, we are referring to device to JACK latency, which is the multitrack recording latency.

The minimum theoretical latency is the device frames (168 frames, 3.5 ms) plus the JACK buffer frames. Thus, the minimum theoretical latency is 4.8 ms for 64 frames buffer. Notice that some additional buffer frames are used in the resamplers but it is unknown how many.

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

After starting Ardour, I change its priority and scheduling to 99 and `SCHED_FIFO` and apply the same to `jackd`. All threads in Overwitch are configured in a similar fashion by default, so you do not need to do anything else.

```
$ sudo chrt -r -p 99 `pidof ardour-5.12.0`
$ sudo chrt -r -p 99 `pidof jackd`
```

## Adding devices

Hopefully, new devices could be easily added in the `overbridge.c` file. However, since Overwitch works only with the first found device at the moment, if you have several devices, connect only the one that you want to use for now.

To define a new device, just add a new struct like this and add the new constant to the array. USB PIDs are already defined there. Try to use the same names as in the device and capitalize the first letter.

```
static const struct overbridge_device_desc ARMK2_DESC = {
  .pid = ARMK2_PID,
  .name = "Analog Rytm MKII",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Output L", "Output R"},
  .output_track_names =
    {"Master L", "Master R", "Track 1", "Track 2", "Track 3", "Track 4",
     "Track 5", "Track 6", "Track 7", "Track 8", "Input L", "Input R"}
};

static const struct overbridge_device_desc OB_DEVICE_DESCS[] = {
  DIGITAKT_DESC, DIGITONE_DESC, ARMK2_DESC
};
```
