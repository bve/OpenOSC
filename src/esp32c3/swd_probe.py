#!/usr/bin/env python3
"""Expose an active OpenOSC wireless SWD probe to OpenOCD on this PC."""

import argparse
import asyncio
from contextlib import asynccontextmanager

PORT = 4441
INFO = b"DAP\0\x02\0\x01\0\0\xff"
INFO_REPLY = b"DAP\0\x04\0\x02\0\0\x02\0\x02"
CLOSE = b"DAP\0\0\0\x03\0"
CLOSED = b"DAP\0\0\0\x02\0"
CLOSE_FAILED = b"DAP\0\x01\0\x02\0"


@asynccontextmanager
async def carrier(args):
    if args.wifi:
        yield args.wifi, PORT
    else:
        from ble_scpi import ScpiBridge

        # The same NUS byte stream carries either SCPI or CMSIS-DAP, according to the instrument mode.
        async with await asyncio.start_server(ScpiBridge(args.ble).client, "127.0.0.1", 0) as server:
            yield "127.0.0.1", server.sockets[0].getsockname()[1]


async def pump(reader, writer):
    while data := await reader.read(2048):
        writer.write(data)
        await writer.drain()


async def relay(probe_in, probe_out, port):
    done = asyncio.Event()
    attached = False

    async def client(local_in, local_out):
        nonlocal attached
        if attached:
            local_out.close()
            await local_out.wait_closed()
            return
        attached = True
        tasks = (asyncio.create_task(pump(local_in, probe_out)), asyncio.create_task(pump(probe_in, local_out)))
        try:
            finished, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in finished:
                task.result()
        except OSError as error:
            print(f"SWD session ended: {error}", flush=True)
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            local_out.close()
            done.set()

    async with await asyncio.start_server(client, "127.0.0.1", port):
        print(f"SWD probe ready for OpenOCD on 127.0.0.1:{port}", flush=True)
        await done.wait()


async def run(args):
    async with carrier(args) as (host, port):
        probe_in, probe_out = await asyncio.wait_for(asyncio.open_connection(host, port), timeout=8)
        try:
            request, expected = (CLOSE, CLOSED) if args.close else (INFO, INFO_REPLY)
            probe_out.write(request)
            await probe_out.drain()
            timeout = 60 if args.ble else 5
            response = await asyncio.wait_for(probe_in.readexactly(len(expected)), timeout=timeout)
            if args.close and response == CLOSE_FAILED:
                await asyncio.wait_for(probe_in.readexactly(1), timeout=timeout)
                raise RuntimeError("The STM32 debug state could not be cleared. The probe is still available.")
            if response != expected:
                raise RuntimeError("The companion did not identify itself as a wireless SWD probe")
            if args.close:
                print("The probe is releasing the STM32 SWD pins.", flush=True)
            else:
                await relay(probe_in, probe_out, args.port)
        finally:
            probe_out.close()
            try:
                await probe_out.wait_closed()
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--wifi", metavar="ADDRESS", help="IPv4 address shown on the instrument")
    transport.add_argument("--ble", metavar="DEVICE", help="Advertised OpenOSC name or Bluetooth address")
    parser.add_argument("--port", type=int, default=PORT, help="Local OpenOCD port (default: 4441)")
    parser.add_argument("--close", action="store_true", help="Detach the STM32 and release the SWD pins")
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except (RuntimeError, TimeoutError, OSError, asyncio.IncompleteReadError) as error:
        parser.exit(1, f"SWD connection failed: {error}\n")


if __name__ == "__main__":
    main()
