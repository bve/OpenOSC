# Arbitrary waveform generator

The ZT703S can play a repeating waveform of 2 to 1024 DAC codes. Each code is an integer from 0 to 4095. The DAC uses the codes directly. Level presets and square-wave duty control apply to the built-in waveforms.

The sample rate sets how quickly the DAC advances through the table. Divide that rate by the number of points to get the waveform's repetition frequency. The minimum is 1 S/s. The limits are 6.25 MS/s at 200 MHz and 10 MS/s at 280 or 360 MHz.

## Play a CSV file on the instrument

1. Open **Generator → Wave (F1) → AWG file** once to create `/openosc/AWG`.
2. Copy a `.csv` file into that folder using the Recovery USB disk. Eject the disk and start the application.
3. Select **Arbitrary** in the **Wave** menu. Open **AWG file**, select the file and press **MENU** to load it.
4. Set **Sample rate (F2)** and press **Output (F4)** on the generator screen to start playback.

Use one decimal DAC code per line. The first non-comment line may be a column heading. Blank lines and lines starting with `#` are allowed. Both LF and CRLF line endings work.

```csv
code
500
1000
1500
2000
```

At 40 S/s, the four-point table repeats at 10 Hz. The loaded table stays in RAM until replaced or the instrument restarts. Load the file again after a restart to use it.

## Load a waveform over SCPI/VISA

`SOUR:ARB:DATA` accepts an IEEE 488.2 definite-length block of little-endian signed 16-bit integers. The block header declares the payload length in bytes. Each integer must be a valid DAC code. End the SCPI message with LF or use USBTMC EOM. An interrupted transfer or invalid code leaves the previous table in place.

```python
import pyvisa

manager = pyvisa.ResourceManager()
with manager.open_resource("USB0::...::INSTR") as instrument:
    instrument.write_termination = "\n"
    instrument.read_termination = "\n"
    instrument.write("OUTP OFF")
    instrument.write_binary_values(
        "SOUR:ARB:DATA ", [500, 1000, 1500, 2000], datatype="h", is_big_endian=False
    )
    instrument.write("SOUR:FUNC ARB")
    instrument.write("SOUR:ARB:SRAT 4000")
    instrument.write("OUTP ON")
manager.close()
```

Replace `USB0::...::INSTR` with the instrument's VISA resource name. After checking the waveform, send `OUTP OFF` to stop it.

| Command | Purpose |
| --- | --- |
| `SOUR:FUNC ARB` | Select the loaded waveform. |
| `SOUR:ARB:DATA?` | Return the table as a binary block. An empty table returns `#10`. |
| `SOUR:ARB:POIN?` | Return the loaded point count. |
| `SOUR:ARB:SOUR?` | Return `NONE`, `FILE` or `REM`. |
| `SOUR:ARB:SRAT <S/s>` or `SOUR:ARB:SRAT?` | Set or query the sample rate. |
| `SOUR:FREQ <Hz>` or `SOUR:FREQ?` | Set or query the repetition frequency when AWG is selected. |
| `OUTP ON` or `OUTP OFF` | Start or stop the output. |

The generator uses two existing DAC buffers. If a built-in DAC waveform is playing and an arbitrary waveform is already loaded, stop the output or select AWG before replacing the loaded table. The ZT706S continues to use the waveforms offered by its generator module.

## Limits and errors

| Item | Limit |
| --- | --- |
| Points | 2 to 1024. One table uses one 2 KiB DAC buffer in SRAM4. |
| Codes | Unscaled DAC codes from 0 to 4095. The output level comes only from the codes. |
| Sample rate | Whole S/s from 1 S/s. The maximum is 6.25 MS/s at 200 MHz and 10 MS/s at 280 or 360 MHz. |
| Frequency | Sample rate ÷ points. For example, 1024 points at 1.024 MS/s repeat at 1 kHz. |
| CSV file | `/openosc/AWG/*.csv`, up to 64 KiB, lines up to 64 characters. |

The instrument checks the whole file or block before it replaces the loaded table. If that check fails, the previous table and output stay unchanged. The generator screen shows the line number for errors in individual CSV rows.

- A NUL byte, an unsupported control character, a byte above 0x7E, or a CR without LF causes a file error. A UTF-8 byte order mark is allowed at the start.
- Only a single column of whole numbers from 0 to 4095 is accepted.
- The file must contain 2 to 1024 codes and fit the line and file size limits.

Read `SOUR:ARB:DATA` errors with `SYST:ERR?`.

| Error | Cause |
| --- | --- |
| `-223` | The block length is odd, or it contains fewer than 2 or more than 1024 codes. |
| `-222` | A code is below 0 or above 4095. |
| `-161` | The block has `#0` (indefinite length), a malformed length, extra bytes before the terminator, or ends early. A reset, USBTMC abort or 3 s without input also ends it. |
| `-168` | A block was sent to another command. |
| `-200` | The other USB session or the CSV loader is replacing the table. |
| `-221` | No free buffer is available, a Bode sweep or zero calibration owns the generator, or the new table cannot play at the current sample rate while the output is on. |
| `-104` | `SOUR:ARB:DATA` received an argument other than a block. |

All block bytes are data, including LF, CR and NUL. After `#0` or a malformed length, the instrument cannot find the end of the data, so it ignores input until the USBTMC end of message. On the serial port, it ignores input until 3 s pass without input. `SOUR:FREQ` with AWG selected needs a loaded table and returns `-221` without one. A block waits for the commands before it in the same message, so `OUTP OFF;:SOUR:ARB:DATA #...` stops the output before it loads the table.

## Lifetime and startup

The loaded table is kept in RAM only. Selecting another waveform keeps the table, and selecting AWG again plays it. The other waveforms also keep their frequency, level and duty settings while AWG is selected, because AWG uses its own sample rate setting.

After a restart or power cycle, the table is empty. The instrument restores the saved waveform selection and sample rate. In AWG mode, the output stays off even when **Settings → Device → Power → Generator at startup** is set to restore the last state. Load the file or block again, then turn the output on.
