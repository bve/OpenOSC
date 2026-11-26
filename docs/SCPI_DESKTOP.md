# SCPI extensions for desktop clients

These commands work over USBTMC and the virtual COM port. The existing
`WAV:PRE?` and `WAV:DATA?` commands retain their per-channel interface.

## Waveform transfer

| Command | Result |
| --- | --- |
| `WAV:FRAM?` | Fetch the enabled, calibrated channels from one acquisition. Uses the session's `WAV:STAR`, `WAV:STOP`, `WAV:POIN` and `WAV:POIN:MODE` settings. |
| `WAV:POIN:MODE STR` | Send every stride-th sample when `WAV:POIN` is nonzero. This is the default. |
| `WAV:POIN:MODE PEAK` | Send the minimum and maximum of each bucket when `WAV:POIN` is nonzero. The bucket size is the stride used by `STR`. A narrow pulse within a bucket contributes to its extrema. |
| `WAV:ENV? [buckets]` | Fetch min/max pairs for the entire acquisition. The default is 1500 buckets. The valid range is 100 to 524288. Leaves the session's waveform settings unchanged. |

`WAV:FRAM?` and `WAV:ENV?` return an IEEE 488.2 definite-length block
with a nine-digit size field. The payload begins with an ASCII line
`sequence,channel_count`. The next line for each included channel is
`CHANn,<preamble>`, where the preamble describes that channel's payload.
All these lines end in LF. The channel data follows in the same order as
the preambles. A final LF terminates the block and is outside its payload.

Preamble type 0 sends one unsigned byte per point. Type 1 sends two bytes
per bucket, minimum followed by maximum. The preamble's point count is
the number of buckets in type 1. The bytes use the same vertical scaling
and offset as the existing `WAV:DATA?` byte format. The last bucket may
contain fewer samples.

In RUN, each frame or envelope query waits for a fresh acquisition.
Set the host read timeout above one second plus the configured record
duration, up to 201 seconds on the slowest acquisition. If no new record
arrives in that time, the reply is `NAN` and the error queue reports `-230`.
In HOLD or after a completed Single, both queries return the held record.

## Measurements

`WAV:MEAS? SCR` computes measurements for the sample window selected by
`WAV:STAR` and `WAV:STOP`, using the scope's screen-resolution algorithm.
This is also the default for `WAV:MEAS?`. Set both indices to match the
desired view. `WAV:MEAS? REC` computes across the entire record. The
selected channel is `WAV:SOUR`. Both modes use the session's leased frame.
Use `WAV:SNAP` before a standalone measurement query to request a new
acquisition in RUN.

The reply contains these comma-separated fields in order.

```text
frequency_Hz,peak_to_peak_V,rms_V,period_s,mean_V,min_V,max_V,ac_rms_V,duty_pct,
rising_edges,reserved,counter_Hz,amplitude_V,top_V,bottom_V,clipped,frequency_source
```

Unavailable numeric values, including the reserved field, are `NAN`.
`clipped` is 0 or 1. `frequency_source` is `NONE`, `EDGE`, `SPEC` or `CNT`.
`counter_Hz` is the latest settled hardware counter value.

## Multimeter temperature

| Command | Units and choices |
| --- | --- |
| `SYST:DMM:TEMP:TYPE[?]` | Thermocouple type: `B`, `E`, `J`, `K`, `N`, `R`, `S` or `T` |
| `SYST:DMM:TEMP:CJUN[?]` | Manual cold junction in °C, -50 to 100 in steps of 0.1, or `NONE` |
| `SYST:DMM:TEMP:UNIT[?]` | `C` or `F` on the screen; readings over SCPI are always in °C |

These set the same values as **Measurement → Thermocouple type**, **Manual
junction** and **Temp units** in the multimeter menu; a `CJUN` value or
`NONE` also sets **Junction source** to Manual. The type selects the curve
the temperature reading uses, so a client reading temperature should query
it. `CJUN` accepts the whole -50 to 100 range for every type. A junction
outside the window of the selected type (type K -270 to 1372 °C, so any
`CJUN` value; type B 0 to 100 °C; the others -40 to 100 °C) gives no
temperature until it is back inside: `MEAS?`, `READ?` and `FETC?` reply `NAN`
and the error queue reports `-230`. Without a manual junction, `CJUN?` replies `NAN`
and the error queue reports `-230`.

## Files

`MMEM:CAT? "FOLDER"[,start]` returns `total,count` followed by `count`
pairs of `"filename",size_in_bytes`. Start at offset 0. Add the page's
count to the offset for the next page. If a name is too large for a reply,
the page returns zero entries and queues `-223`. Advance the offset by
one to skip that entry.

`MMEM:DATA? "FOLDER/FILENAME"` returns a definite-length binary block
ending in LF. The block length is the file length in bytes.
`MMEM:DEL "FOLDER/FILENAME"` deletes a file. Read `SYST:ERR?` after a
delete to check whether it succeeded.

Supported folders are `SCREENSHOTS`, `LOGS`, `CAPTURES`, `DECODE` and
`FRA` under `/openosc`. Files still being written are hidden until the
writer commits them. File transfers hold a reader until the transfer ends
or is canceled. Deletion while a file is in use returns `-221`.

The instrument saves screenshots as PNG. Use the filename from
`MMEM:CAT? "SCREENSHOTS"` to download a stored PNG or an earlier BMP.
`DISP:DATA?` and `HCOP:DATA?` return the live screen as a 320×240 BMP.

## Serial decoder results

Select **UART (TTL)**, **RS-232**, **RS-485** or **DMX512** on the instrument under
**Analyze → Decoder**. `DEC:PROT?` and `DEC:RES?` report `UART`, `RS232`, `RS485`
or `DMX512`. The CSV returned by `DEC:DATA?` uses the protocol names `uart`, `rs232`,
`rs485` and `dmx512`. The first three use the UART event kinds `uart_data`,
`uart_break` and `uart_fragment`. DMX512 uses `dmx_packet`, `dmx_break` and
`dmx_fragment` in a v3 file with four more columns; see
[DECODER_RS485_DMX512.md](DECODER_RS485_DMX512.md). A `DEC:RES?` status of
`ac_coupled` means an RS-485 or DMX512 source channel was AC coupled and nothing
was decoded.

## Idle settings

| Command | Units and choices |
| --- | --- |
| `SYST:POW:TIM[?]` | Minutes, 0 (off), 15, 30 or 60 |
| `SYST:DISP:TIM[?]` | Seconds, 0 (off), 30, 60 or 120 |
| `SYST:DISP:DIM[?]` | Percent of full brightness, 0, 5, 10, 15, 20 or 25 |

These set the same values as the local settings menu. A USB host keeps
idle dimming and automatic power-off suspended.
