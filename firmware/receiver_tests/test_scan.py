import asyncio
from bleak import BleakScanner

async def main():
    print("scanning...")
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        print(d.address, d.name)

asyncio.run(main())