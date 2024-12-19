---
layout: base
title: Adding devices
permalink: /addingdevices/
---

## Adding devices

Devices can be specified in two ways.

* Outside the library, in `JSON` files. Useful for a typical desktop usage as devices can be user-defined, so no need to recompile the code or wait for new releases.
* Inside the library, in `C` code. Useful when using the `liboverwitch` library and `GLib` dependencies are unwanted. Notice that the library is compiled with `JSON` support by default. See the [`Installation`](#Installation) section.

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

{% raw %}
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
{% endraw %}
