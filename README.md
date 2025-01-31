# Overwitch

[//]: # (Do not modify this file manually.)
[//]: # (This file is generated from the docs directory by executing `make`.)

Overwitch is a set of JACK (JACK Audio Connection Kit) clients for Overbridge 2 devices. Since PipeWire is ABI compatible with JACK, Overwitch works with PipeWire too.

![Overwitch GUI screenshot](docs/images/screenshot.png "Overwitch GUI")

This project is based on the Overbridge USB reverse engineering done by Stefan Rehm in [dtdump](https://github.com/droelfdroelf/dtdump).

The papers [Controlling adaptive resampling](https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf) and [Using a DLL to filter time](https://kokkinizita.linuxaudio.org/papers/usingdll.pdf) by Fons Adriaensen have been very helpful and inspiring, as well as his own implementation done in the zita resamplers found in the [alsa tools](https://github.com/jackaudio/tools) project.

At the moment, it provides support for all Overbridge 2 devices, which are Analog Four MKII, Analog Rytm MKII, Digitakt, Digitakt II, Digitone, Digitone Keys, Analog Heat, Analog Heat MKII, Analog Heat +FX and Syntakt.

Overbridge 1 devices, which are Analog Four MKI, Analog Keys and Analog Rytm MKI, are not supported yet.

Overwitch consists of 5 different binaries divided in 2 categories: multi-device applications (they can **not** be used simultaneously) and single-device utilities.

Multi-device applications:

* `overwitch`, which is a GUI application.
* `overwitch-service`, which is a CLI application meant to be used as a systemd service.

Utilities:

* `overwitch-cli`, which is a single-client program.
* `overwitch-play`, which plays multitrack audio thru Overbridge devices.
* `overwitch-record`, which records multitrack audio from Overbridge devices.

For a device manager application for Elektron devices, check [Elektroid](https://dagargo.github.io/elektroid/).

## Installation

As with other autotools project, you need to run the commands below. There are a few options available.

* If you just want to compile the command line applications, pass `CLI_ONLY=yes` to `./configure`.
* If you do not want to use the JSON devices files, pass `JSON_DEVS_FILE=no` to `./configure`. This is useful to eliminate GLIB dependencies when building the library. In this case, the devices configuration used are the ones in the source code.

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
- libjson-glib-dev (only if `JSON_DEVS_FILE=no` is not used)
- libgtk-4-dev (only if `CLI_ONLY=yes` is not used)
- systemd-dev (only used to install the udev rules)

You can easily install all them by running `sudo apt install automake libtool libusb-1.0-0-dev libjack-jackd2-dev libsamplerate0-dev libsndfile1-dev autopoint gettext libsystemd-dev libjson-glib-dev libgtk-4-dev systemd-dev`.

For Fedora, run `sudo yum install automake libtool libusb1-devel jack-audio-connection-kit-devel libsamplerate-devel libsndfile-devel gettext-devel json-glib-devel gtk4-devel systemd-devel` to install the build dependencies.

As this will install `jackd2`, you would be asked to configure it to be run with real time priority. Be sure to answer yes. With this, the `audio` group would be able to run processes with real time priority. Be sure to be in the `audio` group too.

### systemd service

For embedded systems or users not wanting to use the GUI, it is recommended to install the systemd service unit by running `sudo make install` from the `systemd` directory.

To allow the service to be started at boot, running `systemctl --user enable overwitch.service` is needed.

## Usage

Overwitch contains three JACK clients, one for the desktop, one for the command line and one to be used as a service. Additionally, a recording and playing utilities for the command line are also included.

Regarding the JACK clients, latency needs to be under control and it can be tuned with the following parameters.

- Blocks, which controls the amount of data sent in a single USB operation. The higher, the higher latency but the lower CPU usage. 4 blocks keeps the latency quite low and does not impact on the CPU.
- Quality, which controls the resampler accuracy. The higher, the more CPU consuming. A medium value is recommended. Notice that in `overwitch-cli`, a value of 0 means the highest quality while a value of 4 means the lowest.

### overwitch

The GUI is self explanatory and does not requiere any parameter passed from the command line. It runs all found Overbridge device in different JACK clients.

Notice that once an Overbridge device is running the options can not be changed so you will need to stop the running instances and refresh the list.

It is possible to rename Overbridge devices by simply editing its name on the list. Still, as JACK devices can not be renamed while running, the device will be restarted.

### overwitch-service

Using `overwitch-service` allows having a systemd unit which uses device hotplugging. This will load the configuration from the same config file the GUI uses.

This is a configuration example with the recommended properties. Not all the properties are shown here.

```
$ cat ~/.config/overwitch/preferences.json
{
  "blocks" : 8,
  "timeout" : 10,
  "quality" : 2,
  "pipewireProps" : "{ node.group = \"pro-audio-0\" }"
}
```

Obviously, when running the service there is no need for the GUI whatsoever.

### overwitch-cli

The CLI interface allows the user to create a single JACK client and have full control the options to be used.

First, list the available devices. The first element is an internal ID that allows to identify the devices.

```
$ overwitch-cli -l
0: Digitakt (ID 1935:000c) at bus 001, address 005
```

Then, you can choose which device you want to use by using one of these options. Notice that the second option will only work for the first device found with that name.

```
$ overwitch-cli -n 0
$ overwitch-cli -d Digitakt
```

To stop, just press `Ctrl+C`. You'll see an oputput like the one below. Notice that we are using the verbose option here but it is **not recommended** to use it and it is showed here for illustrative purposes only.

```
$ overwitch-cli -n 0 -vv -b 8
[...]
DEBUG:engine.c:972:ow_engine_start: Starting audio thread...
DEBUG:dll.c:57:ow_dll_overbridge_init: Initializing Overbridge side of DLL...
DEBUG:jclient.c:167:jclient_set_buffer_size_cb: JACK buffer size: 64
DEBUG:resampler.c:584:ow_resampler_set_buffer_size: Setting resampler buffer size to 64
DEBUG:resampler.c:129:ow_resampler_reset_buffers: Resetting buffers...
DEBUG:resampler.c:111:ow_resampler_clear_buffers: Clearing buffers...
DEBUG:dll.c:163:ow_dll_host_reset: Resetting the DLL...
DEBUG:resampler.c:185:ow_resampler_reset_dll: DLL target delay: 208 frames (4.333333 ms)
DEBUG:engine.c:872:run_audio: Clearing buffers...
DEBUG:resampler.c:380:ow_resampler_compute_ratios: Digitakt @ 003,009 (Digitakt OG): Starting up resampler...
Digitakt @ 003,009: o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000000
DEBUG:resampler.c:418:ow_resampler_compute_ratios: Digitakt @ 003,009 (Digitakt OG): Tuning resampler...
Digitakt @ 003,009: o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 0.999754
Digitakt @ 003,009: o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000129
DEBUG:resampler.c:436:ow_resampler_compute_ratios: Digitakt @ 003,009 (Digitakt OG): Running resampler...
DEBUG:jclient.c:73:jclient_set_latency_cb: JACK latency request
DEBUG:jclient.c:79:jclient_set_latency_cb: o2h latency: [ 64, 0 ]
DEBUG:resampler.c:265:resampler_o2h_reader: o2h: Emptying buffer (3072 B) and running...
Digitakt @ 003,009: o2h latency:  2.3 [ 1.3,  2.7] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000031
^C
DEBUG:jclient.c:284:jclient_stop: Stopping client...
Digitakt @ 003,009: o2h latency:  1.7 [ 1.3,  2.7] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000009
DEBUG:engine.c:884:run_audio: Processing remaining event...
Digitakt @ 003,009: o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000009
DEBUG:jclient.c:523:jclient_run: Exiting...
DEBUG:jclient.c:159:jclient_jack_client_registration_cb: JACK client Digitakt OG is being unregistered...
```

You can list all the available options with `-h`.

```
$ overwitch 2.0
Usage: overwitch-cli [options]
Options:
  --use-device-number, -n value
  --use-device, -d value
  --bus-device-address, -a value
  --resampling-quality, -q value
  --blocks-per-transfer, -b value
  --usb-transfer-timeout, -t value
  --rt-priority, -p value
  --rename, -r value
  --list-devices, -l
  --verbose, -v
  --help, -h
```

### overwitch-play

This small utility let the user play an audio file thru the Overbridge devices.

```
$ overwitch-play -d Digitakt audio_file
```

You can list all the available options with `-h`.

```
$ overwitch-play -h
overwitch 2.0
Usage: overwitch-play [options] file
Options:
  --use-device-number, -n value
  --use-device, -d value
  --bus-device-address, -a value
  --blocks-per-transfer, -b value
  --usb-transfer-timeout, -t value
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
overwitch 2.0
Usage: overwitch-record [options]
Options:
  --use-device-number, -n value
  --use-device, -d value
  --bus-device-address, -a value
  --track-mask, -m value
  --track-buffer-size-kilobytes, -s value
  --blocks-per-transfer, -b value
  --usb-transfer-timeout, -t value
  --list-devices, -l
  --verbose, -v
  --help, -h
```

## Configuration

### PipeWire

Depending on your PipeWire configuration, you might want to pass some additional information to the different binaries by setting the `PIPEWIRE_PROPS` environment variable.

This value can be set in the GUI settings directly but any value passed at command launch will always take precedence over that configuration.

The proper way of setting this when using `overwitch-service` is in `~/.config/overwitch/preferences.json`, which is the same file the GUI uses.

Under PipeWire, a JACK client always follows a driver and when no connections are created it follows the "Dummy-Driver". This might cause some latency issues when making connections as the clients will transit to a new driver, making the timing measurements to wobble for a moment and ultimately increasing the latency,

To avoid that, here are some recommendations. Still, always try to follow the official PipeWire documentation.

* Use the Pro audio profile.
* Do not have passive PipeWire nodes (`node.passive` set to `true`) to avoid driver changes.
* Schedule Overwitch (both CLI and GUI) under the hardware driver. This can be achieved by using the properties `node.link-group` or `node.group`.

```
# List your node.groups:
$ pw-cli info all | grep -i node.group

# Set node.group to your output device (e.g. "pro-audio-0")
$ PIPEWIRE_PROPS='{ node.group = "pro-audio-0" }' overwitch
```

### Latency

Device to JACK latency is different from JACK to device latency though they are very close. These latencies are the transferred frames to and from the device and, by default, these are performed in 24 blocks of 7 frames (168 frames).

Thus, the minimum theoretical latency is the device frames plus the JACK buffer frames plus some additional buffer frames are used in the resamplers but it is unknown how many.

To keep latency as low as possible, the amount of blocks can be configured in the JACK clients. Values between 2 and 32 can be used.

### Tuning

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

Devices can be specified in two ways.

* Outside the library, in `JSON` files. Useful for a typical desktop usage as devices can be user-defined, so no need to recompile the code or wait for new releases.
* Inside the library, in `C` code. Useful when using the `liboverwitch` library and `GLib` dependencies are unwanted. Notice that the library is compiled with `JSON` support by default.

### Outside the library

This is a self-explanatory device definition from `res/devices.json`. The file is an array of these definitions.

There are 2 types of formats, depending on how many bytes are used to store samples in the USB blocks. Format 1 uses 4 bytes integers and format 2 uses 3 bytes integers. For format 2 devices, some tracks might use 4 bytes to store the samples even though the actual samples are only 3 bytes.

```
{
  "pid": 2899,
  "name": "Analog Heat +FX",
  "format": 1,
  "input_tracks": [
    {
      "name": "Main L Input",
      "size": 4
    },
    {
      "name": "Main R Input",
      "size": 4
    },
    {
      "name": "FX Send L",
      "size": 4
    },
    {
      "name": "FX Send R",
      "size": 4
    }
  ],
  "output_tracks": [
    {
      "name": "Main L",
      "size": 4
    },
    {
      "name": "Main R",
      "size": 4
    },
    {
      "name": "FX Return L",
      "size": 4
    },
    {
      "name": "FX Return R",
      "size": 4
    }
  ]
}
```

If the `~/.config/elektroid/elektron-devices.json` is found and the device is defined there, the definition will take precedence over the device defined in the included JSON file. This allows working with devices not included in the distributed file without the need of defining all of them.

### Inside the library

New Overbridge 2 devices could be easily added in the `overbridge.c` file as they all use the same protocol. For an explanation of the `format` member see the [`Outside the library`](###outside-the-library) section.

To define a new device, just add a new struct like this and add the new constant to the array. USB PIDs are already defined just above. For instance, if you were adding the Analog Heat +FX, you could do it like in the example below.

Notice that the definition of the device must match the device itself, so outputs and inputs must match the ones the device has and must be in the same order. As this struct defines the device, an input is a port the device will read data from and an output is a port the device will write data to.

```
static const struct ow_device_desc ANALOG_HEAT_FX_DESC = {
  .pid = ANALOG_HEAT_FX_PID,
  .name = "Analog Heat +FX",
  .format = 1,
  .inputs = 4,
  .outputs = 4,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "FX Send L",.size = 4},
		   {.name = "FX Send R",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "FX Return L",.size = 4},
		    {.name = "FX Return R",.size = 4}}
};

static const struct overbridge_device_desc OB_DEVICE_DESCS[] = {
  DIGITAKT_DESC, DIGITONE_DESC, ANALOG_HEAT_FX_DESC, NULL
};
```
