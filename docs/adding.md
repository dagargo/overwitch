---
layout: base
title: Adding devices
permalink: /addingdevices/
order: 4
---

## Adding devices

Devices are specified in `JSON` files. As devices can be user-defined, there is no need to recompile the code or wait for new releases to use Overwitch with new devices of the same type.

Overwitch first searches a device in all the files with `json` extension in the `~/.config/overwitch/devices.d` directory, where each file contains a single `JSON` object; then into the user defined file `~/.config/overwitch/devices.json`, which is a `JSON` array of these objects; and finally into the devices file distributed with Overwitch, which is a `JSON` array too.

### JSON format

This example is a self-explanatory `JSON` object for a device.

```
{
  "pid": 2899,
  "name": "Analog Heat +FX",
  "version": "2",
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

Notice that there are 4 versions, depending on the transfer type and how many bytes are used to store samples in the USB blocks.

* Version 1 (isochronous transfers) uses 2 or 3 bytes integers and is used by Overbridge 1 devices, which are Analog Four MKI, Analog Keys and Analog Rytm MKI.
* Version 2 (interrupt transfers) uses 4 bytes integers and is used by classig Overbridge 2 devices.
* Version 2.1 (interrupt transfers) uses 3 bytes integers. Note that some tracks might use 4 bytes to store the samples even though the actual samples are only 3 bytes. Used by Digitakt II and Digitone II.
* Version 3 (isochronous transfers). Used by Tonverk.

### Overbridge 1 devices

Overbridge 1 devices need an OS that supports the Overbridge protocol used by Overbridge 2, like the Analog Four MKI OS 1.56. Overwitch shows an error with older versions.

The tracks these devices send and receive, and their bit depth, are configured with the Overbridge Control Panel on Windows or macOS and stored in the device. The device definition must match this layout:

* `output_tracks` are the tracks sent by the device and `input_tracks` the tracks sent to the device, in the same order as in the Control Panel.
* The `size` is 2 for 16 bits and 3 for 24 bits and it must be the same for all the tracks in a direction.
* At least one track per direction is required.

Overwitch checks the definition against the layout stored in the device and, if they do not match, it shows the tracks in the device. For instance, this is the definition of an Analog Four MKI sending its main outputs and its first 2 synth tracks and receiving 2 tracks, all of them with 24 bits.

```
{
  "pid": 2891,
  "name": "Analog Four MKI",
  "version": "1",
  "input_tracks": [
    {
      "name": "Main L Input",
      "size": 3
    },
    {
      "name": "Main R Input",
      "size": 3
    }
  ],
  "output_tracks": [
    {
      "name": "Main L",
      "size": 3
    },
    {
      "name": "Main R",
      "size": 3
    },
    {
      "name": "Synth Track 1",
      "size": 3
    },
    {
      "name": "Synth Track 2",
      "size": 3
    }
  ]
}
```

The distributed devices file includes the Analog Four MKI and, untested, the Analog Keys and the Analog Rytm MKI, all of them with their main outputs and inputs with 16 bits.

The layout can also be set from Overwitch. When every track has a `code`, which is the channel the device uses for it, Overwitch stores this layout in the device if it is different, so there is no need to use the Control Panel. Codes must be set for all the tracks or for none. These are the codes known for the Analog Four MKI.

| Direction | Track | Code |
| --- | --- | --- |
| Output | Main L, Main R | 160, 161 |
| Output | Synth Track 1 to Synth Track 4 | 128 to 131 |
| Output | Input L, Input R | 192, 193 |
| Input | Main L Input, Main R Input | 32, 33 |
| Input | Synth Track 1 Input to Synth Track 4 Input | 0 to 3 |

The GUI edits this layout with the button in the Layout column if the definition lists the tracks that can be chosen with `available_output_tracks` and `available_input_tracks`, which need a `name` and a `code` for every track. The distributed Analog Four MKI definition includes these. The layout is saved in a definition in `~/.config/overwitch/devices.d` and the device is restarted. Devices that can not be started, for instance because their definition does not match the layout stored in the device, are also shown, so their layout can be fixed this way. Like the Control Panel, the GUI does not allow more than 6 tracks with 24 bits or 10 tracks with 16 bits, adding both directions, nor more than 8 tracks per direction.

For these devices, a block contains 1 ms of audio and the valid amount of blocks is between 1 and 5, being 3 the default.
