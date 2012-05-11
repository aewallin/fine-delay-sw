import struct
import datetime
import sys

"""
        int32_t card_id;
        uint8_t type;
        uint64_t utc;
        uint32_t coarse;
        uint32_t frac;
        uint32_t seq_id;
        uint32_t channel;'
"""
f = file(sys.argv[1])
while True:
    s = f.read(32)
    if not s: break
    (card_id,
    type,
    utc,
    coarse,
    frac,
    seq_id,
    channel,) = struct.unpack('<IbQIIIIxxx', s)
    elnano = int(float(coarse * 8 * 512 + frac) / 512)
    try:
        fecha = repr(datetime.datetime.fromtimestamp(utc + elnano/1.0e9))
    except ValueError:
        fecha = ()
    print "%04x:%d %d %06d %012d %06d %03x \t%d.%09d %s" % (card_id,
		channel,
		type,
		seq_id,
		utc,
		coarse,
		frac,
                utc,
                elnano,
                fecha,
                )
