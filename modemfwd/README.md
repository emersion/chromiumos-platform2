# Modem Firmware Daemon

This daemon abstracts out the common portions of updating modem firmware, i.e.
deciding whether there is currently newer firmware available and getting the
modem ready to be flashed with new firmware.

## Modem-specific program API

In order to enforce a process boundary between the open-source `modemfwd` and
potentially non-open-source modem firmware updaters, we farm out steps that
require modem-specific knowledge to different programs. `modemfwd` will call
into these programs with different flags to request different services. These
flags are declared in the [system API] repo.

* `--get_fw_info`: return version information for the currently installed
  firmware (see below)
* `--prepare_to_flash`: put the modem into firmware download mode
* `--flash_main_fw=<file>`
* `--flash_carrier_fw=<file>`
* `--reboot`

`--get_fw_info` should return the main firmware on the first line, the carrier
UUID on the next line and the carrier version on the one after that:

```
$ modem_program --get_fw_info
11.22.33.44
big-long-carrier-uuid-string
55.66
```

The carrier UUID should match with one from the shill mobile operator DB.

All commands should return 0 on success and something non-zero on failure.
`modemfwd` will look for these binaries in the directory passed as the
`--helper_directory` argument on the command line.

## Helper and firmawre directory structure

The protos defined in `helper_manifest.proto` and `firmware_manifest.proto`
define manifests that should be provided in the helper and firmware directories
so modemfwd can figure out what devices, carriers, etc. the contents can be
used with.

[system API]: https://chromium.googlesource.com/chromiumos/platform/system_api/+/master/switches/modemfwd_switches.h
