---
layout: base
title: Usage
permalink: /usage/
order: 3
---

## Usage

Overwitch contains two JACK clients. One is a D-Bus and systemd compatible hotplug service what will manage all the connected devices, which is used by the GUI application; and the other is a single client CLI application. Additionally, a playing and recording utilities for the CLI are also included.

Regarding the JACK clients, latency needs to be under control and it can be tuned with the following parameters.

- Quality, which controls the resampler accuracy. The higher the quality, the higher CPU usage. A medium value is recommended. Notice that a value of 0 means the highest quality while a value of 4 means the lowest.
- Blocks, which controls the amount of data sent in a single USB operation. The more blocks, the higher latency but the lower CPU usage. As too lower values might stress the machines to the point of requiring a reboot, 10 is the minimum recommended value. If a device become unresponsive or you see the error below, increase the blocks and restart the device.

```
ERROR:engine.c:351:cb_xfr_audio_out: h2o: Error on USB audio transfer (0 B): LIBUSB_TRANSFER_TIMED_OUT
```

### Use cases

For all use cases, the default installation is needed.

#### Typical desktop user

Just use `overwitch` (GUI). This will start up the included D-Bus service when needed. No other tools are required.
Notice that closing the application window does **not** terminate the D-Bus service, which means that the hotplug system is still running. Starting the application again will show the window with the ongoing state of all the devices. To terminate everything, click on the exit menu.

#### Non GUI user

Install the systemd service from the `systemd` directory and start it up. This uses the same executable as the D-Bus service and will start a JACK client for any devices as soon as it is connected. No other tools are required. Stopping the service will stop all the devices.

#### Testing

In case of testing Overwitch, only the CLI utilities should be used. In this scenario, use all of these with `-vv` to add some debugging output.

### overwitch

The GUI is self explanatory and does not requiere any parameter passed from the command line. It controls the D-Bus service, which will manage any Overbridge device.

Notice that changing the preferences will restart all the clients.

It is possible to rename Overbridge devices by simply editing its name on the list. Still, as JACK devices can not be renamed while running, the clients will be restarted.

### overwitch-service

Using `overwitch-service` allows having a systemd unit which uses device hotplug. This will load the configuration from the same configuration file the GUI uses.

This is a configuration example with the recommended properties. Not all the properties are shown here.

```
$ cat ~/.config/overwitch/preferences.json
{
  "blocks" : 12,
  "timeout" : 10,
  "quality" : 2,
  "pipewireProps" : "{ node.group = \"pro-audio-0\" }"
}
```

Obviously, when running the service there is no need for the GUI whatsoever.

Notice that this binary is used by both the D-Bus service and the systemd service is rarely needed to be run like this.

### overwitch-cli

The CLI interface allows the user to create a single JACK client and have full control the options to be used.

First, list the available devices. The first element is an internal ID that allows to identify the devices.

```
$ overwitch-cli -l
0: Digitakt (ID 1935:0b2c) at bus 003, address 021
```

Then, you can choose which device you want to use by using `-n`.

```
$ overwitch-cli -n 0
```

You can select the device by name too but the use of this option is discouraged and `-n` should be used instead. When using this option, the first device in the list will be used.

```
$ overwitch-cli -d Digitakt
```

To stop, just press `Ctrl+C`. You'll see an oputput like the one below. Notice that we are using the verbose option here but it is **not recommended** to use it and it is showed here for illustrative purposes only.

