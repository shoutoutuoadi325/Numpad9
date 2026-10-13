"""Generate original Numpad9 icon without external dependencies."""
import struct, zlib
from pathlib import Path
root = Path(__file__).resolve().parent.parent

def png(size):
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            # Navy rounded square with a phone-layout 3x3 mint keypad.
            rx, ry = min(x, size-1-x), min(y, size-1-y)
            radius = size * .18
            inside = rx >= radius or ry >= radius or (rx-radius)**2+(ry-radius)**2 <= radius**2
            color = (18, 32, 54, 255) if inside else (0, 0, 0, 0)
            for ky in range(3):
                for kx in range(3):
                    cx, cy = size*(.29+.21*kx), size*(.29+.21*ky)
                    if abs(x-cx)<size*.064 and abs(y-cy)<size*.064:
                        color = (74, 224, 188, 255) if ky < 2 else (244, 248, 252, 255)
            row.extend(color)
        rows.append(b'\0'+bytes(row))
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows),9))+chunk(b'IEND',b'')

images = [png(n) for n in (32,48,64,128,256)]
output = struct.pack('<HHH',0,1,len(images)); offset=6+16*len(images)
for size, data in zip((32,48,64,128,256),images):
    output+=struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(data),offset); offset+=len(data)
(root/'assets'/'Numpad9.ico').write_bytes(output+b''.join(images))
(root/'assets'/'Numpad9.png').write_bytes(images[-1])
(root/'assets'/'store-logo-300.png').write_bytes(png(300))
(root/'assets'/'store-logo-1080.png').write_bytes(png(1080))

# White glyph on transparency for the Windows Input Indicator.
def ime_png(size):
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            alpha = 0
            for ky in range(3):
                for kx in range(3):
                    cx, cy = size*(.22+.28*kx), size*(.22+.28*ky)
                    if abs(x-cx)<size*.075 and abs(y-cy)<size*.075:
                        alpha = 255
            row.extend((255,255,255,alpha))
        rows.append(b'\0'+bytes(row))
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows),9))+chunk(b'IEND',b'')

sizes=(16,20,24,32,40,48)
images=[ime_png(n) for n in sizes]
output=struct.pack('<HHH',0,1,len(images)); offset=6+16*len(images)
for size,data in zip(sizes,images):
    output+=struct.pack('<BBBBHHII',size,size,0,0,1,32,len(data),offset); offset+=len(data)
(root/'assets'/'ime.ico').write_bytes(output+b''.join(images))
