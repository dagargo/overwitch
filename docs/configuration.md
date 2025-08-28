---
layout: base
title: Configuration
permalink: /configuration/
order: 2
---

## Configuration

### PipeWire

Depending on your PipeWire configuration, you might want to pass some additional information to the different binaries by setting the `PIPEWIRE_PROPS` environment variable.

This value can be set in the GUI settings directly but any value passed at command launch will always take precedence over that configuration.

The proper way of setting this when using `overwitch-service` is in `~/.config/overwitch/preferences.json`, which is the same file the GUI uses.

Under PipeWire, a JACK client always follows a driver and when no connections are created it follows the "Dummy-Driver". This might cause some latency issues when making connections as the clients will transit to a new driver, making the timing measurements to wobble for a moment and ultimately increasing the latency.

To avoid this, here are some recommendations. Still, always try to follow the official PipeWire documentation.

* Use the Pro audio profile.
* Do not have passive PipeWire nodes (`node.passive` set to `true`) to avoid driver changes.
* Schedule Overwitch (both CLI and GUI) under the hardware driver. This can be achieved by using the properties `node.link-group` or `node.group`.

```
# List your node.groups:
$ pw-cli info all | grep -i node.group

# Set node.group to your output device (e.g. "pro-audio-0")
$ PIPEWIRE_PROPS='{ node.group = "pro-audio-0" }' overwitch-cli
```

### Latency

Device to JACK latency is different from JACK to device latency though they are very close. These latencies are the transferred frames to and from the device and, by default, these are performed in 24 blocks of 7 frames (168 frames).

Thus, the minimum theoretical latency is the device frames plus the JACK buffer frames plus some additional buffer frames are used in the resamplers but it is unknown how many.

To keep latency as low as possible, the amount of blocks can be configured in the JACK clients. Values between 2 and 32 can be used. However, values as high as `8` might cause the device to crash with the error below.

```
ERROR:engine.c:352:cb_xfr_audio_out: h2o: Error on USB 613 audio transfer (0 B): LIBUSB_TRANSFER_TIMED_OUT
```

### Tuning

Although this is a matter of JACK, Ardour and OS tuning, here you have some tips.

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
        sudo cpupower -c $c frequency-set -g performance
      fi
    fi
  done
}

function enable() {
  for c in $(seq 1 $(cat /sys/devices/system/cpu/present | awk -F- '{print $2}')); do
    echo 1  | sudo tee /sys/devices/system/cpu/cpu$c/online > /dev/null
    sudo cpupower -c $c frequency-set -g ondemand
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

With `rtirq-init` package installed, I simply let the script reconfigure the priorities of IRQ handling threads and then I run JACK, Overwitch and the desired applications. Notice that real time priorities are set by default so there is **no need** to set explicitely these.

```
$ sudo /etc/init.d/rtirq start
```

While using a RT kernel is not needed, it will help even more to reduce latency and xruns.

```
$ uname -v
#1 SMP PREEMPT_RT Debian 6.12.41-1 (2025-08-12)
```

Lastly, disable your system sounds.

With all this configuration I get no JACK xruns with 64 frames buffer (2 periods) and occasional xruns with 32 frames buffer (3 periods) with network enabled and under normal usage conditions.

Although you can run Overwitch with verbose output this is **not recommended** unless you are debugging the application.
