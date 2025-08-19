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
  "type": 2,
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

Notice that there are 3 types of devices, depending on the transfer type and how many bytes are used to store samples in the USB blocks.

* Type 1 (isochronous transfers) is reserved for Analog Rytm MKI and Analog Four MKI and Keys.
* Type 2 (interrupt transfers) uses 4 bytes integers.
* Type 3 (interrupt transfers) uses 3 bytes integers. Note that some tracks might use 4 bytes to store the samples even though the actual samples are only 3 bytes.
