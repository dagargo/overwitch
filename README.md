# Overwitch

Overwitch is an [Overbridge 2](https://www.elektron.se/overbridge/) device client for [JACK (JACK Audio Connection Kit)](http://linux-audio.com/jack/), meaning that you can connect to, listen to, and record compatible [Elektron](www.elektron.se) music devices on GNU/Linux systems (however, you can not use Overwitch to control Elektron devices through the Overbridge protocol, for example controlling a filter, as is possible on Windows and MacOS).

At the moment, Overwitch supports the following devices:
- [Analog Four MKII](https://www.elektron.se/products/analog-four-mkii/)
- [Analog Rytm MKII](https://www.elektron.se/products/analog-rytm-mkii/)
- [Digitakt](https://www.elektron.se/products/digitakt/)
- [Digitone](https://www.elektron.se/products/digitone/)
- [Digitone Keys](https://www.elektron.se/products/digitone-keys/)
- [Analog Heat (both MKI and MKII)](https://www.elektron.se/products/analog-heat-mkii/).

Analog Four MKI, Analog Keys, and Analog Rytm MKI are not supported yet.

@Speldosa: TO-DO: Is "Overbridge 1 devices" really the best name for the unsupported devices above? For example, Analog Heat MKI was supported in Overbridge 1 but also works with Overwitch according to the specifications above. I've removed this concept for now.

Note that Overwitch is a terminal application, meaning that all commands are ment to be typed into a terminal window (see [this tutorial](https://www.youtube.com/watch?v=s3ii48qYBxA) for a primer on how to use the Terminal if you're unfamiliar with it).

If you want to discuss this software, you can do so in [this thread](https://www.elektronauts.com/t/overwitch-a-jack-client-for-overbridge-devices-aka-overbridge-for-linux/153983/41) on the Elektronauts forum.

# Acknowledgements

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump). The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have also been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

# Installation procedure

## Package dependencies of Overwitch

In order to install and run this program, you need to have the following programs installed:
- [automake](https://www.gnu.org/software/automake/)
- [libusb-1.0-0-dev](https://packages.debian.org/buster/libusb-1.0-0-dev)
- [libjack-jackd2-dev](https://packages.debian.org/sid/libjack-jackd2-dev)
- [libsamplerate0-dev](https://packages.debian.org/sid/libsamplerate0-dev)

If you're unsure whether you already have these programs installed on your system, you can simply run:

```sudo apt install automake libusb-1.0-0-dev libjack-jackd2-dev libsamplerate0-dev```

@Speldosa: TO-DO: I wasn't asked to configure jackd2. Why? Can you access this configuration some other way? Also, what is meant by being "in the audio group"? It seems like the configuration doesn't always show up for everybody (it depends on your system): https://jackaudio.org/faq/linux_rt_config.html

As this will install `jackd2` (that is, version 2 of JACK), you will be asked to configure it to be run with real time priority. Be sure to answer yes. With this, the `audio` group would be able to run processes with real time priority. Be sure to be in the `audio` group too.

## Installation of Overwitch

First, download all files contained within this repository (either by downloading one of the releases or by downloading the master branch) and unzip them in a folder of your choosing. Then navigate to that folder in a terminal window and run the following four [Autotools commands](https://www.gnu.org/software/automake/manual/html_node/Autotools-Introduction.html):

```
autoreconf --install
./configure
make
sudo make install
```

@Speldosa: TO-DO: I needed to use "sudo" before several of the commands above in order to make them work. Is this normal? Should this be mentioned?

After this, you will be able to run Overwitch no matter which directory you're at in the terminal application. You can even delete the downloaded repository files if you'd like.

# Usage

You run Overwitch by typing `overwitch` in your terminal window followed by one or several options, and then pressing enter. For example, running Overwitch with the help option, `overwitch -h`, shows all the available options. These are:

- Use device: -d
- Resampling quality: -q
- Transfer blocks: -b
- List devices: -l
- Verbose: -v
- Help: -h

In the rest of this section, we'll go through and exemplify these options.

## Basic usage

In order to detect Overbridge 2 devices that are connected via USB to the computer, they need to be switched on and configured to run in Overbrige mode, which can be done through the system setting menu on the devices themselves (it is probably also a good idea to make sure they're running the latest firmware as well).

Then, you can list the available devices by running:

```
overwitch -l
```

This could output something like the following:

```
Bus 001 Port 001:003 Device 057: ID 1935:0010 Analog Rytm MKII
Bus 001 Port 001:002 Device 056: ID 1935:000e Analog Four MKII
```

which would mean that you have an Analog Rytm MKII and an Analog Four MKII connected to your computer. In order to start using one of these devices with Overwitch, you can run the following command (notice the use of quotation around the name of the device since it contains spaces; this isn't needed if the name doesn't contain spaces):

```
chrt -f 35 overwitch -d "Analog Four MKII" -v
```

To stop this process, just press `Ctrl+C`.

It is recommended to run `overwitch` with real time priority, which is why `chrt -f 35` is added in front of the `overwitch` command it in the example above. The `-d` option simply tells Overwitch to connect to whatever device name that is specified afterwards. The `-v` option puts Overwitch in verbose mode, meaning that information that normally is hidden will be shown. This can be good for illustrative or debugging purposes, but it is **not recommended** to use it for normal use, where it can be omitted.

When running the code above, you'll see an output like the one below:

```
Device: Analog Four MKII (outputs: 8, inputs: 6)
JACK sample rate: 48000
JACK buffer size: 1024
DEBUG:overbridge.c:757:(overbridge_run): Starting MIDI thread...
DEBUG:overbridge.c:765:(overbridge_run): Starting device thread...
DEBUG:jclient.c:347:(jclient_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 1.000175, 0.999826; curr. ratios: 0.999866, 1.000134
DEBUG:jclient.c:347:(jclient_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999917, 1.000083; curr. ratios: 0.999946, 1.000054
DEBUG:jclient.c:347:(jclient_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 0.999970, 1.000030; curr. ratios: 0.999985, 1.000015
DEBUG:jclient.c:347:(jclient_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 1.000002, 0.999998; curr. ratios: 1.000005, 0.999995
DEBUG:jclient.c:347:(jclient_compute_ratios): Max. latencies (ms): 0.0, 0.0; avg. ratios: 1.000012, 0.999988; curr. ratios: 1.000013, 0.999987
```

## Latency

When starting up Overwitch, in order to limit latency to the lowest possible value, audio is not sent through during the first few seconds.

Device to JACK latency is different from JACK to device latency though they are very close. These latencies are the transferred frames to and from the device and, by default, these are performed in 24 groups (blocks) of 7 frames (168 frames).

Thus, the minimum theoretical latency is the device frames plus the JACK buffer frames plus some additional buffer frames are used in the resamplers but it is unknown how many.

But looks like this block amount can be changed. With the option `-b` we can override this value indicating how many blocks are processed at a time. The default value is 24 but values between 2 and 32 can be used. Notice that this option is **highly experimental**.

## -q option (rename this to something better)

@Speldosa: TO-DO: Write me! I can't find any information about this option, so I can't write it myself.

## Using multiple devices at the same time

If you want to run several instances of Overwitch at the same time, in order to use several devices at the same time, you can open and run the application in two separate terminal windows. Alternatively, if you want to run everything in a single window, you can put an `&` between two different Overwitch calls, for example `chrt -f 35 overwitch -d "Analog Four MKII" & chrt -f 35 overwitch -d "Analog Rytm MKII"`. For more information, see [this link](https://unix.stackexchange.com/questions/112381/how-do-i-run-two-ongoing-processes-at-once-in-linux-bash/112382).

## Troubleshooting

- If you're getting error messages saying that the JACK server cannot be connected to, it might be the case that JACK isn't running in the background on your computer. You could try to solve this by running `jack_control start` before running Overwitch.

# Listening to and recording your devices

If you have a DAW (digital audio workstation), such as [Bitwig Studio](https://www.bitwig.com) or [REAPER](https://www.reaper.fm/download.php), or some more basic recording software, like [Audacity](https://www.elektronauts.com/t/overwitch-a-jack-client-for-overbridge-devices-aka-overbridge-for-linux/153983/44), you can access the inputs and outputs of your Elektron devices as you would with for example a soundcard. Otherwise, if you'd like to listen to and record your devices directly via the terminal interface, you can follow [this guide](https://www.elektronauts.com/t/overwitch-a-jack-client-for-overbridge-devices-aka-overbridge-for-linux/153983/44) on how to use `jack_connect` and `jack_capture` for this purpose.

# Advanced usage/modifying the source code

## Adding devices

@Speldosa: TO-DO: Aren't all possible devices already added now? Should this be mentioned here?

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
  DIGITAKT_DESC, DIGITONE_DESC, ARMK2_DESC
};
```

## Tuning

@Speldosa: TO-DO: Is it possible to use some other word rather than "Tuning"? When I hear tuning, especially in this context, I'm thinking about tuning a piano :)

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
