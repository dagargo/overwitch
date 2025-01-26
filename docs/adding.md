---
layout: base
title: Adding devices
permalink: /addingdevices/
---

## Adding devices

Devices are specified in `JSON` files. As devices can be user-defined, there is no need to recompile the code or wait for new releases to use Overwitch with new devices.

Overwitch first searches a device in all the files in the `~/.config/overwitch/devices.d` directory, where each one contains a `JSON` object; then into the user defined file `~/.config/overwitch/devices.json`, which is a `JSON` array; and finally into the file included in the project, which is a `JSON` array too.

### JSON format

This is a self-explanatory device. Keep in mind that, while files in the `~/.config/overwitch/devices.d` directory follow this definition, both the included file and the custom file are an array of these devices.

There are 3 types of formats, depending on how many bytes are used to store samples in the USB blocks.

* Format 1 is reserved for Analog Rytm MKI and Analog Form MKI and Keys.
* Format 2 uses 4 bytes integers.
* Format 3 uses 3 bytes integers. Note that some tracks might use 4 bytes to store the samples even though the actual samples are only 3 bytes.

```
{
  "pid": 2899,
  "name": "Analog Heat +FX",
  "format": 2,
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
