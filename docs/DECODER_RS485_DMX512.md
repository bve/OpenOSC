# RS-485 and DMX512 decoding

OpenOSC decodes an RS-485 pair from the two scope channels as **CH1 − CH2**. **RS-485**
reads asynchronous serial characters with the UART settings. **DMX512** reads lighting
packets as defined by ANSI E1.11-2024 (USITT DMX512-A). Both are in
**Analyze → Decoder → Protocol**. [ANSI E1.11-2024](https://tsp.esta.org/tsp/documents/docs/ANSI%20E1.11%20-%202024.pdf)

## Safe connection

The two probe tips measure the pair. The probe ground clips set the reference for both
channels, and they are connected to each other inside the instrument.

1. Connect the **CH1 tip to Data+** and the **CH2 tip to Data−**.
2. Connect **both ground clips to one verified common signal reference** of the bus. This is
   the signal common of the transceiver you measure at, or the DMX512 data link common.
   Verify it with a meter before you clip on. Never clip a ground lead to Data+ or Data−.
   The clip would short that line to the instrument's ground.
3. Check that each line's voltage to that reference stays within the instrument's rated
   input range for the probe and V/div in use. EIA-485 allows −7 V to +12 V of common
   mode, so measure Data+ and Data− against the reference first.
4. Measure low-voltage circuits only. If there is no safe common reference, or the
   equipment is mains powered and its common may float, use an **isolated RS-485
   receiver**. Measure its logic output with **Source → Receiver RO** on one channel.

A USB connection ties the instrument's ground to the computer's ground. Check the
reference with the intended USB connection in place before attaching the probes.

## Settings

| Row | Choices | Meaning |
| --- | --- | --- |
| Source | `Data+ CH1 - Data- CH2` (default), `Receiver RO on CH1/CH2` | The differential pair, or the logic output of an external receiver |
| RO line | CH1, CH2 | Receiver source only |
| Baud, Data bits, Parity, Stop bits, Bit order | as UART | RS-485 only; DMX512 is fixed at 250 kbaud 8N2, LSB first |
| Detect baud | in HOLD | RS-485 only; works on CH1 − CH2 too |
| Polarity | `Normal (idle high)`, `Inverted (idle low)` | Normal: the line idles (MARK) where CH1 − CH2, or RO, is high |
| CH1-CH2 thr | default 0 V | The centre of the decision band for CH1 − CH2 |
| Hysteresis | Auto, Manual | The total width of the band around the threshold |

**Polarity.** Vendors label the RS-485 lines A/B in both directions, so do not decide by
the labels alone. Look at the idle line. With **Normal**, an idle (MARK) line must show
CH1 above CH2. If the idle difference is negative, set **Polarity → Inverted** instead of
swapping the probes.

**Threshold and hysteresis.** The pair has its own threshold, 0 V by default. The CH1
and CH2 thresholds of the other protocols do not apply to it. A level changes only when
CH1 − CH2 leaves the band: above *threshold + hysteresis / 2* it reads MARK, below
*threshold − hysteresis / 2* it reads SPACE.
- **Auto** sets the band to 10 % of the CH1 − CH2 swing measured in each record. A 4 V
  swing gives a 400 mV band (±200 mV around the threshold).
- **Manual** uses the width you enter in mV.
- The receiver source uses that channel's normal threshold (1.65 V by default).

Each channel is converted through its own calibration, V/div and probe factor before the
subtraction, so the two channels may use different V/div or probes.

## Record requirements

Check the decoder's status line if the capture does not show the expected packets.

| Requirement | Otherwise |
| --- | --- |
| The source channels are enabled and, for differential mode, recorded together | `SOURCE OFF` |
| Each source channel is calibrated | `NO SCALE` |
| Each source channel is **DC** coupled | `AC: USE DC` (SCPI status `ac_coupled`) |
| Both traces on screen, not at the ADC limits | Counted as `CLIP` |
| At least 1.25 MS/s for DMX512 (5 points a 4 µs bit); 10 MS/s recommended | `FsLOW` |
| A record of at least 23 ms for one full 513-slot DMX512 packet | The packet is `CUT` at the record's end |

For RS-485, use at least 5 points a bit at the chosen baud rate.

## DMX512 timing

The decoder applies the receiver limits of ANSI E1.11-2024 table 7. Table 6 gives the
transmitter limits, which test transmitters should follow.

| | Receiver minimum (table 7) | Transmitter minimum (table 6) |
| --- | --- | --- |
| BREAK | 88 µs | 92 µs |
| Mark after BREAK (MAB) | 8 µs | 12 µs |
| BREAK to BREAK | 1196 µs | 1204 µs |
| Bit rate | 245–255 kbit/s (4 µs a bit) | the same |

- A space of at least 88 µs is a BREAK and starts a packet. Slot 0 after the MAB is the
  START code; 0x00 is the NULL START code for dimmer data. Up to 512 data slots follow,
  each 8N2, LSB first.
- Packets with fewer than 512 slots are normal and decoded as such.
- A slot with a stop bit at SPACE ends the packet with `FE`, and that slot and the rest
  of the packet are discarded (clause 9.1). So does a space that holds the line through the
  stop bits but is shorter than a BREAK. Decoding resumes at the next valid BREAK.
- A MAB shorter than 8 µs, or a BREAK-to-BREAK time below 1196 µs, marks the packet `TIM`.
  The slots are still shown.
- Start codes are identified but not interpreted: RDM, text and other alternate START
  code packets show their raw slots.

## Results

**Table and strip.** A DMX512 packet shows its START code as ID and its data slots as
DATA. Packets over 255 slots continue in `CONT` rows (255 + 255 + 2 for 512 slots). The
strip label is the START code and the slot count, for example `SC00 512`.

**Detail** shows:
- `SC 00 slots 256-510 of 512`: the START code, the slots in this row and the packet's total;
- `BREAK 92.0us MAB 12.0us`; `>=` before the BREAK when it began before the record;
- data lines numbered by DMX slot.

| Code | DMX512 meaning |
| --- | --- |
| `ok` | No timing or framing error detected. See `OPEN` in Detail if the record ended between slots |
| `FE` | Stop bit at SPACE, or a space shorter than a BREAK through the stop bits |
| `TIM` | MAB below 8 µs, or BREAK to BREAK below 1196 µs |
| `LEN` | More than 512 data slots; the first 512 are kept |
| `BRK` | A BREAK not followed by a START code |
| `CUT` | The record ends inside a slot, or starts inside a packet (`fragment`) |
| `EVT>` | The result's storage is full (512 rows or 4096 data bytes) |

RS-485 shows the UART results: `PE`, `FE`, `BRK`, `?` and so on.

## CSV and SCPI

- `DEC:PROT?` reports `RS485` or `DMX512`. The CSV protocol names are `rs485` and `dmx512`.
- The source lines in the CSV metadata are `DATA+` (CH1) and `DATA-` (CH2) with the
  CH1 − CH2 threshold and hysteresis, or `RO` with its channel.
- RS-485 files are format v2 with UART event kinds.
- DMX512 files are format **v3**: the 50 v2 columns, then four more.
  - `dmx_first_slot`: the slot number of the row's first data slot, blank when the row has no slots.
  - `dmx_slots`: the packet's data slot count, on its last row.
  - `break_s` and `mab_s`: the BREAK and MAB in seconds.
  - `id_hex` is the START code; `group` and `cont` link the rows of one packet.
