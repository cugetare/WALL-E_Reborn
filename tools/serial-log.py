"""Capture a bounded serial boot log; optional reset, no flash/NVS writes."""
import argparse
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM12')
parser.add_argument('--seconds', type=float, default=15)
parser.add_argument('--reset', action='store_true')
args = parser.parse_args()
with serial.Serial(args.port, 115200, timeout=0.2) as port:
    if args.reset:
        port.dtr = False
        port.rts = True
        time.sleep(0.15)
        port.rts = False
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        print(port.read(port.in_waiting or 1).decode('utf-8', errors='replace'), end='', flush=True)
