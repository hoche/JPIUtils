# General notes on the format of JPI .DAT files

The general format of the .DAT files is a number of textual header records
(broken into lines with CR/LF), followed by binary data dumping each flight's
data. There is a header record for each flight which stores how long the
flight's data is.

## Header records

Header records are "$X,....*NN\r\n" where X is a letter for a record type, and
header records are just text lines delimited by line feeds. Most are series of
numeric short values just converted to comma delimited ascii.

Header records all end in "*NN", where the NN is two hex digits. It is a
checksum, computed by a byte which is the XOR of all the bytes in the header
line excluding the initial '$' and trailing '*NN'.

There is also an end of file record following all of the flight data, which
appears to be "$E,4*5D\r\n" in all cases seen. No useful meaning is known.

### Header record types

Fields are in ascii, generally unsigned short values delimited by commas:

$U = tail number
    $U,N12345_*44

$A = configured limits:
VoltsHi*10,VoltsLo*10,DIF,CHT,CLD,TIT,OilHi,OilLo
    $A,305,230,500,415,60,1650,230,90*7F

$F = Fuel flow config and limits
empty,full,warning,kfactor,kfactor
    $F,0,999,  0,2950,2950*53

$T = timestamp of download, fielded (Times are UTC)
MM,DD,YY,hh,mm,?? maybe some kind of seq num but not strictly sequential?
    $T, 5,13, 5,23, 2, 2222*65

$C = config info (only partially known)
model#,feature flags lo, feature flags hi, unknown flags,firmware version
   $C, 700,63741, 6193, 1552, 292*58

The feature flags is a 32 bit set of flags as follows:

    // -m-d fpai r2to eeee eeee eccc cccc cc-b
    //
    // e = egt (up to 9 cyls)
    // c = cht (up to 9 cyls)
    // d = probably cld
    // b = bat
    // o = oil
    // t = tit1
    // 2 = tit2
    // a = OAT
    // f = fuel flow
    // r = CDT (also CARB - not distinguished in the CSV output)
    // i = IAT
    // m = MAP
    // p = RPM
    // *** e and c may be swapped (but always exist in tandem)
    // *** d and b may be swapped (but seem to exist in tandem)
    // *** m, p and i may be swapped among themselves, haven't seen
    //     enough independent examples to know for sure.

$D = flight info
flight#, length of flight's data in 16 bit words
    $D,  227, 3979*57

$L = last header record
unknown meaning
   $L, 49*4D


Binary data follows immediately after the CR/LF of the last record.

All binary data records are checksummed and the single byte checksum is
appended following each record. The checksum is a simple XOR of every byte of
the data record for firmware versions before 3.00, and the negative of a
simple sum of every byte of the data record for firmware versions after 3.00.

See calc_old_checksum and calc_new_checksum() functions

It should be noted that this is the ONLY change to the data format that was
made when JPI decided to no longer provide software that translated the .DAT
format to .CSV. In other words, they added no features they just purposely
broke old software. Draw your own conclusions about the motivation for this.


## Binary Records

1. Flight information header

The flight header follows immediately after the $L record, and is as follows:

    struct flightheader {
       ushort flightnumber;  // matches what's in the $D record
       ulong flags;          // matches the "feature flags" in the $C record
       ushort unknown;       // may contain flags about what units (e.g. F vs. C)
       ushort interval_secs; // record interval, in seconds (usually 6)
       datebits dt;          // date as fielded bits, see struct below
       timebits tm;          // time as fielded bits, see struct below
    };

The interval_secs field does not appear always to be the case, sometimes it is
a value that doesn't make sense. In that case the default interval is 6
secs. But that field may not be fully understood regarding when it is not the
interval.

The datebits/timebits are bit fields as follows:

    // pack a date into 16 bits
    struct datebits {
       unsigned day:5;
       unsigned mon:4;
       unsinged year:7;
    };

    struct timebits {
       unsigned secs:5;      // #secs / 2 is stored
       unsigned mins:6;
       unsigned hrs:5;
    };


2. Data records

The data is best thought of as an array of 48 short integer values. Each array
element is initialized to 0xf0. Each stored data record is then just the
one byte value difference from the previous value (EGTs are an exception and
have a "scale" byte that can make them a 2 byte short difference).

The difference data is then compressed using a system of simply excluding that
which does not change. Bit flags indicate which fields are changed. The fields
are further broken into 6 sets of 8, and entire "sets" can be
suppressed. Again, there are bit flags indicating which sets are present.

The fields are broken into 6 sets of 8, and both the field and the set
can be suppressed for a given data record. The existence of sets and fields in
the data record are flagged with bit flags.

