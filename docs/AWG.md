# Arbitrary waveform generator

The ZT703S and the ZT706S can play a repeating waveform loaded from a CSV file or over SCPI/VISA. Each point is a code from 0 to 4095, on both models. The codes set the output level. Level presets, the ZT706S amplitude setting and square-wave duty control apply to the built-in waveforms.

> [!WARNING]
> OpenOSC is in active development. The ZT706S arbitrary waveform output has not been run on hardware yet. Its frames follow the stock firmware's protocol, and its sample rates and levels are nominal.

| | ZT703S | ZT706S |
| --- | --- | --- |
| Output | DAC, one code per update | External generator module, one byte per sample |
| Points | 2 to 1024 | 20 to 255 |
| Codes | 0 to 4095, used directly | 0 to 4095, each sent as the nearest byte 0 to 255 |
| Sample rate | 1 S/s to 6.25 MS/s at 200 MHz, 10 MS/s at 280 or 360 MHz | 1 S/s to 200 MS/s, nominal |
| Applied rate | The DAC timer's nearest rate | 200 MHz ÷ divisor, nominal |

The sample rate sets how quickly the output advances through the table. Divide the applied rate by the number of points to get the waveform's repetition frequency. The generator screen shows the applied rate and the repetition frequency.

## Play a CSV file on the instrument

1. Open **Generator → Wave (F1) → AWG file** once to create `/openosc/AWG`.
2. Copy a `.csv` file into that folder using the Recovery USB disk. Eject the disk and start the application.
3. Select **Arbitrary** in the **Wave** menu. Open **AWG file**, select the file and press **MENU** to load it.
4. Set **Sample rate (F2)** and press **Output (F4)** on the generator screen to start playback.

Use one decimal code per line. The first non-comment line may be a column heading. Blank lines and lines starting with `#` are allowed. Both LF and CRLF line endings work.

```csv
code
500
1000
1500
2000
```

On the ZT703S, this four-point table repeats at 10 Hz at 40 S/s. The ZT706S needs at least 20 codes per file. The loaded table stays in RAM until replaced or the instrument restarts. Load the file again after a restart to use it.

## Load a waveform over SCPI/VISA

`SOUR:ARB:DATA` accepts an IEEE 488.2 definite-length block of little-endian signed 16-bit integers. The block header declares the payload length in bytes. Each integer must be a code from 0 to 4095. End the SCPI message with LF or use USBTMC EOM. An interrupted transfer or an invalid code leaves the previous table in place.

This example loads a 20-point ramp and plays it at 1 kHz on either model.

```python
import pyvisa

codes = [round(i * 4095 / 19) for i in range(20)]
manager = pyvisa.ResourceManager()
with manager.open_resource("USB0::...::INSTR") as instrument:
    instrument.write_termination = "\n"
    instrument.read_termination = "\n"
    instrument.timeout = 10000
    instrument.write("OUTP OFF")
    instrument.write_binary_values("SOUR:ARB:DATA ", codes, datatype="h", is_big_endian=False)
    instrument.write("SOUR:FUNC ARB")
    instrument.write("SOUR:ARB:SRAT 20000")
    instrument.write("OUTP ON")
    print(instrument.query("*OPC?"), instrument.query("SYST:ERR?"))
manager.close()
```

Replace `USB0::...::INSTR` with the instrument's VISA resource name. After checking the waveform, send `OUTP OFF` to stop it.

| Command | Purpose |
| --- | --- |
| `SOUR:FUNC ARB` | Select the loaded waveform. |
| `SOUR:ARB:DATA?` | Return the table as a binary block, with the codes as loaded. An empty table returns `#10`. |
| `SOUR:ARB:POIN?` | Return the loaded point count. |
| `SOUR:ARB:SOUR?` | Return `NONE`, `FILE` or `REM`. |
| `SOUR:ARB:SRAT <S/s>` or `SOUR:ARB:SRAT?` | Set or query the requested sample rate. |
| `SOUR:FREQ <Hz>` or `SOUR:FREQ?` | Set or query the repetition frequency when AWG is selected. |
| `OUTP ON` or `OUTP OFF` | Start or stop the output. |

On the ZT703S, the generator uses two existing DAC buffers. If a built-in DAC waveform is playing and an arbitrary waveform is already loaded, stop the output or select AWG before replacing the loaded table.

## ZT706S generator module

The ZT706S sends the waveform to its generator module as the 512-byte frame the stock firmware uses for the built-in waveforms. The frame holds every point of the table, a divisor and a checksum. The module holds each sample for `divisor` ticks of a clock the stock firmware takes as 200 MHz.

