// An animated GIF for the tests (Qt writes no GIF): `colours.size()` frames of one colour each,
// w × h, each shown `delayCs` hundredths of a second, looping forever (NETSCAPE2.0). The pixels go
// as an "uncompressed" LZW stream — a clear code before every second pixel keeps every code 3 bits
// wide (the decoder's table never reaches 8 entries) — which any GIF decoder reads.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QList>

#include <algorithm>

inline QByteArray animatedGif(int w, int h, const QList<QColor> &colours, int delayCs)
{
    QByteArray out("GIF89a");
    auto u16 = [&out](int v) { out.append(char(v & 0xff)).append(char((v >> 8) & 0xff)); };
    u16(w);
    u16(h);
    out.append(char(0x91)); // a global colour table of 4 entries
    out.append(char(0));    // background colour
    out.append(char(0));    // pixel aspect ratio
    for (int i = 0; i < 4; ++i) {
        const QColor c = i < colours.size() ? colours.at(i) : QColor(Qt::black);
        out.append(char(c.red())).append(char(c.green())).append(char(c.blue()));
    }
    out.append("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 19); // loop forever
    for (int f = 0; f < std::min(4, int(colours.size())); ++f) {
        // Graphic control: the delay, nothing transparent, the frame kept (disposal 1).
        out.append("\x21\xf9\x04", 3);
        out.append(char(0x04));
        u16(delayCs);
        out.append(char(0)).append(char(0));
        // The frame over the whole picture, no local colour table.
        out.append(char(0x2c));
        u16(0);
        u16(0);
        u16(w);
        u16(h);
        out.append(char(0));
        out.append(char(2)); // LZW minimum code size: 2 (codes 3 bits wide)
        QByteArray data;
        quint32 bits = 0;
        int count = 0;
        auto put = [&](int code) {
            bits |= quint32(code) << count;
            count += 3;
            while (count >= 8) {
                data.append(char(bits & 0xff));
                bits >>= 8;
                count -= 8;
            }
        };
        for (int i = 0; i < w * h; ++i) {
            if (i % 2 == 0)
                put(4); // clear
            put(f);
        }
        put(5); // end of information
        if (count > 0)
            data.append(char(bits & 0xff));
        for (qsizetype i = 0; i < data.size(); i += 255) {
            const qsizetype n = std::min<qsizetype>(255, data.size() - i);
            out.append(char(n));
            out.append(data.mid(i, n));
        }
        out.append(char(0)); // the frame's data ends
    }
    out.append(char(0x3b));
    return out;
}
