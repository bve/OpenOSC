# OpenOSC

[![Total release downloads](https://img.shields.io/github/downloads/bve/OpenOSC/total?style=plastic)](https://github.com/bve/OpenOSC/releases)
[![ZT703S firmware downloads](https://img.shields.io/github/downloads/bve/OpenOSC/OpenOSC-Firmware-ZT703S.OSC?style=plastic&label=ZT703S%20downloads&displayAssetName=false)](https://github.com/bve/OpenOSC/releases)
[![ZT706S firmware downloads](https://img.shields.io/github/downloads/bve/OpenOSC/OpenOSC-Firmware-ZT706S.OSC?style=plastic&label=ZT706S%20downloads&displayAssetName=false)](https://github.com/bve/OpenOSC/releases)
[![Latest release](https://img.shields.io/github/v/release/bve/OpenOSC?style=plastic)](https://github.com/bve/OpenOSC/releases/latest)
[![GitHub stars](https://img.shields.io/github/stars/bve/OpenOSC?style=plastic)](https://github.com/bve/OpenOSC/stargazers)

Open-source firmware for the **ZT703S** and **ZT706S** handheld oscilloscope multimeters.

OpenOSC replaces the stock application with its own oscilloscope, multimeter and signal generator
firmware. It also adds a small Recovery bootloader that updates the firmware from a USB drive.

**Documentation:** the [wiki](https://github.com/bve/OpenOSC/wiki) has a user guide for every
instrument, the SCPI reference, troubleshooting and pages for contributors.

> **Status:** work in progress. ZT703S is the main development and test device.
> **ZT706S:** I don't have one. Users run it, but many features are untested on that model.
> **Clones of the ZT703S and ZT706S** should work too: install the files for the model they are based on.
> If something does not work on a clone, please [open an issue](https://github.com/bve/OpenOSC/issues/new/choose).

> [!WARNING]
> **OpenOSC is in active development.** Use the stock firmware if you need its established operation.
>
> **I take no responsibility for bricked or damaged devices.** You install this firmware entirely
> at your own risk.

> [!NOTE]
> **You can always go back to the stock firmware.** OpenOSC never modifies the factory bootloader,
> your calibration or the factory settings. The factory bootloader still accepts official updates,
> so installing the official firmware replaces OpenOSC completely.
> See [Going back to the stock firmware](#going-back-to-the-stock-firmware).

## Screenshots

Captured on a ZT703S running OpenOSC. The Bode screens replay a recorded twin-T sweep in the current interface.

<table>
  <tr>
    <td><img src=".github/images/scope.png" width="320" alt="Oscilloscope: CH1 and CH2 square waves at about 20 kHz and 40 kHz, with frequency and peak-to-peak readouts and the record memory label"></td>
    <td><img src=".github/images/multimeter-charge.png" width="320" alt="Multimeter: DC milliamps, signed charge in mAh beside the range, and MIN/MAX/AVG statistics"></td>
  </tr>
  <tr>
    <td align="center">Oscilloscope, two channels</td>
    <td align="center">Multimeter with charge counter</td>
  </tr>
  <tr>
    <td><img src=".github/images/bode.png" width="320" alt="Bode analyzer: recorded twin-T sweep with f1 and f2 cutoffs and the f0 notch marker"></td>
    <td><img src=".github/images/multimeter-range.png" width="320" alt="Multimeter: DC voltage on the 2.5 V range with bar graph notches aligned to the 0.5 V scale"></td>
  </tr>
  <tr>
    <td align="center">Bode plot (recorded sweep replay)</td>
    <td align="center">Multimeter bar graph on the 2.5 V range</td>
  </tr>
  <tr>
    <td><img src=".github/images/scope-record-memory.png" width="320" alt="Oscilloscope: 512K record memory at 140 MS/s, with a less-than marker for capture headroom"></td>
    <td><img src=".github/images/multimeter-capacitance.png" width="320" alt="Multimeter analog capacitance view: 9.999 nF range with a nominal 10 nF dial"></td>
  </tr>
  <tr>
    <td align="center">512K record memory at 140 MS/s</td>
    <td align="center">Multimeter analog capacitance scale</td>
  </tr>
  <tr>
    <td><img src=".github/images/scope-readouts.png" width="320" alt="Oscilloscope: two readouts per channel, time cursors and the STOP status word"></td>
    <td><img src=".github/images/multimeter-large.png" width="320" alt="Multimeter: large 80 px reading under the analog dial"></td>
  </tr>
  <tr>
    <td align="center">Two readouts per channel, time cursors</td>
    <td align="center">Multimeter, large reading</td>
  </tr>
  <tr>
    <td><img src=".github/images/interleave-703.png" width="320" alt="Performance settings: Interleave CH1 on, 200 MS/s"></td>
    <td><img src=".github/images/interleave-offset.png" width="320" alt="Scope calibration: interleave offset of +9.8 codes saved for 200 MHz"></td>
  </tr>
  <tr>
    <td align="center">CH1 interleave on the ZT703S (experimental)</td>
    <td align="center">Interleave offset calibration</td>
  </tr>
  <tr>
    <td><img src=".github/images/bode-notch-table.png" width="320" alt="Bode table: the notch row is marked f0 and low CH2, with phase shown as dashes"></td>
    <td><img src=".github/images/multimeter-type-k.png" width="320" alt="Multimeter: Type K thermocouple reading 22.5 °C"></td>
  </tr>
  <tr>
    <td align="center">Bode table (recorded sweep replay)</td>
    <td align="center">Type K temperature</td>
  </tr>
  <tr>
    <td><img src=".github/images/decoder-split.png" width="320" alt="Protocol decoder, split view: SPI waveform above, decoded table below"></td>
    <td><img src=".github/images/decoder-can.png" width="320" alt="Protocol decoder: CAN frames table with IDs, data and status"></td>
  </tr>
  <tr>
    <td align="center">Protocol decoder: split view (SPI)</td>
    <td align="center">Protocol decoder: CAN table</td>
  </tr>
  <tr>
    <td><img src=".github/images/decoder-i2c.png" width="320" alt="Protocol decoder: I2C transactions table with addresses and data"></td>
    <td><img src=".github/images/decoder-canfd-detail.png" width="320" alt="Protocol decoder: detail of a CAN FD frame with its 64-byte payload"></td>
  </tr>
  <tr>
    <td align="center">Protocol decoder: I²C table</td>
    <td align="center">CAN FD frame detail</td>
  </tr>
  <tr>
    <td><img src=".github/images/decoder-uart.png" width="320" alt="Protocol decoder: UART frames table"></td>
    <td><img src=".github/images/decoder-uart-autobaud.png" width="320" alt="Decoder settings: UART baud rate detected automatically"></td>
  </tr>
  <tr>
    <td align="center">Protocol decoder: UART table</td>
    <td align="center">UART baud rate detection</td>
  </tr>
  <tr>
    <td><img src=".github/images/decoder-dmx512-detail.png" width="320" alt="DMX512 packet detail with the start code, numbered slots, BREAK and mark-after-break timing"></td>
    <td><img src=".github/images/menu-high-contrast.png" width="320" alt="Display settings with white menu text on black and a cyan selection outline"></td>
  </tr>
  <tr>
    <td align="center">DMX512 packet detail</td>
    <td align="center">High-contrast menus</td>
  </tr>
  <tr>
    <td><img src=".github/images/multimeter-analog.png" width="320" alt="Multimeter analog view: 1 kV DC range with dial markings from 0 to 1000 V"></td>
    <td><img src=".github/images/generator.png" width="320" alt="Signal generator: sine at 1 kHz with frequency, waveform preview and soft-keys at the bottom"></td>
  </tr>
  <tr>
    <td align="center">Multimeter analog view, 1 kV DC range</td>
    <td align="center">Signal generator</td>
  </tr>
  <tr>
    <td><img src=".github/images/multimeter-hold-modes.png" width="320" alt="Multimeter HOLD settings: Manual, Delayed, Stable and Auto modes"></td>
    <td><img src=".github/images/multimeter-auto-hold.png" width="320" alt="Multimeter in Auto HOLD: a stable reading held automatically"></td>
  </tr>
  <tr>
    <td align="center">Multimeter HOLD modes</td>
    <td align="center">Auto HOLD</td>
  </tr>
  <tr>
    <td><img src=".github/images/scheme-daylight.png" width="320" alt="Oscilloscope with the Daylight color scheme"></td>
    <td><img src=".github/images/scheme-amber.png" width="320" alt="Oscilloscope with the Amber color scheme"></td>
  </tr>
  <tr>
    <td align="center">Daylight color scheme</td>
    <td align="center">Amber color scheme</td>
  </tr>
  <tr>
    <td><img src=".github/images/settings-uptime.png" width="320" alt="Settings menu showing elapsed time since startup in the header"></td>
    <td><img src=".github/images/generator-layout.png" width="320" alt="Generator layout editor: choosing the position of the frequency, preview and buttons blocks"></td>
  </tr>
  <tr>
    <td align="center">Settings with uptime</td>
    <td align="center">Generator layout</td>
  </tr>
  <tr>
    <td><img src=".github/images/fft.png" width="320" alt="FFT, Blackman window, 2048 points, with frequency cursors"></td>
    <td><img src=".github/images/xy.png" width="320" alt="XY mode: two square waves in quadrature"></td>
  </tr>
  <tr>
    <td align="center">FFT with cursors</td>
    <td align="center">XY mode</td>
  </tr>
</table>

## Features

**Oscilloscope**

- Two channels, 20 mV/div to 10 V/div, timebase from 10 ns/div to 20 s/div
- Trigger modes Auto, Normal and Single, with adjustable pre/post-trigger position and Center trigger.
  Tap **F3** to select Trigger and use Left/Right to adjust pretrigger. Tap **F3** again to cycle modes.
  The Trigger menu supports Edge, Pulse width, Runt and Rise/fall time. [Trigger setup](docs/SCOPE_TRIGGERS.md)
  Hold **F3** to set the trigger level to the signal midpoint and pretrigger to 50%. Pan with **F2**
- Continuous Roll mode for slow timebases (200 ms/div and slower)
- Record memory from 64K to 512K points across enabled channels. The record overview shows the
  budget used for the displayed capture. A label such as `<512K` means the capture plan reserves part
  of that budget, for example as DMA headroom at the fastest sample rates. The prefix depends on the
  plan only, so it stays steady while the settings stay the same. **Analyze → Acquisition → Captured**
  shows the exact number of points per channel in the displayed record.
- The multimeter as a third channel beside CH1 and CH2 (**Analyze → Display → View → DMM** and
  **DMM function**), measured at the multimeter's own inputs: its line at every timebase in its own
  color (**Analyze → Colors**), with its zero tag and its reading over the plot. **F1**/**F2** select
  it after the channels; Up/Down then set its units/div and its position. Its readouts (mean, min,
  max, peak to peak, RMS) have a footer page of their own, or the panel of a channel that is off, and
  level cursors can measure it in its units
- In the default **Quick** profile, tap **AUTO** to start Auto Setup or cancel it in progress.
  Hold **AUTO** for 2 s in Y-T to open **Zoom & Pan**. Up/Down zoom the view, Left/Right pan, and
  **F4** returns to the previous card. The **Classic** profile uses a tap to reset pan and a hold to
  start Auto Setup. Choose the profile and the **F2**/**F4** hold actions in **Settings → Display → Scope shortcuts**
- Hold **F1** to choose the selected channel's probe ratio, including 10000:1 (**X10K** on the Position card).
  Hold **F2** for Home view or Reset zoom & pan. Hold **F4** to open the assigned editor.
  Returning from the Cursor settings shortcut selects the first enabled cursor for arrow-key control
- Configurable on-screen measurements, two per channel in the footer, time and level cursors
- **Settings → Display → Swap CH1/CH2** exchanges the on-screen channel labels, colors and
  measurement columns. It defaults to Off. The Channel and Trigger menus show both numbers when swapped.
  Capture, SCPI, calibration and decoder wiring use the physical input numbers.
- Hardware frequency counter with an averaged on-screen overlay. Under **Analyze → Display →
  Frequency counter**, **Timed edges** improves the resolution of slow CH1 signals and
  **Trace f (~)** can show an estimate from the waveform when the comparator counts zero edges.
  Under **Analyze → Measurements**, **Short window f/T** can use a recent hardware count when the
  measurement window is too short to resolve frequency and period. These options are off by default.
- XY, MATH and reference waveforms; sin(x)/x and spline interpolation, fading or infinite persistence, Glow
- Per-channel waveform density (**Channel → Density**) in Y-T and Y-T+FFT. Choose **Colors** to show
  rare paths in cooler colors and frequent paths in warmer colors, or **Mono** with a selectable color.
  Density defaults to **Off**. Its history follows **Analyze → Display → History → Persistence**.
  Density uses a 1 s history with Persistence Off. **Infinite** keeps every observed path, and
  **Clear history and reference** starts a new accumulation
- Grid brightness (**Analyze → Display → View → Grid brightness**, 0–100 %) and a separate trace
  brightness for CH1 and CH2 (**Channel → Trace brightness**, 10–100 %). Both dim towards the plot
  background on dark and light schemes. Trace brightness also applies to Peak Hold, Persistence, Glow,
  X-Y (CH1) and Density. Channel colors, labels and markers keep full contrast
- FFT up to 2048 points with four window functions, dBFS or linear scale
- Protocol decoders (**Analyze → Decoder**): UART (TTL), RS-232 and RS-485 with autobaud, DMX512, I²C, SPI, LIN, CAN and ISO CAN FD.
  Waveform with annotations, split or table view and event detail. **Decoder → Save CSV** exports the
  completed decode to `/openosc/DECODE`.

  Select the RX channel and use DC coupling. **UART (TTL)** defaults to high idle with a 1.65 V
  threshold and also works on the logic side of an RS-232 transceiver. **RS-232** uses negative
  idle and a separate threshold that defaults to 0 V. At that threshold, Auto hysteresis is at
  least 3 V wide (±1.5 V), which rejects noise near 0 V on an idle or unconnected input. Both modes
  share the baud rate, data bits, parity, stop bits and bit order.

  **RS-485** and **DMX512** decode the pair as CH1 − CH2 (Data+ on CH1, Data− on CH2, both DC
  coupled) against their own threshold, 0 V by default, with a Polarity choice for a pair wired
  the other way round. A receiver's logic output on one channel also works. RS-485 uses the UART
  framing settings; DMX512 checks BREAK and MAB against ANSI E1.11 and shows the START code and
  each slot. See [RS-485 and DMX512 decoding](docs/DECODER_RS485_DMX512.md) for safe probe
  connection and record requirements.
- Bode analyzer (**Analyze → Bode (FRA)**): gain and phase of a circuit against frequency with the
  built-in generator, cursors, −3 dB corner `fc` for low-pass and high-pass responses,
  cutoffs `f1` and `f2` with measured center `f0` for band-pass and notch responses,
  bandwidth, gain and phase margins, CSV export
- Zero and gain calibration of both channels, stored as files and never written to the factory calibration
- Selectable performance profiles: 200/100, 280/140 and 360/90 (core MHz / MS/s per channel),
  switched instantly without a restart
- CH1 interleave over both ADCs, up to 280 MS/s on CH1 alone. Both models offer Off, On and Auto.
  On the ZT703S, interleave is experimental, with on-device offset and gain/timing calibration.
  Its Auto mode follows the MKR rule: CH1 alone at 10 or 25 ns/div on the 200 and 280 MHz profiles,
  except in Roll or while the generator streams from the DAC. On interleaves CH1 alone at every timebase
  and profile. The ZT703S defaults to Off and the ZT706S to Auto (the stock rule)
- Save waveforms to `/openosc/CAPTURES` from **Analyze** with **Save waveform CSV.gz** or
  **Save waveform RAW.gz**. Both use streaming gzip compression. Decompress the file on a
  computer to recover the CSV or RAW data. OpenOSC checks free space as it writes compressed
  bytes and removes the temporary file if the save runs out of space.
- **Settings → Files & USB → Waveforms** lists the saved waveforms with their sizes. F3 deletes
  the selected file after a confirmation. **Clear waveforms** deletes every waveform after a
  confirmation, and F4 stops it part way. Both cover files in `/openosc/CAPTURES` ending in
  `.CSV`, `.RAW`, `.CSV.GZ` or `.RAW.GZ` in any letter case, including plain exports from
  earlier firmware. Finish any waveform saves or USB downloads before deleting waveforms.

**Multimeter**

- DC/AC voltage, millivolts, DC/AC current and milliamps, resistance, continuity, diode, capacitance
- Type K thermocouple temperature in °C or °F. In **Measurement → Junction source**, choose **Manual** (25 °C by default), **MCU at startup**, or **MCU live**. The startup option holds the die temperature captured when the firmware launches. The live option updates it about once a second. As the instrument warms up, die temperature can diverge from the temperature at the thermocouple terminals.
- Auto and manual ranges (the AUTO/RANGE key works as in the stock firmware) and REL
- HOLD modes (**MENU → Measurement → Hold**): Manual, Delayed (after a countdown), Stable (waits
  for a stable reading) and Auto (holds every new stable reading)
- Statistics, trend chart and CSV logging
- Resistance and capacitance use Ω, kΩ, MΩ and pF, nF, µF on the display, including for REL,
  MEAN and MIN/MAX/AVG. The displayed precision follows the meter resolution. For example,
  0.6700 MΩ appears as 670.0 kΩ, 0.090 nF as 90 pF, and 22.00 mF as 22000 µF. **Range** shows
  the meter's range limit in its own unit, so a 90 pF reading on the 9.999 nF range shows
  `Range 9.999 nF` with the reading in pF.
- The unit stands to the right of the reading on its baseline. MIN, MAX and AVG omit the unit when
  all three use the unit shown beside the reading. OL appears centred without a unit.
- Session logging without a PC (**MENU → Measurement → Session log**): interval rows (count, min, max
  and average of the readings in each interval) or event records (a new record whenever the reading
  leaves a percentage band), stopped manually, at a set duration or when you leave the multimeter.
  Saved sessions open on the device as a trend with a cursor, zoom and pan, and stay CSV files in
  `/openosc/LOGS`. See [DMM session logging](docs/DMM_SESSION_LOGGING.md)
- Ampere-hour counter in DC current (A and mA): the signed charge since the function was selected,
  shown as mAh or Ah beside the range. It counts while the readings are valid and arrive at most 2 s
  apart (an overload, the meter's own HOLD or REL, a lost or corrupted reading, or a longer gap pauses
  it, with nothing estimated for the missing time). Manual HOLD and REL change only the display. A
  confirmed switch to another function or entering the multimeter screen resets it; a range change or
  a repeated selection of the same function keeps it.
- Large 72/80 px reading when the bar graph and statistics are off
- Bar graph labels follow the active range in measurement units. The 2.5 V range runs from 0 to 2.5 V
  in 0.5 V steps. Each notch extends below the bar and lines up with its scale value. The analog dial
  labels its scale in the reading's unit. On both the bar and the dial, ranges that end below the
  meter's counts, such as 1000 V DC, 750 V AC and 10 A, reach full scale at the end of the range.
  **MENU → Bar scale unit** shows or hides the unit after the bar's last number. It is on by default.

**Signal generator**

- Six waveforms: sine, square, triangle, half sine, positive sine and sawtooth
- Digit-by-digit frequency entry; duty cycle for square
- Keeps running in the background while you use the oscilloscope or the multimeter
- Output is OFF after power-on by default, or restores its last state
  (**Settings → Device → Power → Generator at startup**)
- Rearrangeable screen (**Settings → Display → Generator layout**)
- ZT703S: square output as in the stock firmware (DAC below 1 kHz, PWM from 1 kHz) or PWM
  always; Reference 1 kHz preset
- [Arbitrary waveform playback](docs/AWG.md) from CSV or binary SCPI/VISA, with 2 to 1024 points on
  the ZT703S and 20 to 255 points on the ZT706S. Test coverage for ZT706S is limited to host checks.
- ZT706S: its external generator module, up to 10 MHz

**USB and remote control**

- USBTMC and CDC (virtual COM port) with SCPI commands, working alongside the local UI
- [SCPI extensions for desktop clients](docs/SCPI_DESKTOP.md) for frame, peak, measurement and file transfers
- Compatible command profiles for Rigol DS oscilloscopes and the OWON XDM1041 multimeter, so
  existing PC software such as ngscopeclient can talk to the device
- [Fyfar](https://github.com/Fyfar) built a web app for the oscilloscope, multimeter, signal generator
  and Bode analyzer.
  It connects over USB via Web Serial. [OpenOSC Panel](https://github.com/Fyfar/openosc-panel)
- [Flavio Morsoletto (fletto2)](https://github.com/fletto2) built a desktop app for Windows, macOS and
  Linux with a live oscilloscope view and multimeter and generator panels. It uses OpenOSC's
  desktop SCPI extensions over USB. [zoyi_gui](https://github.com/fletto2/zoyi_gui)
- `SYSTem:KEY <name>[,<ms>]` presses a panel key through the real input path: `F1`..`F4`, `MENU`, `UP`,
  `DOWN`, `LEFT`, `RIGHT`, `MODE`, `HOLD`, `AUTO`, `REL`, held for `<ms>` (default 100, 30 to 10000);
  `*OPC?` answers when the key is released and its press has been handled. The long press (1 s; 2 s
  for `AUTO` and `REL`) counts from the debounced press, about 26 ms in, so `F1,1000` is still a short
  press. `AUTO` on the multimeter screen acts from 52 ms held. As on the panel, `REL` held longer than
  **Settings → Device → Power → Power-off hold**, counted from the start of the press, powers the device
  off, and `MODE` switching the instrument mode reconnects USB, so the reply is lost
- USB Remote mode: the PC controls the device while the screen keeps showing live data
- USB data works only in the 200/100 performance profile. Connect directly to a PC port:
  disconnects are known, especially through USB hubs.
- **Settings → Files & USB → USB controller** offers Enabled (default), Disabled and Auto.
  Auto tries at startup or when the USB power signal rises. Without a computer, it turns the
  controller off after 5 s on the bus. An attach that needs to pause acquisition can wait up to
  30 s. While the power signal stays high, Auto retries after 30 s, 60 s and then every 2 min.
  Choosing Auto with the controller already off starts that schedule at 30 s. A detected
  computer keeps the controller on while the power signal is high, including during suspend.
  The controller turns off when both the power signal and host have been absent for 5 s.
  USB Remote is available while the controller is attached and keeps it on during the session.
- Firmware released before USB Auto treats `"controller": "auto"` as invalid and loads defaults
  or an earlier settings copy. To keep settings when downgrading, select Enabled or Disabled
  and save with a normal restart before installing the older firmware.

**Interface and files**

- LVGL-based user interface (English only)
- The Settings header shows the time since the firmware started (hours, minutes and seconds)
- Rearrangeable screens: on the oscilloscope, choose the order of the waveform, the measurements
  and the soft-keys (**Analyze → Layout**); on the multimeter, the order of the reading, the
  statistics/bar/trend, the dial and the soft-keys (**MENU → Layout**). Soft-keys are at the
  bottom by default, in the same order as the physical F1–F4 keys
- The labels above F1–F4 always show what each key does right now; in menus they stay at the
  bottom, right above the keys
- **Settings → Display → Left key in menus** defaults to Back. Step selects the previous choice
  or switches a toggle off. In an open choice list, Left and Right move one item without wrapping.
  F4 goes back or cancels an edit.
- **Settings → Display → Menu contrast** defaults to Theme. High contrast gives menus black
  backgrounds, white text and a distinct selection highlight across all instruments.
- Oscilloscope color schemes (**Analyze → Colors**): Current, High contrast, Daylight and Amber,
  plus custom RGB colors for individual elements. Multimeter looks: Custom and Hybrid
- Lossless PNG screenshots with a built-in viewer that also opens earlier BMP screenshots
- Settings and the color theme are stored as JSON files on the external SPI Flash and saved when
  you switch the device off, restart it or enter Recovery. The microcontroller's internal Flash
  holds only the firmware. You can edit the files from a computer; a damaged settings file is
  restored from its previous version or from a copy in `/openosc/SETCOPY/`.
- Hold-to-power-off with a progress bar
- **Settings → Device → Power → Wake on measurement** offers Off (default), DMM, Scope and Both.
  A qualifying event restores the backlight and restarts the idle power-off timer. The DMM
  responds to a change of at least 10% of full scale within the same range. A range change sets a
  new reference, and dedicated millivolt modes are excluded. The scope responds to a new live
  record with a genuine trigger in Auto, Normal or Single.
- Hardware watchdog in both the firmware and Recovery: a hang or a crash restarts the device

**Recovery bootloader**

- Shows the device storage as a USB drive and installs firmware from `/openosc/UPDATE/`
- Accepts `.OSC` packages and raw `.bin` images; verifies the board model and CRC before erasing
- Shows the factory calibration data and its format (long or compact) and can save a copy of the
  factory Flash block
- Checks and repairs the FAT file system, and can format the storage drive
- Opens automatically if the main firmware is missing or invalid

## How it works

The device's own factory bootloader occupies the first 128 KiB of internal Flash. OpenOSC never
modifies it. OpenOSC is installed on top of it in two parts:

| Part | Address | Purpose |
| --- | --- | --- |
| Factory bootloader | `0x08000000` | Untouched, together with the calibration and factory settings stored in the same block. Accepts the stock `.ZTK` update containers. |
| OpenOSC Recovery | `0x08020000` | Second-stage bootloader (128 KiB). Exposes a USB drive and installs the main firmware. |
| OpenOSC firmware | `0x08040400` | The main application. |

## Installation

Installation takes two steps. First, the Recovery bootloader is installed through the device's
standard firmware update mechanism. Then Recovery installs the OpenOSC firmware.
The same steps, with more detail, are in the wiki: [Installation](https://github.com/bve/OpenOSC/wiki/Installation)
and [Updating](https://github.com/bve/OpenOSC/wiki/Updating).

### Step 1: install the Recovery bootloader (standard update)

Recovery is packed as a regular factory update container (`.ZTK`):

- ZT703S: `OpenOSC-Recovery-ZT703S.ZTK`
- ZT706S: `OpenOSC-Recovery-ZT706S.ZTK`

The bootloader reads the target model from the file header. On most units the file name can be
changed, as long as it keeps the upper-case `.ZTK` extension.

> [!IMPORTANT]
> **If Recovery does not install** (the file disappears from the drive, but the stock application
> still starts), rename the file to `ZOYI-703S-FW-V143.ZTK` and copy it again. Some ZT703S units
> (Boot v1.0.15) install only files with this kind of name
> ([#16](https://github.com/bve/OpenOSC/issues/16)). Units with later boot versions install it too,
> so you can also use this name from the start if you do not know your unit's boot version.
>
> If a large `.ZTK` does not fit on the drive ("not enough free space"), free up the device's storage
> or format it from the stock application: **Scope → MENU → Apps → Image Viewer → F1 → Format (F3)**.

Install it the same way as an official firmware update:

1. Put the device into the factory USB update mode: switch it off, hold **F1** and switch it on.
   Release **F1** when the screen shows **USB: Disk Driver**. These keys were checked on the ZT703S.
2. Connect the device to a computer. A USB drive appears (on the ZT703S it is named `FLASH`).
3. Copy the `.ZTK` file for your model into the `firmware` folder on that drive.
4. Wait until the bootloader finishes. It installs the file and deletes it from the drive.
5. The device restarts into OpenOSC Recovery; restart it manually if it does not.
   The stock application has been replaced, so Recovery opens its menu.

### Optional: back up the factory block

**This step is optional.** You can skip it and go straight to step 2.

OpenOSC never writes to the factory block. The only Flash write code in OpenOSC accepts addresses
from `0x08040000` upward, the firmware area, so neither Recovery nor the firmware can erase or
change the factory block, even by mistake. Installing, updating and running OpenOSC leave it
exactly as it was.

The backup is a precaution for rare events outside OpenOSC, such as a hardware fault. If you want
one:

1. In Recovery, select **BACKUP FACTORY BOOT** and confirm it with **F3**.
2. Recovery copies the whole factory block (`0x08000000`–`0x0801FFFF`, 128 KiB) to the USB drive
   and verifies the copy:
   - `/openosc/BACKUP/FACTORY_BOOT_08000000.bin`: the backup itself;
   - `/openosc/BACKUP/FACTORY_BOOT_08000000.txt`: its address, size and CRC32.
3. Press **MENU** to return to the Recovery menu. The USB drive appears on the computer again
   in the menu.
4. Copy both files from the `openosc/BACKUP/` folder on the drive to your computer and keep them
   somewhere safe.

The block contains the factory bootloader and **this unit's own calibration and settings**.
Keep a separate backup for each device and never write one device's backup to another.
Writing a backup back requires an SWD programmer.

### Step 2: install the OpenOSC firmware (through Recovery)

1. Start Recovery. It opens automatically when no valid firmware is installed.
   Later, you can open it by holding **F2** while the device starts.
2. Connect the device to a computer. Recovery shows the device's storage as a USB drive.
3. Copy the firmware file for your model into `/openosc/UPDATE/`: `OpenOSC-Firmware-ZT703S.OSC` or
   `OpenOSC-Firmware-ZT706S.OSC` (from the release or your own build).
4. Finish copying. Optionally, eject the drive on the computer.
5. On the device, select **UPDATE FIRMWARE**, choose the file and confirm it with **F3**.
6. When the update completes, Recovery offers to start the application with **CONFIRM** selected:
   press **MENU** or **F3**.

Recovery controls: **MENU** = OK, **F4** = back/cancel, arrow keys = navigation.
In a confirmation, **F3** confirms and **F2** cancels at once. It starts on **CANCEL** (except the
offer to start after an update): left/right select **CONFIRM**, **MENU** presses the selected one.
If the computer still has the USB drive mounted, Recovery first asks **DISCONNECT USB DISK?**.
Confirm it and Recovery detaches the drive itself; the cable can stay connected. The drive
reconnects when you return to the Recovery menu.

Recovery checks the board model, CRC and vector table of the image before erasing anything.
An image for another model is rejected. Later updates of the main firmware use step 2 only.

### Going back to the stock firmware

You can return to the stock firmware at any time. OpenOSC only replaces the application area. The
factory bootloader is never modified, and it starts before OpenOSC every time the device is
switched on.

1. Get the official firmware update (`.ZTK`) for your model from the manufacturer. It is a good idea
   to download it before you install OpenOSC.
2. Install it exactly like [step 1](#step-1-install-the-recovery-bootloader-standard-update):
   put the device into the factory USB update mode (hold **F1** while switching it on) and copy the
   file into the `firmware` folder.
3. The factory bootloader replaces OpenOSC Recovery and the OpenOSC firmware with the stock
   application.

The calibration and the factory settings are stored in the factory block, which OpenOSC never
writes, so the stock firmware uses them as before. OpenOSC's own files in the `/openosc/` folder
on the storage drive are not needed by the stock firmware and can be deleted.

The [factory block backup](#optional-back-up-the-factory-block) is not needed for this. It is a
safety copy for the unlikely case that the block itself is ever damaged.

## Building

Requirements: [PlatformIO](https://platformio.org/). The ST platform and the ARM toolchain are
pinned in `platformio.ini` and installed automatically.

```sh
pio run
```

This builds both devices. Output files:

| Device | Recovery (step 1) | Firmware (step 2) |
| --- | --- | --- |
| ZT703S | `.pio/build/recovery/OpenOSC-Recovery-ZT703S.ZTK` | `.pio/build/firmware/OpenOSC-Firmware-ZT703S.OSC` |
| ZT706S | `.pio/build/zt706s_recovery/OpenOSC-Recovery-ZT706S.ZTK` | `.pio/build/zt706s_firmware/OpenOSC-Firmware-ZT706S.OSC` |

Each build folder also contains `firmware.bin`. For the firmware environments, this `.bin`
has a board marker and Recovery accepts it as well.
To build one device only, run, for example, `pio run -e recovery -e firmware` for ZT703S.

The firmware version is taken from [`VERSION`](VERSION).

## Third-party components

All dependencies are included in [`vendor/`](vendor):

- STM32CubeH7: CMSIS, HAL and USB Device library (ST)
- CMSIS-DSP (Arm, Apache-2.0)
- zlib (zlib License)
- FatFs (ChaN)
- LVGL 9.6.0 (MIT)
- ArduinoJson 6.21.5 (MIT)
- IBM Plex Sans and Noto Sans Mono fonts (SIL OFL 1.1)

Each component keeps its own license file.

## Contributing

Bug reports, ZT706S test results and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md)
and the [Code of Conduct](CODE_OF_CONDUCT.md). Questions go to
[Discussions](https://github.com/bve/OpenOSC/discussions); security problems are reported as described
in [SECURITY.md](SECURITY.md).

For the code base, see the contributor pages in the wiki:
[Building](https://github.com/bve/OpenOSC/wiki/Building),
[Architecture Overview](https://github.com/bve/OpenOSC/wiki/Architecture-Overview),
[Source Layout](https://github.com/bve/OpenOSC/wiki/Source-Layout) and
[Memory and Flash Layout](https://github.com/bve/OpenOSC/wiki/Memory-and-Flash-Layout).

## License

OpenOSC is licensed under the [GNU General Public License v3.0](LICENSE).

All product names and trademarks belong to their respective owners. This project is not
affiliated with or endorsed by the device manufacturer.

## Support the project

If OpenOSC is useful to you, you can support its development:

- [Ko-fi](https://ko-fi.com/val23)
- [Liberapay](https://liberapay.com/Val23)

## Releases

Releases come out often, and you do not need to install every one: see
[Why are there so many releases?](https://github.com/bve/OpenOSC/wiki/FAQ) in the FAQ.

## Retro gaming

If you'd like to turn your scopemeter into a retro gaming handheld, check out my
just-for-fun dual-boot builds at [OpenOSC-RetroGo](https://github.com/bve/OpenOSC-RetroGo).

## Other devices

OpenOSC currently targets the ZT703S and ZT706S, and I can port it to other devices. A port needs
the device on my bench: its board has to be traced, its peripherals identified and every function
tested on real hardware. If you'd like OpenOSC on another oscilloscope or multimeter, I'd be
grateful if you could send me a unit to study. Please get in touch in
[Discussions](https://github.com/bve/OpenOSC/discussions) first, so we can check the device and
agree on shipping.