- **Levels.** The module receives `round(code × 255 / 4095)`. Codes 0, 2048 and 4095 become bytes 0, 128 and 255. `SOUR:ARB:DATA?` returns the original codes, which the preview also uses. **Amplitude** applies to the built-in waveforms. The output voltage for each byte has not been measured.
- **Rate.** The instrument picks the divisor whose rate, 200 MHz ÷ divisor, is nearest the requested rate. When two divisors are equally near, it takes the higher rate. Divisor 1 gives the highest nominal rate, 200 MS/s. The stock firmware uses this divisor for its 20-point sine at 10 MHz. A request for 150 MS/s selects 200 MS/s, and a request for 120 MS/s selects 100 MS/s. The screen shows the resulting rate. All ZT706S rates and frequencies are nominal pending hardware tests.
- **Confirmation.** A new table or rate while the output is on sends a new frame. The screen shows **SENDING** until the module returns the frame's checksum byte. SCPI commands that send a frame, including `SOUR:ARB:DATA`, `SOUR:ARB:SRAT`, `SOUR:FREQ` in AWG mode and `OUTP ON`, complete with that answer. `*OPC?` waits for it. If the module does not answer, the command reports `-240` and the instrument retries as it does for the built-in waveforms.
- **Off.** **Output OFF** sends the module's zero waveform, as for every other waveform. The loaded table stays in RAM.

## Limits and errors

| Item | Limit |
| --- | --- |
| Points | ZT703S: 2 to 1024, in one 2 KiB DAC buffer in SRAM4. ZT706S: 20 to 255, in SRAM4. The table plays at its exact length. |
| Codes | Whole numbers from 0 to 4095. |
| Sample rate | Whole S/s from 1 S/s. ZT703S: up to 6.25 MS/s at 200 MHz and 10 MS/s at 280 or 360 MHz. ZT706S: up to 200 MS/s, nominal. |
| Frequency | Applied sample rate ÷ points. For example, 1024 points at 1.024 MS/s repeat at 1 kHz on the ZT703S, and 20 points at 200 MS/s repeat at 10 MHz on the ZT706S. |
| CSV file | `/openosc/AWG/*.csv`, up to 64 KiB, lines up to 64 characters. |

The instrument checks the whole file or block before it replaces the loaded table. If that check fails, the previous table and output stay unchanged. The generator screen shows the line number for errors in individual CSV rows.

- A NUL byte, an unsupported control character, a byte above 0x7E, or a CR without LF causes a file error. A UTF-8 byte order mark is allowed at the start.
- Only a single column of whole numbers from 0 to 4095 is accepted.
- The file must contain the model's number of points and fit the line and file size limits.

Read `SOUR:ARB:DATA` errors with `SYST:ERR?`.

| Error | Cause |
| --- | --- |
| `-223` | The block length is odd, or it holds fewer or more codes than the model allows. |
| `-222` | A code is below 0 or above 4095, or the sample rate is outside the model's range. |
| `-161` | The block has `#0` (indefinite length), a malformed length, extra bytes before the terminator, or ends early. A reset, USBTMC abort or 3 s without input also ends it. |
| `-168` | A block was sent to another command. |
| `-200` | The other USB session or the CSV loader is replacing the table. |
| `-221` | A Bode sweep or zero calibration owns the generator, or the new table cannot be played while the output is on. On the ZT703S, also when no DAC buffer is free or the table cannot play at the current sample rate. |
| `-240` | ZT706S: the generator module did not confirm the frame. |
| `-104` | `SOUR:ARB:DATA` received an argument other than a block. |

All block bytes are data, including LF, CR and NUL. After `#0` or a malformed length, the instrument cannot find the end of the data, so it ignores input until the USBTMC end of message. On the serial port, it ignores input until 3 s pass without input. `SOUR:FREQ` with AWG selected needs a loaded table and returns `-221` without one. A block waits for the commands before it in the same message, so `OUTP OFF;:SOUR:ARB:DATA #...` stops the output before it loads the table.

## Lifetime and startup

The loaded table is kept in RAM only. Selecting another waveform keeps the table, and selecting AWG again plays it. The other waveforms also keep their frequency, level, amplitude and duty settings while AWG is selected, because AWG uses its own sample rate setting.

After a restart or power cycle, the table is empty. The instrument restores the saved waveform selection and sample rate. In AWG mode, the output stays off even when **Settings → Device → Power → Generator at startup** is set to restore the last state. Load the file or block again, then turn the output on.
