# Contributing to OpenOSC

Thank you for your interest in OpenOSC. Bug reports, hardware test results, documentation fixes and
code are all welcome.

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md). By taking part, you agree to it.

## Where to go

| You want to | Use |
| --- | --- |
| Ask a question or get help with installation | [Discussions](https://github.com/bve/OpenOSC/discussions) |
| Report a bug | [New issue → Bug report](https://github.com/bve/OpenOSC/issues/new/choose) |
| Suggest a feature | [New issue → Feature request](https://github.com/bve/OpenOSC/issues/new/choose) |
| Report a security problem | Privately, as described in the [Security policy](SECURITY.md) |

Before opening an issue, search the existing issues and discussions.

## Reporting bugs

A good bug report lets someone else see the same problem. Please include:

- the device model (ZT703S or ZT706S);
- the firmware version from **Settings → About**, or the release you installed;
- the performance profile (200/100, 280/140 or 360/90);
- what you did, what you expected and what happened instead;
- a screenshot if the problem is visible on screen (PNG screenshots are saved on the device in
  `/openosc/SCREENSHOTS/`);
- for USB problems: the operating system, the PC software and whether a USB hub is used.

Do not attach your factory block backup (`FACTORY_BOOT_08000000.bin`). It contains your unit's own
calibration and is not needed for bug reports.

## Testing on ZT706S

The ZT706S build runs on users' devices, but the maintainer does not own one and many features are
untested on that model. If you own a ZT706S, test reports are especially valuable. You can back up
the factory block first (optional, see the [README](README.md#optional-back-up-the-factory-block))
and open an issue with your results, even if everything works.

## Building

Install [PlatformIO](https://platformio.org/), then run:

```sh
pio run
```

This builds Recovery and the firmware for both devices. See [Building](README.md#building) for the
output files. To build one device only, run `pio run -e recovery -e firmware` (ZT703S).

The board definitions live in `config/boards/*.json`. After changing one, regenerate the derived
files:

```sh
python3 scripts/generate_board_config.py
```

The build stops with an error if the generated files are out of date.

## Pull requests

1. For anything larger than a small fix, open an issue first so we can agree on the approach.
2. Keep each pull request focused on one change.
3. Make sure `pio run` builds all four environments without new warnings.
4. Test on hardware when the change affects the device behaviour, and say which model you used.
   If you could not test, say so.
5. Follow the style of the surrounding code: C++17, no exceptions, no RTTI and no heap
   allocation outside LVGL.
6. Do not modify the code in `vendor/`, except to update a third-party component as a whole.
7. Do not add code that writes to the factory block (`0x08000000`–`0x0801FFFF`).

## License

OpenOSC is licensed under the [GNU General Public License v3.0](LICENSE). By submitting a
contribution, you agree that it is distributed under the same license.
