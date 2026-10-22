"""Write the Mac's local time into the stick's RTC over serial.

Usage: python tools/settime.py /dev/cu.usbserial-XXXX
Runs automatically after `pio run -t upload` (tools/upload_settime.py).
"""
import sys, time
from datetime import datetime
import serial

port = sys.argv[1]
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.2
s.dtr = s.rts = False  # Keep IO0 high; opening the port still reboots the stick.
s.open()
deadline = time.time() + 6
while time.time() < deadline:  # Wait for boot banner after an upload reset.
    if b"CLAWD_POMO_READY" in s.readline():
        break
s.write(datetime.now().strftime("T%Y-%m-%d %H:%M:%S\n").encode())
deadline = time.time() + 3
while time.time() < deadline:
    line = s.readline().decode(errors="replace").strip()
    if line.startswith("CLAWD_TIME_SET"):
        print(line)
        sys.exit(0)
sys.exit("settime: no CLAWD_TIME_SET reply")
