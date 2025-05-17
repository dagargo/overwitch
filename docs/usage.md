---
layout: base
title: Usage
permalink: /usage/
order: 3
---

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
