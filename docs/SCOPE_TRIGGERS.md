# Scope trigger types

On the Scope screen, select **Trigger** with F3 and open its menu with MENU. Choose **Type**, then set
the source, polarity or slope, levels and time condition. The same menu sets Auto, Normal or Single
and pretrigger position. After selecting the Trigger card, a short press of F3 cycles the capture mode.

| Type | Event | Trigger point |
| --- | --- | --- |
| Edge | A rising or falling crossing of one level | The crossing |
| Pulse width | A complete positive or negative pulse whose width meets the time condition | The trailing crossing |
| Runt | A positive pulse that returns below the low level before crossing the high level, or a negative pulse that returns above the high level before crossing the low level | The return crossing |
| Rise/fall time | A rise through both levels or a fall through both levels whose transition time meets the time condition | The crossing of the second level |

Pulse width and Rise/fall time can compare against a lower or upper time limit, or test whether the
time falls between or outside two limits. The comparisons are strict. Runt also offers **Any** to
accept any observed width. The two voltage levels should be distinct. With Noise reject enabled, the
signal must move far enough from its starting threshold to arm the next candidate. A run that has
already started before the captured samples, or continues beyond them, cannot qualify.

The F3 card names the selected qualified type. The horizontal marker and top-bar value show the level
at which the trigger is decided. Up and Down on the Trigger card adjust this level. For Runt it is the low level for a positive pulse and the high
level for a negative pulse. For Rise/fall time it is the high level for a rise and the low level for a
fall. Both levels remain editable in the menu. Arrow direction follows the displayed trace if it is
inverted.

## Capture behavior

Normal and Single re-arm after a rejected candidate. Single holds the first qualified record or an
explicitly forced capture. Auto can still display an untriggered record after its wait budget, which
follows the record duration with a 20 ms minimum and a 1 s maximum. At slow timebases up to 20 s/div,
the qualified types use finite records, and Roll is available with Edge. Past 20 s/div the capture
only rolls: a qualified type is kept and applies again at 20 s/div or faster.

OpenOSC qualifies these events in the ADC samples around a comparator crossing. The entire pulse or
transition and the samples needed for noise rejection must fit in the buffered record. A longer
timebase, more pretrigger history or a deeper record can help with longer events. Short or isolated
events can fall between samples or during the re-arm interval. A qualified event away from the
comparator crossing can place the trigger outside the requested pretrigger position when the record
cannot fit at that position.

The configurable time range is 1 ns to 1 s. Time qualification counts whole samples at the actual
sample rate (`ACQ:SRAT?`). The smallest usable interval depends on the current timebase and sample rate.

- Counting whole samples introduces up to one sample period of timing uncertainty. A limit that is an
  exact number of samples is compared strictly: a pulse of exactly that many samples is neither shorter
  nor longer.
- One sample period is about 7.1 ns at 140 MS/s and 3.6 ns at 280 MS/s. Qualification also requires
  the signal to cross the configured levels in the observed samples.
- The longest is the history the capture holds before the trigger point: at least the pretrigger part
  of the record, more when the ring has spare memory or the capture waited for the crossing.
- After a rejected candidate the instrument is blind until the next capture is armed: the end of the
  post-trigger part, the search of the record, the restart and the pretrigger fill. A rare runt among
  frequent normal pulses is caught when it falls inside a capture's record around a comparator
  crossing. More frequent events and a longer capture window increase the chance of observing one.

## SCPI

Each setting below also accepts a query. Voltage and time settings take SI values with optional units.

| Command | Values |
| --- | --- |
| `TRIG:MODE` | `EDGE`, `PULS`, `RUNT`, `SLOP` |
| `TRIG:SWE` | `AUTO`, `NORM`, `SING` |
| `TRIG:EDGE:SOUR` | `CHAN1`, `CHAN2` |
| `TRIG:POL` | `POS`, `NEG` |
| `TRIG:WHEN` | `ANY`, `LESS`, `GRE`, `INS`, `OUTS` |
| `TRIG:TIME:LOW`, `TRIG:TIME:UPP` | Lower and upper limit, for example `50us` |
| `TRIG:EDGE:LEV` | Single-level threshold for Edge and Pulse width |
| `TRIG:LEV:LOW`, `TRIG:LEV:HIGH` | Band endpoints for Runt and Rise/fall time |
| `TRIG:EDGE:NREJ` | Noise reject `ON`, `OFF` |

To catch positive pulses shorter than 50 µs on CH1, for example:

```text
TRIG:MODE PULS
TRIG:EDGE:SOUR CHAN1
TRIG:POL POS
TRIG:EDGE:LEV 1.65V
TRIG:WHEN LESS
TRIG:TIME:UPP 50us
SING
```
