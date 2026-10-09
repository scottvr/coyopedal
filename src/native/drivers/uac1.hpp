#pragma once
#include "uac2.hpp"
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <algorithm>

// USB Audio 1.0 descriptor discovery. It fills the same topology the UAC2
// module does, so pairing, the Full Speed capacity check, the feature-unit
// handling and the PCM codecs are shared; what differs is the clock model.
// UAC1 has no clock entities and no way to ask which clock a stream uses, so
// every stream of a function is given clock 0: a single-function UAC1
// interface runs both directions from the one crystal it has. The rate is
// negotiated per endpoint, and only when the endpoint says it can be.
// Like uac2.hpp: no device identities, no OS dependencies, no allocation at
// streaming time.
namespace uac1 {
using uac2::Error;
using uac2::le16;
inline uint32_t le24(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}
// What UAC1 says about an alternate that the shared Stream cannot carry: the
// rates its Format Type I descriptor lists, and the class-specific endpoint
// attributes. `rateControl` is bit D0 of those attributes; a device that
// clears it stalls the sampling-frequency request, and a device that runs
// one rate has no need of it.
struct Alternate {
    uint8_t interface = 0, alternate = 0;
    bool continuous = false, rateControl = false, maxPacketsOnly = false;
    uint32_t lower = 0, upper = 0;
    std::vector<uint32_t> rates;
    bool supports(uint32_t rate) const {
        if (continuous)
            return rate >= lower && rate <= upper;
        return std::find(rates.begin(), rates.end(), rate) != rates.end();
    }
};
struct Configuration {
    uac2::Configuration topology;
    std::vector<Alternate> alternates;
};
inline const Alternate* alternateFor(const Configuration& c, const uac2::Stream& s) {
    for (const auto& a : c.alternates)
        if (a.interface == s.interface && a.alternate == s.alternate)
            return &a;
    return nullptr;
}
inline Error discover(const uint8_t* b, size_t size, Configuration& result) {
    result = {};
    if (!b || size < 9 || b[0] != 9 || b[1] != 2 || le16(b + 2) != size || !b[5])
        return Error::Truncated;
    uac2::Configuration c;
    c.value = b[5];
    // An AudioControl interface's header names the streaming interfaces it
    // owns; that list, not an interface association, is the function.
    struct Function {
        uint8_t control;
        std::vector<uint8_t> interfaces;
    };
    std::vector<Function> functions;
    // Pass 1 checks every length once, so pass 2 can index freely, and rejects
    // an interface/alternate pair that appears twice.
    std::vector<uint16_t> alternates;
    for (size_t off = 9; off < size;) {
        if (size - off < 2 || b[off] < 2 || b[off] > size - off)
            return Error::Truncated;
        const auto* d = b + off;
        if (d[1] == 4) {
            if (d[0] < 9)
                return Error::Descriptor;
            uint16_t key = uint16_t(d[2]) * 256 + d[3];
            if (std::find(alternates.begin(), alternates.end(), key) != alternates.end())
                return Error::Duplicate;
            alternates.push_back(key);
        }
        off += d[0];
    }
    // An isochronous endpoint of a streaming alternate, held until the
    // alternate is complete: UAC1 marks the data endpoint by the class-specific
    // endpoint descriptor that follows it, and the optional feedback endpoint
    // by what it is not.
    struct Candidate {
        uac2::Endpoint endpoint;
        bool classSpecific = false, rateControl = false, maxPacketsOnly = false;
    };
    std::vector<std::vector<Candidate>> candidates;
    std::vector<Alternate> alts;
    int stream = -1;
    int ac = -1;
    for (size_t off = 9; off < size;) {
        const auto* d = b + off;
        unsigned n = d[0];
        if (d[1] == 4) {
            stream = ac = -1;
            // Audio class 1, protocol 0: UAC2 and UAC3 use 0x20 and 0x30.
            const bool audio = d[5] == 1 && d[7] == 0;
            if (audio && d[6] == 1 && d[3] == 0)
                ac = d[2];
            else if (audio && d[6] == 2 && d[3] != 0) {
                uac2::Stream s;
                s.interface = d[2];
                s.alternate = d[3];
                s.expectedEndpoints = d[4];
                c.streams.push_back(s);
                candidates.emplace_back();
                Alternate alt;
                alt.interface = d[2];
                alt.alternate = d[3];
                alts.push_back(alt);
                stream = int(c.streams.size() - 1);
            }
        } else if (d[1] == 0x24 && ac >= 0) {
            if (n < 3)
                return Error::Descriptor;
            if (d[2] == 1) {
                // HEADER: bcdADC, wTotalLength, bInCollection, baInterfaceNr[].
                if (n < 8 || le16(d + 3) != 0x0100 || n != 8u + d[7])
                    return Error::Descriptor;
                for (const auto& f : functions)
                    if (f.control == ac)
                        return Error::Duplicate;
                functions.push_back({uint8_t(ac), std::vector<uint8_t>(d + 8, d + n)});
            } else if (d[2] == 2 || d[2] == 3) {
                // INPUT_TERMINAL carries its channel count; OUTPUT_TERMINAL its
                // source. Neither names a clock: that field stays 0.
                const bool in = d[2] == 2;
                if (n < (in ? 12u : 9u) || !d[3])
                    return Error::Descriptor;
                c.terminals.push_back(
                    {uint8_t(ac), d[3], 0, d[2], uint8_t(in ? d[7] : 0), le16(d + 4)});
            } else if (d[2] == 6) {
                // FEATURE_UNIT: bUnitID, bSourceID, bControlSize, then one
                // bmaControls per channel plus one for master, then iFeature.
                if (n < 8 || !d[3] || !d[5] || (n - 7u) % d[5] != 0u)
                    return Error::Descriptor;
                const unsigned entries = (n - 7u) / d[5];
                c.features.push_back(
                    {uint8_t(ac), d[3], d[4], uint8_t(entries > 0 ? entries - 1 : 0)});
            }
            // Mixer, selector, processing and extension units are not
            // traversed: a stream is linked to its USB terminal directly.
        } else if (d[1] == 0x24 && stream >= 0) {
            if (n < 3)
                return Error::Descriptor;
            auto& s = c.streams[stream];
            if (d[2] == 1) {
                // AS_GENERAL: bTerminalLink, bDelay, wFormatTag.
                if (n < 7 || s.general)
                    return Error::Descriptor;
                s.general = true;
                s.terminal = d[3];
                const uint16_t tag = le16(d + 5);
                if (tag == 1 || tag == 2 || tag == 3) {
                    s.pcm = true;
                    s.format.encoding = tag == 1   ? uac2::Encoding::SignedPCM
                                        : tag == 2 ? uac2::Encoding::UnsignedPCM8
                                                   : uac2::Encoding::Float32;
                }
            } else if (d[2] == 2) {
                // FORMAT_TYPE: only Type I is PCM. bNrChannels lives here in
                // UAC1, with bSubframeSize, bBitResolution and the rates.
                if (n < 4 || s.typed)
                    return Error::Descriptor;
                s.typed = true;
                if (d[3] != 1) {
                    s.pcm = false;
                } else {
                    if (n < 8)
                        return Error::Descriptor;
                    s.format.channels = d[4];
                    s.format.subslot = d[5];
                    s.format.bits = d[6];
                    auto& a = alts[stream];
                    if (d[7] == 0) {
                        if (n != 14)
                            return Error::Descriptor;
                        a.continuous = true;
                        a.lower = le24(d + 8);
                        a.upper = le24(d + 11);
                        if (!a.lower || a.lower > a.upper)
                            return Error::Descriptor;
                    } else {
                        if (n != 8u + 3u * d[7])
                            return Error::Descriptor;
                        for (unsigned i = 0; i < d[7]; ++i) {
                            const uint32_t rate = le24(d + 8 + 3 * i);
                            if (!rate)
                                return Error::Descriptor;
                            a.rates.push_back(rate);
                        }
                    }
                }
            }
        } else if (d[1] == 5 && stream >= 0) {
            if (n < 7)
                return Error::Descriptor;
            auto& s = c.streams[stream];
            ++s.seenEndpoints;
            if ((d[3] & 3) == 1) {
                const uint16_t w = le16(d + 4);
                const unsigned transactions = 1 + ((w >> 11) & 3);
                if (!(d[2] & 15) || (d[2] & 0x70) || !d[6] || d[6] > 16 || transactions > 3 ||
                    (w & 0xe000))
                    return Error::Descriptor;
                Candidate candidate;
                candidate.endpoint = {d[2], d[6], uint8_t((d[3] >> 2) & 3),
                                      uint8_t((d[3] >> 4) & 3),
                                      uint32_t((w & 2047) * transactions)};
                if (!candidate.endpoint.maxBytes)
                    return Error::Descriptor;
                candidates[stream].push_back(candidate);
            }
        } else if (d[1] == 0x25 && stream >= 0) {
            // EP_GENERAL belongs to the endpoint just described: bmAttributes
            // (D0 sampling frequency, D7 MaxPacketsOnly), lock delay units/value.
            if (n < 7 || d[2] != 1 || candidates[stream].empty() ||
                candidates[stream].back().classSpecific)
                return Error::Descriptor;
            auto& candidate = candidates[stream].back();
            candidate.classSpecific = true;
            candidate.rateControl = d[3] & 1;
            candidate.maxPacketsOnly = d[3] & 0x80;
        }
        off += n;
    }
    if (functions.empty())
        return Error::NoPCM;
    for (size_t i = 0; i < c.terminals.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (c.terminals[i].control == c.terminals[j].control &&
                c.terminals[i].id == c.terminals[j].id)
                return Error::Duplicate;
    for (const auto& f : functions)
        c.functions.push_back({f.control, 1, f.control, true});
    std::vector<uac2::Stream> usable;
    std::vector<Alternate> usableAlternates;
    for (size_t index = 0; index < c.streams.size(); ++index) {
        auto s = c.streams[index];
        auto a = alts[index];
        if (!s.pcm)
            continue;
        // Which function owns this interface. A header that omits its own
        // streaming interface is tolerated only when there is nothing else the
        // interface could belong to.
        const Function* owner = nullptr;
        for (const auto& f : functions)
            if (std::find(f.interfaces.begin(), f.interfaces.end(), s.interface) !=
                f.interfaces.end()) {
                if (owner)
                    return Error::Topology;
                owner = &f;
            }
        if (!owner && functions.size() == 1)
            owner = &functions[0];
        if (!owner)
            return Error::Topology;
        s.control = owner->control;
        // The data endpoint is the one with the class-specific descriptor; a
        // lone isochronous endpoint without one is accepted as the data
        // endpoint. Whatever remains must be a feedback endpoint.
        const Candidate* data = nullptr;
        for (const auto& candidate : candidates[index])
            if (candidate.classSpecific) {
                if (data)
                    return Error::Descriptor;
                data = &candidate;
            }
        if (!data && candidates[index].size() == 1)
            data = &candidates[index][0];
        if (!s.general || !s.typed || !data || s.seenEndpoints != s.expectedEndpoints)
            return Error::Descriptor;
        s.data = data->endpoint;
        a.rateControl = data->rateControl;
        a.maxPacketsOnly = data->maxPacketsOnly;
        for (const auto& candidate : candidates[index]) {
            if (&candidate == data)
                continue;
            if (s.feedback.address || !(candidate.endpoint.address & 0x80) ||
                (candidate.endpoint.maxBytes != 3 && candidate.endpoint.maxBytes != 4))
                return Error::Descriptor;
            s.feedback = candidate.endpoint;
            s.feedback.usage = 1;
        }
        const auto& f = s.format;
        if (!f.channels || !f.subslot || f.subslot > 4 || !f.bits || f.bits > f.subslot * 8)
            return Error::Descriptor;
        if ((f.encoding == uac2::Encoding::UnsignedPCM8 && (f.bits != 8 || f.subslot != 1)) ||
            (f.encoding == uac2::Encoding::Float32 && (f.bits != 32 || f.subslot != 4)))
            continue;
        if (a.continuous ? a.lower > a.upper : a.rates.empty())
            return Error::Descriptor;
        bool linked = false;
        for (const auto& t : c.terminals)
            if (t.control == s.control && t.id == s.terminal) {
                if (t.kind != 0x0101 || (s.input() ? t.type != 3 : t.type != 2))
                    return Error::Topology;
                linked = true;
            }
        if (!linked)
            return Error::Topology;
        if (s.input() && s.feedback.address)
            return Error::Topology;
        if (s.data.maxBytes < f.frameBytes())
            return Error::Capacity;
        s.clock = 0;
        usable.push_back(s);
        usableAlternates.push_back(a);
    }
    c.streams = std::move(usable);
    if (c.streams.empty())
        return Error::NoPCM;
    result.topology = std::move(c);
    result.alternates = std::move(usableAlternates);
    return Error::None;
}
} // namespace uac1
