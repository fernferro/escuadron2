#!/usr/bin/env python3
"""Lee memoria de la imagen original.

    python3 tools/analysis/rdmem.py f 80096A88 40   # floats
    python3 tools/analysis/rdmem.py s 7FD9F3C8 108  # textos
"""
import struct,sys
d=open('orig/GSWE64/combined.dol','rb').read()
off=struct.unpack('>18I',d[0:72]);addr=struct.unpack('>18I',d[72:144]);size=struct.unpack('>18I',d[144:216])
def rb(a,n):
    for i in range(18):
        if size[i] and addr[i]<=a<addr[i]+size[i]:
            o=off[i]+a-addr[i];return d[o:o+n]
mode=sys.argv[1];a=int(sys.argv[2],16);n=int(sys.argv[3],16)
b=rb(a,n)
if mode=='f':
    for i in range(0,n,4): print(hex(a+i),struct.unpack('>f',b[i:i+4])[0],b[i:i+4].hex())
else:
    i=0
    while i<n:
        j=b.index(b'\0',i); print(hex(a+i),repr(b[i:j])); i=(j+4)&~3 if j>i else i+4