```
$ overwitch-cli -n 0 -vv -b 12
DEBUG:overwitch.c:308:ow_get_device_desc_file: Searching device in /home/user/.config/overwitch/devices.json
DEBUG:overwitch.c:314:ow_get_device_desc_file: Failed to open file “/home/user/.config/overwitch/devices.json”: No such file or directory
DEBUG:overwitch.c:308:ow_get_device_desc_file: Searching device in /usr/local/share/overwitch/devices.json
DEBUG:overwitch.c:161:ow_get_device_desc_reader: Device with PID 2860 found
DEBUG:overwitch.c:90:ow_get_device_list: Found Digitakt (bus 003, address 018, ID 1935:0b2c)
DEBUG:engine.c:550:ow_engine_init: USB transfer timeout: 10
DEBUG:engine.c:432:ow_engine_init_mem: Blocks per transfer: 12
DEBUG:engine.c:446:ow_engine_init_mem: o2h: USB in frame size: 48 B
DEBUG:engine.c:447:ow_engine_init_mem: h2o: USB out frame size: 8 B
DEBUG:engine.c:475:ow_engine_init_mem: o2h: USB in block size: 368 B
DEBUG:engine.c:477:ow_engine_init_mem: h2o: USB out block size: 88 B
DEBUG:engine.c:485:ow_engine_init_mem: o2h: audio transfer size: 4032 B
DEBUG:engine.c:487:ow_engine_init_mem: h2o: audio transfer size: 672 B
DEBUG:engine.c:1085:ow_engine_load_overbridge_name: USB control in data (32 B): Digitakt
DEBUG:engine.c:1105:ow_engine_load_overbridge_name: USB control in data (16 B): 0097       1.52A
DEBUG:dll.c:153:ow_dll_host_init: Initializing host side of DLL...
DEBUG:jclient.c:568:jclient_start: Starting thread...
DEBUG:jclient.c:175:jclient_set_sample_rate_cb: JACK sample rate: 48000
DEBUG:jclient.c:446:jclient_run: Using RT priority 77...
DEBUG:jclient.c:448:jclient_run: Registering ports...
DEBUG:jclient.c:453:jclient_run: Registering output port Main L...
[...]
DEBUG:jclient.c:472:jclient_run: Registering input port Main R Input...
DEBUG:resampler.c:594:ow_resampler_init_samplerate: Setting resampler sample rate to 48000
DEBUG:resampler.c:585:ow_resampler_init_buffer_size: Setting resampler buffer size to 128
DEBUG:resampler.c:175:ow_resampler_reset_buffers: Resetting buffers...
DEBUG:resampler.c:157:ow_resampler_clear_buffers: Clearing buffers...
DEBUG:dll.c:164:ow_dll_host_reset: Resetting the DLL...
DEBUG:engine.c:944:ow_engine_start: Starting thread...
DEBUG:dll.c:53:ow_dll_overbridge_init: Initializing Overbridge side of DLL (48000.0 Hz, 84 frames)...
DEBUG:jclient.c:518:jclient_run: Activating...
DEBUG:jclient.c:166:jclient_set_buffer_size_cb: JACK buffer size: 128
DEBUG:jclient.c:527:jclient_run: Activated
DEBUG:resampler.c:421:ow_resampler_compute_ratios: Digitakt @ 003,018 (Digitakt): Setting Overbridge side to steady (notifying readiness)...
DEBUG:engine.c:776:run_audio: Notification of readiness received from resampler
DEBUG:engine.c:798:run_audio: Booting or clearing engine...
DEBUG:resampler.c:450:ow_resampler_compute_ratios: Digitakt @ 003,018 (Digitakt): Booting resampler...
Digitakt @ 003,018 (Digitakt): o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000000
DEBUG:resampler.c:473:ow_resampler_compute_ratios: Digitakt @ 003,018 (Digitakt): Tuning resampler...
Digitakt @ 003,018 (Digitakt): o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000082
Digitakt @ 003,018 (Digitakt): o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000057
DEBUG:resampler.c:492:ow_resampler_compute_ratios: Digitakt @ 003,018 (Digitakt): Running resampler...
DEBUG:jclient.c:74:jclient_set_latency_cb: JACK latency request
DEBUG:jclient.c:92:jclient_set_latency_cb: h2o latency: [ 128, 128 ]
DEBUG:jclient.c:74:jclient_set_latency_cb: JACK latency request
DEBUG:jclient.c:78:jclient_set_latency_cb: o2h latency: [ 128, 128 ]
DEBUG:resampler.c:313:resampler_o2h_reader: o2h: Emptying buffer (6144 B) and running...
DEBUG:resampler.c:296:resampler_o2h_reader: o2h: Audio ring buffer underflow (0 < 4032)
[...]
DEBUG:resampler.c:296:resampler_o2h_reader: o2h: Audio ring buffer underflow (0 < 4032)
Digitakt @ 003,018 (Digitakt): o2h latency:  4.2 [ 2.7,  4.4] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000056
[...]
Digitakt @ 003,018 (Digitakt): o2h latency:  3.0 [ 2.7,  4.6] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000018
^C
DEBUG:jclient.c:293:jclient_stop: Stopping client...
DEBUG:resampler.c:641:ow_resampler_stop: Stopping resampler...
DEBUG:engine.c:1069:ow_engine_stop: Stopping engine...
DEBUG:engine.c:860:run_audio: Processing remaining events...
Digitakt @ 003,018 (Digitakt): o2h latency: -1.0 [-1.0, -1.0] ms; h2o latency: -1.0 [-1.0, -1.0] ms, o2h ratio: 1.000014
DEBUG:jclient.c:532:jclient_run: Exiting...
DEBUG:jclient.c:158:jclient_jack_client_registration_cb: JACK client Digitakt is being unregistered...
```

You can list all the available options with `-h`.

```
$ overwitch-cli -h
overwitch 2.1
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
$ overwitch-play -n 0 audio_file
```

You can list all the available options with `-h`.

```
$ overwitch-play -h
overwitch 2.1
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
$ overwitch-record -n 0
^C
829920 frames written
Digitakt_2025-08-28T11:00:08.wav file created
```

By default, it records all the output tracks from the Overbridge device but it is possible to select which ones to record. First, list the devices in verbose mode to see all the available tracks.

```
$ overwitch-record -l -v
DEBUG:overwitch.c:308:ow_get_device_desc_file: Searching device in /home/user/.config/overwitch/devices.json
DEBUG:overwitch.c:314:ow_get_device_desc_file: Failed to open file “/home/user/.config/overwitch/devices.json”: No such file or directory
DEBUG:overwitch.c:308:ow_get_device_desc_file: Searching device in /usr/local/share/overwitch/devices.json
DEBUG:overwitch.c:161:ow_get_device_desc_reader: Device with PID 2860 found
DEBUG:overwitch.c:90:ow_get_device_list: Found Digitakt (bus 003, address 021, ID 1935:0b2c)
0: Digitakt (ID 1935:0b2c) at bus 003, address 021
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

Then, just select the output tracks to record as this. `0` means that the track is not recorded while any other character means it will. In this example, we are recording outputs 1 and 2 (main output pair).

```
$ $ overwitch-record -n 0 -m 110000000000
^C
638400 frames written
Digitakt_2025-08-28T11:21:38.wav file created
```

It is not neccessary to provide all tracks, meaning that using `11` as the mask will behave exactly as the example above.

You can list all the available options with `-h`.

```
$ overwitch-record -h
overwitch 2.1
Usage: overwitch-record [options]
Options:
  --use-device-number, -n value
  --use-device, -d value
  --bus-device-address, -a value
  --track-mask, -m value
  --disk-buffer-size-kilobytes, -s value
  --blocks-per-transfer, -b value
  --usb-transfer-timeout, -t value
  --list-devices, -l
  --verbose, -v
  --help, -h
```