The timestamp is not stored and is implied by the initial date/time from the
flightheader incremented by the interval_secs.

The resultant stream is just the bytes that exist where indicated by a bit
flag.

The data differences record can be roughly thought of as follows, though
fields will be compressed away based on bit flags in the flags fields.

    struct record {
        // These first three bytes always exist
       byte decodeflags[2];  // Always identical, don't know why there are two
       byte repeatcount;

       // The following are compressed based on bits in decodeflags bytes
       byte fieldflags[6];   // init to zero when considering each data rec
       byte scaleflags[2];
       byte signflags[6];

       // The following are compressed based on bits in fieldflags/scaleflags
       byte fielddif[48];   // dif values to apply to the datarec struct
       byte scaledif[16];

       // checksum of all of this record's bytes (just the ones that exist
        // in the file)
       byte checksum;
    };

In all cases seen, decodeflags[0] == decodeflags[1]. It's possible that
decodeflags[1] was meant to signal existence of the signflags bytes
independently, but that has not been observed.

For each bit set in (decodeflags[0] & (1 << x)) (where 0 <= x && x < 6),
fieldflags[x] and signflags[x] will exist. For each bit set in (decodeflags[0]
& (0x40 << x) (where 0 <= x && x < 2), scaleflags[x] will exist.

If repeat count is non-zero, output the current data set again (incrementing
the timestamp) and then continue processing.

For each bit x set in fieldflags (i.e. fieldflags[x/8] & (1 << (x%8)) != 0),
the corresponding fielddif[x] value will exist (otherwise fielddif[x] remains
zero, i.e. no change from the previous value). If signbits bit x is set
subtract the fielddif[x] value else add the fielddif[x] value to that field in
the data record.

If the scaleflags bit x is set, then scaledif[x] will be the high order byte
of the difference value, so add (or subtract, based on the field's signflags
bit) (scaledif[x] << 8) to the xth field.

Each byte of scaleflags only applies to the first 8 (EGT/TIT) values of the
record for each engine (see record definition below). scaleflags[1] will be
zero for single engine cases.

The data fields are all 16 bit (short integer) values. Init all values to 0xf0
for the first record, then apply the difs of each record above. They are
ordered as follows, noting that some fields only apply to some configurations
of the EDM monitors based on features provided. In particular, some of the
EDM-800 features overlap with some of the EDM-760 twin features. In the single
engine cases the twin fields are ignored. Also the twin fields overlap such
that for 7/8/9 cyl engines they also use some of the fields beginning at the
twin data offset. Several fields (BAT, USD, FF, MAP) are stored as an integer
of the value*10 (i.e. the BAT field stores tenths of volts), so it must be
adjusted for output.

    struct datarec {
       // first byte of val/sign/scale flags
       short egt[6];
       short t1;
       short t2;

       // second byte of val/sign(/scale?) flags
       short cht[6];
       short cld;
       short oil;

       // third byte of val/sign(/scale?) flags
       short mark;
       short unk_3_1;
       short cdt;
       short iat;
       short bat;
       short oat;
       short usd;
       short ff;

       // fourth byte of val/sign(/scale?) flags
       short regt[6];			 // right engine for EDM-760
       union {
          short hp;                           // single engine EDM-800
          short rt1;                           // twin engine EDM-760
       };
       short rt2;

       // fifth byte of val/sign(/scale?) flags
       short rcht[6];                       // right engine for EDM-760
       short rcld;
       short roil;

       // sixth byte of val/sign(/scale?) flags
       short map;                              // single engine EDM-800
       short rpm;                              // single engine EDM-800
       union {
          short rpm_highbyte;                  // single engine EDM-800
          short rcdt;                           // twin engine EDM-760
       };
       short riat;
       short unk_6_4;
       short unk_6_5;
       short rusd;
       short rff;

    };

SPECIAL CASES AND EXCEPTIONS:

- DIF is a computed field and must be computed for each record output. 
- If HP exists (i.e. EDM-800), HP must be initialized to zero instead of 0xf0
   before the first record.
- If RPM_HIGHBYTE exists, this is the high byte of the RPM difference,
   similar to the scale bytes of the EGT fields. The sign of add/subtract must
   follow the signflag from just the RPM field.

Loop over a given flight's data parsing the difference records as above until
you reach the end of that flight's data. There might be a pad byte at the end
of the whole flight's data if the last record ends on an odd number of bytes
(remember flight data length is stored as the number of shorts, not number of
bytes). The next byte will begin the next flight's data.

