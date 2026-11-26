# DMM session logging

Session logging records the multimeter on the instrument itself, without a PC. A session runs in the
background while the DMM screen shows, writes one CSV file in `/openosc/LOGS`, and can be opened later
on the device as a trend with a cursor, zoom and pan.

The instrument has no clock that survives a power cycle, so every time in a session is the elapsed time
since the session started, from `0`. To place a session on a calendar, add its start time to the
`start_s` and `end_s` columns on a PC (for example in a spreadsheet).

## Starting and stopping

Open **MENU → Measurement → Session log**:

| Row | What it does |
| --- | --- |
| **Start session / Stop session** | Starts a session on the current reading, or stops the running one. While a session runs, the value shows `REC` and the elapsed time. |
| **Mode** | **Interval** or **Event** (below). |
| **Interval** | 1, 2, 5, 10 or 30 s, 1, 2, 5 or 10 min. Interval mode only. |
| **Duration** | 1, 5, 10 or 30 min, 1, 2, 4, 8 or 12 h, 1, 2 or 7 days. The session stops by itself when it is reached. |
| **Event band** | 0.1, 0.2, 0.5, 1, 2, 5, 10, 20 or 50 %. Event mode only. |
| **Estimate** | Rows and file size of the interval plan, or the most an event session can write. |
| **Saved sessions** | The list of sessions, see [Viewing a session](#viewing-a-session). |

The defaults are Interval, 1 s, 1 h and 5.0 %. The settings are saved with the other settings. A
running session keeps the settings it started with, so a change applies to the next session.

A session needs a reading of a known function: start it once the DMM shows a value (or OL). While it
runs, the state pill of the DMM screen shows `REC`, and the instrument does not power itself off for
being idle.

A session ends and its file is completed when any of these happens:

- **Stop session** is selected (`end=manual` in the file);
- the duration is reached (`duration`);
- another instrument is selected with MODE (`mode_change`);
- the instrument is powered off or restarted (`shutdown`);
- the file reaches its size limit of 4 MiB (`storage_full`);
- the storage reports an error (`error`; what was written so far stays in the `.TMP` file).

The file is written as `SESnnnnn.TMP`, synced to the storage once a minute, and renamed to
`SESnnnnn.CSV` after its contents have been read back and verified, as other OpenOSC recordings are. If
power is lost, or the shutdown runs out of time before the verification ends, the `.TMP` file stays with
everything synced so far. The session list shows it as unfinished and opens it like any other.

The per-packet **CSV logging** of the Measurement menu (`DMMnnnnn.CSV`) is unchanged and independent:
both can run at the same time.

## Interval mode

The session is divided into intervals from its start: interval *k* covers `[k × I, (k + 1) × I)`, and
the last one ends at the duration. Each interval that received any packet becomes one row with the count
of valid readings in it and their first, last, minimum, maximum and average values. The DMM sends a
reading about every 330 ms, so a 1 s interval holds about three readings. The minimum and maximum keep a
spike or a drop that lasted one reading, at any interval.

Nothing is interpolated. An interval without a valid reading has no value: its status says what it
received instead (see [Statuses](#statuses)). Consecutive intervals without any packet form one
`no_data` row spanning all of them. When a session is stopped in the middle of an interval, that last
interval is written up to the stop if it received anything.

The planned rows must fit the file's size limit: about 52 000 rows. For example, a 1 s interval allows
up to 12 h, and a 2 s interval allows 1 day. A longer plan is refused at start, and **Estimate** says so.

## Event mode

Event mode writes a record each time the reading changes beyond the band, and nothing while it stays
inside. Each record has its start and end time, its first reading, and the minimum, maximum and average
of its readings.

- **Opening.** The first reading opens the first record (trigger `start`). Its value is the record's
  reference.
- **Band.** A later valid reading stays in the record while its distance from the reference is at most
  the band: the larger of *Event band* × |reference| and 5 units of the reference reading's last digit.
  The comparison counts in units of that last digit, so values such as 1.234 and 1.229 V differ by
  exactly 5 counts.
- **Closing and rearming.** A valid reading outside the band closes the record and opens the next one
  with itself as the reference (trigger `deviation`). A record ends where the next one starts. A spike shows as a short record of its own,
  followed by another `deviation` record when the reading returns.
- **At and near zero.** A percentage of a reading near zero would be narrower than one digit, so the
  5-digit floor applies there. With a 1 mV last digit and a reference of 0.000 V, readings from −0.005 to
  0.005 V stay in the record and 0.006 V opens the next one.
- **Readings without a value.** OL, a blank reading, the meter's own HOLD or another function close a
  value record and open a record of that status (trigger `state`). Readings of the same status join it.
  The first valid reading afterwards opens a value record again (trigger `resume`).
- **No data.** When no packet arrives for 2 s, a `no_data` record starts 2 s after the last packet and
  lasts until packets return.

The 5 % default suits a steady supply or a sensor output: noise and small drift stay in one record, and a
dropout, a step or an intermittent contact opens a new one. Lower values record drift in more detail and
write more records.

The worst case is one record a reading. The size limit then bounds an event session to about 38 000
records, and **Estimate** shows the most the chosen duration can write.

## What a session measures

A session measures the function shown when it starts. V and mV of the same coupling (DC or AC) count as
the same measurement, as do A and mA, because their values are recorded in SI units. A range change
keeps the session.

- The local **HOLD** and **REL** change only the screen. The session keeps recording the live readings,
  without the REL offset.
- The meter's own HOLD and REL (the readings the meter itself freezes or offsets) are recorded as
  `held`, or as `other_function` when its REL changes from what it was at the start.
- Another function, or a reading in another quantity, is recorded as `other_function` without a value.
  Return to the original function to continue the session, or stop it and start a new one.
- A temperature session keeps the thermocouple type it started with. If the type changes during the
  session, from the menu or with `SYST:DMM:TEMP:TYPE`, the readings after the change are recorded as
  `other_function` without a value, because they follow another curve. Set the type back to continue the
  session, or stop it and start a new one.

## Statuses

| Status | Meaning |
| --- | --- |
| `ok` | At least one valid reading. Only `ok` rows have values. |
| `overload` | OL readings only (rows with values count their OL readings in `overloads`). |
| `no_reading` | Blank, dashes or an unreadable display. |
| `held` | The meter's own HOLD froze the reading. |
| `other_function` | Another function or quantity, another thermocouple type, or the meter's own REL changed. |
| `no_data` | No packet at all. |

When a row without values received several kinds of readings, its status is the first of `overload`,
`held`, `no_reading` and `other_function` that occurred in it.

## File format

`/openosc/LOGS/SESnnnnn.CSV` is plain text with CRLF line ends. It can be copied over USB in Recovery or
read with the SCPI `MMEM` commands. Lines starting with `#` describe the session:

```text
# OpenOSC DMM session 1
# mode=interval,interval_s=1,duration_s=3600,threshold_pct=5.0,floor_counts=5
# function=voltage_dc,quantity=voltage,unit=V,dmm_relative=0,board=ZT703S
# time=seconds since the session start (no real-time clock),values=SI units
index,start_s,end_s,status,trigger,readings,first,last,min,max,avg,overloads
0,0,1,ok,,3,1.2345e+0,1.2346e+0,1.2344e+0,1.2346e+0,1.2345e+0,0
1,1,2,ok,,3,1.2346e+0,9.876e+0,1.2345e+0,9.876e+0,4.11223333333e+0,0
2,2,5,no_data,,0,,,,,,0
...
# end=manual,elapsed_s=125.4,records=126,readings=371,deviations=0,overloads=0,min=1.2344e+0,max=9.876e+0,avg=1.30144e+0
```

A temperature session adds the thermocouple type it started with, a letter from `B`, `E`, `J`, `K`, `N`,
`R`, `S` and `T`, to the `function` line, which then reads
`# function=temperature_k,thermocouple=J,quantity=temperature,unit=degC,dmm_relative=0,board=ZT703S`.
The `function` key stays `temperature_k` for every type; the curve is in `thermocouple=`. Other
functions, and a temperature the meter reports itself, have no `thermocouple=` key. A file from an
earlier firmware has none either: it was recorded with type K.

| Column | Meaning |
| --- | --- |
| `index` | Interval mode: the interval's number (a `no_data` run: its first). Event mode: the record's number. |
| `start_s`, `end_s` | Elapsed seconds since the session start, with up to six decimals. |
| `status` | See [Statuses](#statuses). |
| `trigger` | Event mode: `start`, `deviation`, `resume` or `state`. Empty in interval mode. |
| `readings` | Valid readings in the row. |
| `first`, `last`, `min`, `max`, `avg` | In SI units (V, A, Ohm, F, Hz, degC) with up to 12 significant digits. Empty without readings. |
| `overloads` | OL readings in the row. |

The last line, `# end=...`, is the session summary: how it ended, its elapsed time, the number of rows,
valid readings, `deviation` records and OL readings, and the minimum, maximum and average of all its
readings. A file without it is unfinished.

## Viewing a session

**MENU → Measurement → Session log → Saved sessions** lists the sessions, newest first, with their mode
and length, or `unfinished`.

| Key | List | Trend |
| --- | --- | --- |
| Up / Down | Select a session | Zoom in / out about the cursor |
| MENU, Right or F1 | Open the session | — |
| Left / Right | — | Move the cursor. Hold to move faster. Past an edge the view pans. |
| F1 / F2 | — | Zoom in / out |
| F3 | Delete the session (asks first) | Show the whole session |
| F4 | Back to Session log | Back to the list |
| MODE | The next instrument | The next instrument |

The trend shows:

- the mode, function, length and number of records above the plot (and the number of lines skipped as
  corrupt, if any);
- the plot, one pixel column at a time from the column's minimum to its maximum, so a one-reading spike
  or drop stays visible at any zoom;
- breaks where the session has no value: a mark on the baseline for a gap, at the top for OL;
- the window's start and end times, the zoom factor, and a bar showing where the window lies in the
  session;
- the cursor's time, and the minimum, maximum and average of the readings in its column.

The file is read in bounded slices between screen updates, first to index it and then for each view
of it. A line that does not parse is skipped and
counted. A file that is not a session, or is empty, says so and can still be deleted.

The live trend of the DMM screen (**Display → Trend**) is unchanged.

## Settings file

`settings.json` keeps the session settings in `multimeter.log` as four numbers:
`[mode, interval_s, duration_s, threshold_permille]`. Mode `0` is Interval and `1` is Event, and
`threshold_permille` is the event band in tenths of a percent (`50` = 5.0 %). The defaults are:

```json
"log": [0, 1, 3600, 50]
```

Files without `log` use the defaults. A value outside its list, of another type or missing falls back
to its default without affecting the other values.
