#!/usr/bin/env python3
"""Expose the OpenOSC Companion's BLE UART as a local raw SCPI TCP socket."""

import argparse
import asyncio

from bleak import BleakClient, BleakScanner

SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


class ScpiBridge:
    def __init__(self, name):
        self.name = name
        self.busy = False

    async def client(self, reader, writer):
        if self.busy:
            writer.close()
            await writer.wait_closed()
            return
        self.busy = True
        try:
            def matches(device, advertisement):
                name = advertisement.local_name or device.name
                return self.name in (name, device.address) and SERVICE in advertisement.service_uuids

            device = await BleakScanner.find_device_by_filter(matches, timeout=10)
            if device is None:
                raise RuntimeError(f"Could not find {self.name} advertising the UART service")
            replies = asyncio.Queue(maxsize=128)
            async with BleakClient(device, pair=True, timeout=45,
                                   disconnected_callback=lambda client: writer.close()) as ble:
                def notify(_, data):
                    try:
                        replies.put_nowait(bytes(data))
                    except asyncio.QueueFull:
                        writer.close()

                await ble.start_notify(TX, notify)

                async def write_radio():
                    # Twenty bytes fit even when the central keeps the default ATT MTU of 23.
                    while data := await reader.read(20):
                        await ble.write_gatt_char(RX, data, response=True)

                async def write_socket():
                    while True:
                        writer.write(await replies.get())
                        await writer.drain()

                tasks = [asyncio.create_task(write_radio()), asyncio.create_task(write_socket())]
                try:
                    done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                    for task in done:
                        task.result()
                finally:
                    for task in tasks:
                        task.cancel()
                    await asyncio.gather(*tasks, return_exceptions=True)
        except Exception as error:
            print(f"SCPI session ended: {error}")
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            finally:
                self.busy = False


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", help="Companion name or BLE address")
    parser.add_argument("--port", type=int, default=5025)
    args = parser.parse_args()
    bridge = ScpiBridge(args.device)
    server = await asyncio.start_server(bridge.client, "127.0.0.1", args.port)
    print(f"SCPI listening on 127.0.0.1:{args.port}")
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
