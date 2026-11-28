# OpenOSC Wireless Module

This ESP-IDF project runs on the ESP32-C3 SuperMini. Both the ZT703S and ZT706S use the same add-on image and expose **Settings → Wireless Module**. Install the OpenOSC firmware for your instrument model. Hardware tests used the ZT703S.

## Wiring

| ESP32-C3 SuperMini | ZT703S / ZT706S | Direction |
| --- | --- | --- |
| GPIO4 | SWCLK / PA14 | Instrument TX and wake signal |
| GPIO3 | SWDIO / PA13 | Companion TX |
| 3V3 (marked 3.3 on some boards) | VDD (about 3.0 V) | Power from the instrument |
| GND | GND | Common signal ground |

Use 3.3 V logic. Connect the SuperMini's 3V3 pin directly to the instrument's VDD pad and connect their grounds. Switching off the instrument cuts power to the SuperMini.

Disconnect the SWD debug probe before enabling the companion. Restart the instrument with the probe disconnected if the menu still reports an attached debugger. The instrument returns the pins to SWD when the link closes. The companion leaves its TX pin as an input before the handshake and during sleep. Disconnect the companion from the SWD pins when debugging or programming the STM32 over SWD.

PA13 and PA14 have no USART alternate function on the STM32H7B0. The instrument uses GPIO with LPTIM2/3 and BDMA2. The ESP uses UART1. The link runs at 500000 baud, 8N2. The PA14/GPIO4 wire idles low and a high pulse wakes the ESP32-C3. Install matching STM32 and ESP32-C3 builds together. [STM32H7B0 datasheet](https://www.st.com/resource/en/datasheet/stm32h7b0vb.pdf)

The default Wi-Fi transmit-power limit is 8.5 dBm. The **Wi-Fi TX power** row and web settings offer limits from 2 to 20 dBm. The same saved limit applies in Access point and Station modes. Recovery uses it during a browser update. A Recovery started by the physical pin uses the default limit.

Only the instrument opens a reply window. The instrument lets wireless commands and data through in short gaps between records. A capture already in progress keeps its timing. With **Power** set to **Off**, no time is reserved for the link. Continuous Roll and a scope waiting for its trigger can defer wireless commands indefinitely. Use **HOLD** to release the bus in those cases. Ordinary companion functions use GPIO and support RDP1. The **Wireless SWD** menu item appears only on RDP0 development units.

## Build and install

Build the companion application, Recovery and initial USB image from the OpenOSC checkout using PlatformIO:

```sh
python3 scripts/build_firmware.py --esp32-only
```

The ESP-IDF 5.5 build uses 4 MiB of available flash. The development output directory is
`build/esp32c3/`. Running `python3 scripts/build_firmware.py` without `--esp32-only` also
stages the release assets in `build/releases/vX.Y.Z/assets/`.

| Release asset | Development image | Use |
| --- | --- | --- |
| `esp32c3-addon-vX.Y.Z.bin` | `OpenOSC-Companion-ESP32-C3-USB.bin` | Initial USB installation or migration to the Recovery partition layout |
| `esp32c3-addon-app-vX.Y.Z.bin` | `OpenOSC-Companion-ESP32-C3.bin` | **Update module app** in the web admin page |
| `esp32c3-addon-recovery-vX.Y.Z.bin` | `OpenOSC-Companion-ESP32-C3-Recovery.bin` | **Update Recovery** in the web admin page |

For initial USB installation, disconnect the SuperMini from the instrument before connecting its USB cable. Enter the ESP32-C3 ROM bootloader and flash the USB image at offset zero. This initializes the ESP flash layout and its settings area. With esptool 4.x installed:

```sh
esptool.py --chip esp32c3 --port <esp32-c3-usb-port> write_flash 0x0 build/esp32c3/OpenOSC-Companion-ESP32-C3-USB.bin
```

Build the matching STM32 firmware with `pio run -e firmware` for the ZT703S or `pio run -e zt706s_firmware` for the ZT706S. Install the model's `.OSC` file through OpenOSC Recovery.

## Use from the instrument

1. Open the main Settings menu by holding **MENU**. Open **Wireless Module** and select **Power** to wake the board.
2. Select **Wi-Fi mode → Access point** to start the protected `OpenOSC-xxxxxx` network. Its suffix is the last six hexadecimal digits of the ESP32-C3 Wi-Fi MAC address. The **Network**, **IPv4 address** and **AP password** rows show the connection details. On a new installation, join with password `12345678`. Existing installations retain their AP password during the update. Open `http://openosc.local/` if the captive portal does not open automatically. The web admin opens without a login.
3. To join an existing 2.4 GHz network, use **Find Wi-Fi networks**, select the network, enter the password and select **Connect**. In the password editor, select **Character**, step through ASCII characters and confirm. **Add character** appends it. **Delete last** removes it. The same STA setup is available in the web admin page. **Wi-Fi mode → Station** reconnects to the saved network.
4. Use **Sync time** once the companion has network time. In AP mode, the admin page can set the companion clock from a browser. The instrument displays UTC and uses it for new FAT file timestamps for the current boot.
5. **Sleep when idle** offers Never, 1, 5, 15 or 30 minutes. The default is 15 minutes, and the module keeps any previously saved choice. The countdown restarts after the last client disconnects or after an action in the Wireless Module menu. Devices joined to the access point, Bluetooth LE links, and open SCPI and web administration connections keep the module awake. In Station mode, the router provides network access and the timer follows connections to the instrument. Select **Power** again to disconnect clients, turn off both radios and enter deep sleep. The instrument reports **No reply** if it cannot confirm the sleep request. On power-up the ESP waits in deep sleep for the instrument.

The `openosc.local` address uses multicast DNS and works in both AP and Station modes from the same local network. If the client cannot resolve `.local` names or another instrument is using that name, open `http://192.168.4.1/` in AP mode or use the **IPv4 address** shown on the instrument in Station mode.

Use **Save AP password** in the web admin to choose an 8–63 character WPA2 password. An active Access point restarts with the new password. Upgrade ESP Recovery before changing the password to keep browser updates on the same network. The AP password, Wi-Fi transmit-power limit, station credentials, Wi-Fi SCPI preference and inactivity timeout are stored on the ESP. Wi-Fi and Bluetooth start with their radios off after the STM32 wakes the companion.

## Browse instrument files

Open **Files** in the web admin to browse the STM32's external SPI flash under `/openosc`.
Expand folders in the tree and select one to see its contents. Use the path above the list to move back up.
The browser can download, upload, create, rename and delete files. It can also create, rename
and delete empty folders. An upload uses a temporary file until it is complete. Uploading to a name
that already exists is rejected. If the scope is continuously rolling or waiting for a trigger, pause
acquisition before using **Files**.

## SCPI over Wi-Fi and Bluetooth LE

- Enable **Wi-Fi SCPI**, then connect to the displayed IPv4 address on TCP port **5025**. Use a raw socket (VISA resource `TCPIP0::<address>::5025::SOCKET`) and newline-terminated commands such as `*IDN?\n`. The AP and STA modes both support this socket. Use it on a trusted network. The raw SCPI socket itself uses plain TCP.
- Enable **Bluetooth LE** and use the **BLE PIN** on the instrument to pair. The service uses the Nordic UART Service UUIDs. The PC bridge below exposes it to the same raw SCPI clients on the local PC. ESP32-C3 provides Bluetooth LE. [Espressif BLE guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-guides/ble/overview.html)

```sh
python -m pip install 'bleak>=1.0'
python src/esp32c3/ble_scpi.py OpenOSC-xxxxxx
```

Connect the SCPI client to `127.0.0.1:5025` while the bridge is running. Use `--port` if that local port is occupied. The first connection can prompt for the six-digit PIN. TCP, WebSocket and BLE share one wireless SCPI session. USB CDC has priority over the shared serial SCPI parser. USBTMC has its own parser.

Commands and replies use the same OpenOSC SCPI handling as USB, including definite binary blocks. The UART sends zero-delimited COBS frames with a CRC, retries a lost reply without repeating its command, and applies backpressure to SCPI. Large waveform transfers take longer over the companion than over USB. In RUN, use `WAV:FRAM?` for successive records, or send `WAV:SNAP` before each `WAV:SEQ?` and `WAV:DATA?` pair. A leased record can temporarily limit local refresh until the next snapshot.

### WebSocket transport for browser clients

Enable **Wi-Fi SCPI** and connect to `ws://openosc.local:5026/scpi` or use the instrument's IPv4 address. Use a trusted network for this plaintext connection. The endpoint accepts the Wireless Module's web origins, `https://fyfar.github.io`, and native clients that omit the Origin header. An occupied wireless session returns HTTP 409 during the handshake.

- Open the socket without a WebSocket subprotocol and set `binaryType = "arraybuffer"`.
- Send newline-terminated SCPI commands as text or binary. Keep client messages at or below 1,024 bytes.
- Append each binary reply to the existing SCPI receive buffer. One reply can span several messages, and one message can contain parts of multiple replies. Parse line terminators and IEEE 488.2 binary block lengths from the accumulated bytes.
- Start with `WAV:POIN 1024` and `WAV:FRAM?` for live waveforms. Wait for the complete reply before sending the next query.
- After switching instrument profiles, poll `*IDN?` on the same connection until the expected identity appears. Reconnect if the socket closes.

When a hosted page connects to the module, the browser may prompt for local network access. Enter the module's IPv4 address and allow the request if prompted. [Chrome local network access guide](https://developer.chrome.com/docs/modern-web-guidance/guides/security/local-network-access)

## Wireless SWD programmer

OpenOCD and GDB can halt and resume the CPU, read and write memory and registers, set breakpoints, reset the STM32 through SWD, and program and verify flash. Disconnect any separate wired debug probe before enabling the companion.

1. Press **HOLD** if a scope capture is running. Wake the companion in **Settings → Wireless Module**, bring up Wi-Fi or enable **Bluetooth LE**, then select **Wireless SWD**. The instrument hands GPIO4/SWCLK and GPIO3/SWDIO over to the probe. SCPI and web administration are suspended while SWD owns the pins.
2. Use OpenOCD with the **CMSIS-DAP TCP** backend. Stock OpenOCD 0.12.0 predates this backend. For a read-only connection check over Wi-Fi, use the address shown on the instrument:

   ```sh
   OPENOSC_DAP_HOST=192.168.4.1 openocd -f src/esp32c3/zt703s-swd.cfg -c 'init; targets; shutdown'
   ```

   Wi-Fi listens on TCP port 4441 only while SWD is enabled. The listener accepts any host that can reach it on the active network. AP mode uses the companion's existing WPA2 password to join the network.

3. For Bluetooth, use the advertised name or address and the Bleak dependency above. Complete ordinary Bluetooth pairing if prompted. After the bridge reports readiness, run OpenOCD in another terminal.

   ```sh
   python src/esp32c3/swd_probe.py --ble OpenOSC-xxxxxx
   openocd -f src/esp32c3/zt703s-swd.cfg -c 'init; targets; shutdown'
   ```

   The BLE bridge exits when OpenOCD disconnects. Start it again for another session. To run a GDB server, omit the `-c` argument and connect GDB to `127.0.0.1:3333`.

4. When finished, run `python src/esp32c3/swd_probe.py --wifi <address> --close` or the same command with `--ble <device>`. The probe clears halting debug and releases the pins. It also detaches five seconds after OpenOCD disconnects normally. If the connection drops unexpectedly, the probe waits up to one minute for a reconnect so an interrupted flash write can be retried. The web admin and the selected radios return after a session started from the instrument. Select **Power** in **Wireless Module** to reconnect the STM32 link. If detach fails, the probe remains reachable so it can be retried. A probe waiting for its first connection expires after the configured idle period, or five minutes when **Never** is selected. An attached debugger can remain paused.

### Recover an instrument that cannot start OpenOSC

Keep the STM32 powered and reset it to release its SWD pins. Temporarily connect **GPIO5 to GND** on the SuperMini and reset the **SuperMini**. GPIO5 is sampled at startup. The companion starts its WPA2 AP and BLE probe independently of the STM32. The SuperMini USB serial console prints the AP name, Wi-Fi password and pairing PIN. Remove the jumper for normal companion operation.

After detaching a standalone probe, the ESP returns to deep sleep until the instrument wakes it.

The two-wire connection supports software reset through SWD. For hardware reset or connect-under-reset, wire **GPIO6** on the SuperMini to **NRST** on the target. GPIO6 is open drain. With the wire installed, add `-c 'reset_config srst_only srst_nogate connect_assert_srst'` to the OpenOCD command.

[Arm CMSIS-DAP commands](https://arm-software.github.io/CMSIS-DAP/latest/group__DAP__Commands__gr.html) · [OpenOCD debug adapter configuration](https://openocd.org/doc/html/Debug-Adapter-Configuration.html)

## Update the ESP32-C3 over Wi-Fi

1. Open the companion's web admin page on its access point or at its station address and select **ESP32-C3 Update**. To update the instrument, use OpenOSC Recovery on the instrument.
2. For application updates, select the application image from the table above and press **Update module app**. Keep the tab open. The ESP restarts in Recovery and the page continues the upload on the same network. In AP mode, the SSID, password and address stay the same. In Station mode, Recovery rejoins the saved Wi-Fi and uses the current address for the upload. A brief Wi-Fi interruption is normal during the restart.
3. For Recovery updates, select the Recovery image from the table above and press **Update Recovery**. The running companion verifies the new Recovery while leaving the application and settings in place. If the transfer is interrupted, retry from the same admin page before starting a firmware update.

Recovery runs from a 768 KiB factory partition and writes one 1.5 MiB application slot. If an application upload is interrupted, repeat the upload from the admin page. Recovery can return to a valid installed application after two minutes without an active upload. To enter Recovery without a working companion application, hold **GPIO7 to GND** while resetting the SuperMini and release it after Recovery starts. It exposes the usual `OpenOSC-xxxxxx` access point. Use **Start module application** if the application is still valid.

The instrument can show **Companion link timed out** while ESP Recovery is writing. Once the ESP32-C3 admin page has returned, select **Power** in **Wireless Module** if the instrument link needs to reconnect.

After an app update or Wireless SWD session, the web admin stays reachable for up to two minutes while the instrument reconnects. The module then sleeps if the link is still disconnected. An active web update can finish before the module sleeps.

Older Recovery builds need to be updated before the browser can continue an application upload on the same network. On an older installation with a reachable Recovery, use its existing upload page once to install the current companion firmware, then update Recovery from the new admin page.

Use the IPv4 address when updating a module whose installed Recovery does not yet advertise `openosc.local`. After updating both images, the hostname works during the application-to-Recovery handoff as well.

The ESP-IDF bootloader watchdog covers startup. An eight-second task watchdog covers the main loop, the debug service and firmware uploads. If a newly installed image resets before completing its startup check, the bootloader returns to Recovery.

[Espressif OTA documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/system/ota.html)
