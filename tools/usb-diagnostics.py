#!/usr/bin/env python3
"""Read USB FX test counters via EP0. Never claims/detaches an audio interface."""
import argparse
import json
import struct
import sys
import time

FIELDS = ('version sofCount codecCount playUnderruns playOverruns captureUnderruns '
          'captureOverruns malformed midiDrops isoRecoveries maxAudioUs '
          'audioOverBudget endpointFailures missedSofs').split()

def decode(data):
    if len(data) != len(FIELDS) * 4:
        raise ValueError(f'Unexpected diagnostics length: {len(data)}')
    result = dict(zip(FIELDS, struct.unpack('<' + 'I' * len(FIELDS), bytes(data))))
    if result['version'] != 1:
        raise ValueError('Unsupported diagnostics version')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--count',type=int,default=1)
    p.add_argument('--interval',type=float,default=1)
    p.add_argument('--serial',help='Select one FX- serial if multiple devices are attached')
    a=p.parse_args()
    if a.count<1 or a.interval<.1: p.error('count >=1 and interval >=0.1 required')
    try:
        import usb.core
        import usb.util
    except ImportError:
        sys.exit('Requires PyUSB and a libusb backend. No hardware was modified.')
    try:
        candidates=[]
        for dev in usb.core.find(find_all=True,idVendor=0x0483,idProduct=0x5740):
            serial=usb.util.get_string(dev,dev.iSerialNumber)
            if serial.startswith('FX-') and (a.serial is None or serial==a.serial):
                candidates.append((dev,serial))
        if len(candidates)!=1:
            sys.exit(f'Found {len(candidates)} matching USB FX devices; attach one or use --serial.')
        dev,serial=candidates[0]
        for i in range(a.count):
            result=decode(dev.ctrl_transfer(0xc0,0x5a,0,0,len(FIELDS)*4,timeout=1000))
            print(json.dumps({'time':time.time(),'serial':serial,**result}),flush=True)
            if i+1<a.count: time.sleep(a.interval)
    except usb.core.USBError as exc:
        sys.exit(f'Could not read diagnostics: {exc}. Audio-driver access may block libusb; this tool does not detach drivers.')
if __name__=='__main__': main()
