#include "native/drivers/uac1.hpp"
#include "native/drivers/uac2_pcm.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// The JOYO MOMIX CAB's configuration (JieLi AC6926A4, vendor e5b7 product
// 0811), as its USB library emits it: UAC1, one AudioControl interface with a
// stereo 16-bit 48 kHz playback alternate on interface 2 and a stereo 16-bit
// 48 kHz capture alternate on interface 3, adaptive endpoints of 192 bytes, no
// feedback endpoint and no sampling-frequency control. The function bytes are
// verbatim from the firmware image; the configuration header was synthesized
// and is to be confirmed against a live dump.
namespace {
std::vector<uint8_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void checkMomix(const uac1::Configuration& c, uint8_t base = 0, uint8_t ids = 0) {
    const auto pairs = uac2::pairs(c.topology);
    assert(pairs.size() == 1);
    const auto& p = pairs[0];
    assert(p.capture.interface == 3 + base && p.capture.alternate == 1);
    assert(p.playback.interface == 2 + base && p.playback.alternate == 1);
    assert(p.capture.control == 1 + base && p.playback.control == 1 + base);
    assert(p.capture.terminal == 4 + ids && p.playback.terminal == 1 + ids);
    assert(p.capture.clock == 0 && p.playback.clock == 0);
    for (const auto* s : {&p.capture, &p.playback}) {
        assert(s->format.channels == 2 && s->format.subslot == 2 && s->format.bits == 16);
        assert(s->format.encoding == uac2::Encoding::SignedPCM);
        assert(s->data.maxBytes == 192 && s->data.interval == 1 && s->data.ticks() == 1);
        assert(s->data.sync == 2); // adaptive
        assert(s->feedback.address == 0);
        const auto* a = uac1::alternateFor(c, *s);
        assert(a && !a->continuous && !a->rateControl && !a->maxPacketsOnly);
        assert(a->rates.size() == 1 && a->rates[0] == 48000);
        assert(a->supports(48000) && !a->supports(44100));
    }
    assert(uac2::fullSpeed48k(p));
    assert(c.topology.features.size() == 2);
    assert(c.topology.features[0].id == 9 + ids && c.topology.features[0].source == 1 + ids &&
           c.topology.features[0].channels == 2);
    assert(c.topology.features[1].id == 10 + ids && c.topology.features[1].source == 2 + ids &&
           c.topology.features[1].channels == 1);
    assert(c.topology.terminals.size() == 4);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto live = load(argv[1]);
    uac1::Configuration c;
    assert(uac1::discover(live.data(), live.size(), c) == uac2::Error::None);
    checkMomix(c);
    // The UAC2 module must decline it, so the driver's fallback is what adopts it.
    uac2::Configuration uac2Config;
    assert(uac2::discover(live.data(), live.size(), uac2Config) != uac2::Error::None);

    // Renumber every interface, terminal, unit and endpoint. Nothing in
    // discovery may depend on the MOMIX's own numbering.
    auto moved = live;
    for (size_t off = 9; off < moved.size(); off += moved[off]) {
        auto* d = moved.data() + off;
        if (d[1] == 4)
            d[2] += 10;
        if (d[1] == 5)
            d[2] = uint8_t((d[2] & 0x80) | ((d[2] + 1) & 15));
        if (d[1] == 0x24 && d[0] >= 8 && d[2] == 1 && d[3] == 0 && d[4] == 1)
            for (unsigned i = 8; i < d[0]; ++i)
                d[i] += 10; // AC header interface collection
        if (d[1] == 0x24 && d[0] == 12 && d[2] == 2)
            d[3] += 20; // input terminal id
        if (d[1] == 0x24 && d[0] == 9 && d[2] == 3) {
            d[3] += 20; // output terminal id
            d[7] += 20; // and its source
        }
        if (d[1] == 0x24 && d[2] == 6 && (d[0] == 10 || d[0] == 9)) {
            d[3] += 20; // feature unit id
            d[4] += 20; // and its source
        }
        if (d[1] == 0x24 && d[0] == 7 && d[2] == 1 && d[5] == 1 && d[6] == 0)
            d[3] += 20; // AS_GENERAL terminal link
    }
    assert(uac1::discover(moved.data(), moved.size(), c) == uac2::Error::None);
    checkMomix(c, 10, 20);
    assert(uac2::pairs(c.topology)[0].capture.data.address == 0x84);

    // Every truncation and every zeroed length is refused.
    for (size_t n = 0; n < live.size(); ++n)
        assert(uac1::discover(live.data(), n, c) != uac2::Error::None);
    for (size_t off = 0; off < live.size(); off += live[off]) {
        auto broken = live;
        broken[off] = 0;
        assert(uac1::discover(broken.data(), broken.size(), c) != uac2::Error::None);
    }

    // Locate the two Format Type I descriptors and the two EP_GENERAL
    // descriptors by content, so the edits below do not depend on offsets.
    std::vector<size_t> formats, endpointGenerals, headers;
    for (size_t off = 9; off < live.size(); off += live[off]) {
        if (live[off + 1] == 0x24 && live[off] == 11 && live[off + 2] == 2 && live[off + 3] == 1)
            formats.push_back(off);
        if (live[off + 1] == 0x25)
            endpointGenerals.push_back(off);
        if (live[off + 1] == 0x24 && live[off + 2] == 1 && live[off] == 10)
            headers.push_back(off);
    }
    assert(formats.size() == 2 && endpointGenerals.size() == 2 && headers.size() == 1);

    // A 44.1 kHz-only alternate parses but does not support 48 kHz.
    {
        auto b = live;
        b[formats[0] + 8] = 0x44;
        b[formats[0] + 9] = 0xac;
        b[formats[0] + 10] = 0x00;
        assert(uac1::discover(b.data(), b.size(), c) == uac2::Error::None);
        const auto p = uac2::pairs(c.topology)[0];
        assert(!uac1::alternateFor(c, p.playback)->supports(48000));
        assert(uac1::alternateFor(c, p.capture)->supports(48000));
    }
    // A continuous range: the 11-byte discrete descriptor becomes 14 bytes.
    {
        auto b = live;
        const uint8_t range[14] = {14, 0x24, 2, 1, 2, 2, 16, 0, 0x44, 0xac, 0, 0x00, 0x77, 0x01};
        b.erase(b.begin() + long(formats[1]), b.begin() + long(formats[1]) + 11);
        b.insert(b.begin() + long(formats[1]), range, range + 14);
        b[2] = uint8_t(b.size());
        b[3] = uint8_t(b.size() >> 8);
        assert(uac1::discover(b.data(), b.size(), c) == uac2::Error::None);
        const auto* a = uac1::alternateFor(c, uac2::pairs(c.topology)[0].capture);
        assert(a && a->continuous && a->lower == 44100 && a->upper == 96000);
        assert(a->supports(48000) && a->supports(96000) && !a->supports(192000));
    }
    // The sampling-frequency control bit is carried per endpoint.
    {
        auto b = live;
        b[endpointGenerals[0] + 3] = 0x01;
        b[endpointGenerals[1] + 3] = 0x80;
        assert(uac1::discover(b.data(), b.size(), c) == uac2::Error::None);
        const auto p = uac2::pairs(c.topology)[0];
        assert(uac1::alternateFor(c, p.playback)->rateControl);
        assert(!uac1::alternateFor(c, p.playback)->maxPacketsOnly);
        assert(!uac1::alternateFor(c, p.capture)->rateControl);
        assert(uac1::alternateFor(c, p.capture)->maxPacketsOnly);
    }
    // A UAC2 header in a UAC1 interface is not a UAC1 function.
    {
        auto b = live;
        b[headers[0] + 4] = 0x02;
        assert(uac1::discover(b.data(), b.size(), c) == uac2::Error::Descriptor);
    }
    // An unrelated interface (a HID keyboard after the audio function) is
    // skipped, as on a composite device.
    {
        auto b = live;
        const uint8_t hid[] = {9, 4, 4,    0,  1, 3, 1, 1,    0, 9, 0x21, 0x11, 0x01,
                               0, 1, 0x22, 63, 0, 7, 5, 0x84, 3, 8, 0,    1};
        b.insert(b.end(), hid, hid + sizeof hid);
        b[2] = uint8_t(b.size());
        b[3] = uint8_t(b.size() >> 8);
        b[4] = 4;
        assert(uac1::discover(b.data(), b.size(), c) == uac2::Error::None);
        checkMomix(c);
    }
    // Mono 16-bit capture decodes through the shared codec: channel 0 is the
    // guitar, and a stereo 16-bit playback frame round-trips.
    uac2::Codec capture, playback;
    assert(uac1::discover(live.data(), live.size(), c) == uac2::Error::None);
    const auto pair = uac2::pairs(c.topology)[0];
    assert(capture.configure(pair.capture.format) && playback.configure(pair.playback.format));
    assert(!capture.nativeMono && !capture.packedPcm());
    const uint8_t frame[4] = {0x00, 0x40, 0x00, 0xc0}; // left 0x4000, right -0x4000
    assert(capture.capture(frame, 0) == 0x40000000);
    assert(capture.capture(frame, 1) == -0x40000000);
    const uint64_t stereo = uint32_t(0x12340000) | (uint64_t(uint32_t(0xabcd0000)) << 32);
    uint8_t out[4] = {};
    playback.playback(out, &stereo, 1);
    assert(out[0] == 0x34 && out[1] == 0x12 && out[2] == 0xcd && out[3] == 0xab);
    puts("Generic UAC1: MOMIX CAB topology, renumbering, truncations, rate lists, endpoint "
         "controls, composite interfaces and 16-bit conversion passed");
}
