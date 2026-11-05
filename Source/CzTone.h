#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Casio CZ-1 tone SysEx, as sent by the (emulated) CZ-1 on "MIDI SAVE":
//
//   F0 44 00 00 7n 21 pp <288 nibbles, low nibble first> F7
//
// = one tone of 144 bytes (pp = internal slot 0-63).  The first 128 bytes follow
// the CZ-101 tone layout (PFLAG, detune, vibrato, two DCO/DCA/DCW lines); the last
// 16 bytes are the ASCII tone name, space padded.  Observed in a real dump:
// 64 tone messages plus 59 "22 pp" messages of 20 bytes (operation memory).
//
// Fields not exposed here are kept as raw bytes, so a decode/encode round trip is
// always byte-exact.
namespace cz
{
constexpr int toneSize = 144;
constexpr int bodySize = 128;
constexpr int nameSize = 16;

struct Tone
{
    std::array<std::uint8_t, toneSize> data {};

    // ---- name (bytes 128..143) ----
    std::string name() const
    {
        std::string s (data.begin() + bodySize, data.end());
        while (! s.empty() && (s.back() == ' ' || s.back() == '\0'))
            s.pop_back();
        std::size_t start = 0;
        while (start < s.size() && s[start] == ' ')
            ++start;
        return s.substr (start);
    }

    // The firmware centres short names, so do the same.
    void setName (const std::string& text)
    {
        std::string s = text.substr (0, nameSize);
        const auto pad = static_cast<std::size_t> (nameSize) - s.size();
        s = std::string (pad / 2, ' ') + s + std::string (pad - pad / 2, ' ');
        for (int i = 0; i < nameSize; ++i)
            data[static_cast<std::size_t> (bodySize + i)] = static_cast<std::uint8_t> (s[static_cast<std::size_t> (i)]);
    }

    // ---- PFLAG (byte 0): bits 0-1 line select, bits 2-3 octave ----
    // lineSelect: 0 = 1, 1 = 2, 2 = 1+1', 3 = 1+2'
    int lineSelect() const { return data[0] & 3; }
    void setLineSelect (int v) { data[0] = static_cast<std::uint8_t> ((data[0] & ~3) | (v & 3)); }

    // octave: 0 = normal, 1 = +1, 2 = -1
    int octave() const { return (data[0] >> 2) & 3; }
    void setOctave (int v) { data[0] = static_cast<std::uint8_t> ((data[0] & ~0x0c) | ((v & 3) << 2)); }
};


// ---- Envelopes -------------------------------------------------------------
// Layout (CZ-101 style, confirmed on the CZ-1 against the panel LCD): per line
// (line 1 at offset 0, line 2 at +57) the body holds three envelopes, each
//   [end byte][8 x (rate byte, level byte)]
// at end-byte offsets DCA 20, DCW 37, DCO 54.  The low nibble of the end byte is
// the last step (0-7); the high nibble is kept untouched.  Bit 0x80 of a level
// byte marks the sustain step; bit 0x80 of a rate byte marks "level goes down"
// (level < previous level; free on a last step that ends at level 0).
//   DCA  rate = ceil((b)    * 99/119)   level = b - 28 (0 stays 0)
//   DCW  rate = ceil((b-8)  * 99/119)   level = ceil(b * 99/127)
//   DCO  rate = ceil(b      * 99/127)   level = b (<=63) or b - 4
// Verified: every step of the 64 factory tones round-trips byte for byte.
enum class EnvKind { dca = 0, dcw = 1, dco = 2 };

struct EnvStep { int rate = 0; int level = 0; };

struct Envelope
{
    int end = 0;          // last active step, 0-7
    int sustain = -1;     // sustain step 0-7, or -1
    std::array<EnvStep, 8> steps {};
};

namespace detail
{
inline int endOffset (EnvKind kind, int line) { return (kind == EnvKind::dca ? 20 : kind == EnvKind::dcw ? 37 : 54) + 57 * line; }

inline int decodeRate (EnvKind kind, int b)
{
    switch (kind)
    {
        case EnvKind::dca: return static_cast<int> (std::ceil (b * 99.0 / 119.0));
        case EnvKind::dcw: return static_cast<int> (std::ceil (std::max (0, b - 8) * 99.0 / 119.0));
        default:           return static_cast<int> (std::ceil (b * 99.0 / 127.0));
    }
}

inline int encodeRate (EnvKind kind, int r)
{
    r = std::clamp (r, 0, 99);
    switch (kind)
    {
        case EnvKind::dca: return static_cast<int> (std::floor (r * 119.0 / 99.0));
        case EnvKind::dcw: return static_cast<int> (std::floor (r * 119.0 / 99.0)) + 8;
        default:           return static_cast<int> (std::floor (r * 127.0 / 99.0));
    }
}

inline int decodeLevel (EnvKind kind, int b)
{
    switch (kind)
    {
        case EnvKind::dca: return b == 0 ? 0 : std::max (0, b - 28);
        case EnvKind::dcw: return static_cast<int> (std::ceil (b * 99.0 / 127.0));
        default:           return b <= 63 ? b : b - 4;
    }
}

inline int encodeLevel (EnvKind kind, int l)
{
    l = std::clamp (l, 0, 99);
    switch (kind)
    {
        case EnvKind::dca: return l == 0 ? 0 : l + 28;
        case EnvKind::dcw: return static_cast<int> (std::floor (l * 127.0 / 99.0));
        default:           return l <= 63 ? l : l + 4;
    }
}
} // namespace detail

inline Envelope getEnvelope (const Tone& tone, EnvKind kind, int line)
{
    Envelope env;
    const auto e = static_cast<std::size_t> (detail::endOffset (kind, line));
    env.end = tone.data[e] & 0x0f;
    if (env.end > 7)
        env.end = 7;
    for (std::size_t i = 0; i < 8; ++i)
    {
        const int rateByte = tone.data[e + 1 + 2 * i];
        const int levelByte = tone.data[e + 2 + 2 * i];
        env.steps[i].rate = detail::decodeRate (kind, rateByte & 0x7f);
        env.steps[i].level = detail::decodeLevel (kind, levelByte & 0x7f);
        if ((levelByte & 0x80) != 0 && static_cast<int> (i) <= env.end && env.sustain < 0)
            env.sustain = static_cast<int> (i);
    }
    return env;
}

// Writes the active steps (0..end) and the end byte's low nibble; steps after `end`
// and the end byte's high nibble are left as they were.
inline void setEnvelope (Tone& tone, EnvKind kind, int line, const Envelope& env)
{
    const auto e = static_cast<std::size_t> (detail::endOffset (kind, line));
    const int end = std::clamp (env.end, 0, 7);
    tone.data[e] = static_cast<std::uint8_t> ((tone.data[e] & 0xf0) | end);

    int previous = 0;
    for (int i = 0; i <= end; ++i)
    {
        const auto& step = env.steps[static_cast<std::size_t> (i)];
        int rateByte = detail::encodeRate (kind, step.rate);
        int levelByte = detail::encodeLevel (kind, step.level);
        // "Level goes down" flag.  The factory data follows level < previous level on every
        // step except the last one when it ends at level 0, where the flag is free: keep it.
        const auto rateIndex = e + 1 + 2 * static_cast<std::size_t> (i);
        if (i == end && step.level == 0)
            rateByte |= tone.data[rateIndex] & 0x80;
        else if (step.level < previous)
            rateByte |= 0x80;
        if (i == env.sustain)
            levelByte |= 0x80;
        tone.data[rateIndex] = static_cast<std::uint8_t> (rateByte);
        tone.data[e + 2 + 2 * static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (levelByte);
        previous = step.level;
    }
}

// ---- Other parameters ---------------------------------------------------------
// Decoded with the firmware as oracle (panel taps + LCD + edit buffer) and checked
// against the 64 factory tones (tests/cz_tone_test.cpp).  Unknown bits are preserved.
//
//   byte 1      detune polarity (0 / 1; 0 = "+" per the CZ-101 doc, not checked on the LCD)
//   byte 2      detune fine 0-60: byte = 4 * (f + (f-1)/15) (a gap every 15 steps)
//   byte 3      detune octave*12 + note (OCT 0-3, NOTE 0-11)
//   byte 4      vibrato wave 1-4 -> 0x08, 0x04, 0x20, 0x02
//   bytes 5-7   vibrato delay: (v, lo, hi) from a 100-entry table, same for
//   bytes 8-10  rate and bytes 11-13 depth
//   bytes 14-15 waveforms (+71-72 on line 2): byte14 bits 7-5 first wave code,
//               bit 1 "second wave present", bits 4-2 second wave code; codes
//               0,1,2,4,5,6 = waves 1..5 and the resonance waves 6-8, whose variant
//               (1-3) is byte15 bits 7-6, shared by both waves.  Other bits kept.
//   bytes 16-19 key follow (+57): DCA range = byte16 low nibble, byte17 = table;
//               DCW range = byte18, byte19 = table
namespace detail
{
inline constexpr std::uint16_t vibDelayTable[100] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 33, 35, 37, 39, 41, 43, 45, 47,
    49, 51, 53, 55, 57, 59, 61, 63, 67, 71,
    75, 79, 83, 87, 91, 95, 99, 103, 107, 111,
    115, 119, 123, 127, 135, 143, 151, 159, 167, 175,
    183, 191, 199, 207, 215, 223, 231, 239, 247, 255,
    271, 287, 303, 319, 335, 351, 367, 383, 399, 415,
    431, 447, 463, 479, 495, 511, 543, 575, 607, 639,
};
inline constexpr std::uint16_t vibRateTable[100] = {
    32, 64, 96, 128, 160, 192, 224, 256, 288, 320,
    352, 384, 416, 448, 480, 512, 544, 576, 608, 640,
    672, 704, 736, 768, 800, 832, 864, 896, 928, 960,
    992, 1056, 1120, 1184, 1248, 1312, 1376, 1440, 1504, 1568,
    1632, 1696, 1760, 1824, 1888, 1952, 2016, 2144, 2272, 2400,
    2528, 2656, 2784, 2912, 3040, 3168, 3296, 3424, 3552, 3680,
    3808, 3936, 4064, 4320, 4576, 4832, 5088, 5344, 5600, 5856,
    6112, 6368, 6624, 6880, 7136, 7392, 7648, 7904, 8160, 8672,
    9184, 9696, 10208, 10720, 11232, 11744, 12256, 12768, 13280, 13792,
    14304, 14816, 15328, 15840, 16352, 17376, 18400, 19424, 20448, 21472,
};
inline constexpr std::uint16_t vibDepthTable[100] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 33, 35, 37, 39, 41, 43, 45, 47, 49,
    51, 53, 55, 57, 59, 61, 63, 67, 71, 75,
    79, 83, 87, 91, 95, 99, 103, 107, 111, 115,
    119, 123, 127, 135, 143, 151, 159, 167, 175, 183,
    191, 199, 207, 215, 223, 231, 239, 247, 255, 271,
    287, 303, 319, 335, 351, 367, 383, 399, 415, 431,
    447, 463, 479, 495, 511, 543, 575, 607, 639, 768,
};

constexpr std::uint8_t kfDcaTable[10] = { 0x00, 0x08, 0x11, 0x1a, 0x24, 0x2f, 0x3a, 0x45, 0x52, 0x5f };
constexpr std::uint8_t kfDcwTable[10] = { 0x00, 0x19, 0x33, 0x4e, 0x6a, 0x86, 0xa3, 0xc1, 0xdf, 0xff };
constexpr std::uint8_t vibWaveBytes[4] = { 0x08, 0x04, 0x20, 0x02 };
constexpr int waveCode[6] = { 0, 1, 2, 4, 5, 6 };   // waves 1-5, then 6-8 share code 6

inline const std::uint16_t* vibTable (int which)
{
    return which == 0 ? vibDelayTable : which == 1 ? vibRateTable : vibDepthTable;
}
} // namespace detail

// ---- detune ----
inline int detunePolarity (const Tone& t) { return t.data[1] & 1; }
inline void setDetunePolarity (Tone& t, int v) { t.data[1] = static_cast<std::uint8_t> ((t.data[1] & ~1) | (v & 1)); }

inline int detuneFine (const Tone& t)
{
    const int g = t.data[2] >> 2;
    return g <= 15 ? g : g - (g - 1) / 16;
}
inline void setDetuneFine (Tone& t, int f)
{
    f = std::clamp (f, 0, 60);
    const int g = f + (f > 0 ? (f - 1) / 15 : 0);
    t.data[2] = static_cast<std::uint8_t> ((t.data[2] & 3) | (g << 2));
}

inline int detuneOctave (const Tone& t) { return std::min (3, t.data[3] / 12); }
inline int detuneNote (const Tone& t) { return std::min (11, t.data[3] % 12); }
inline void setDetuneOctaveNote (Tone& t, int octave, int note)
{
    t.data[3] = static_cast<std::uint8_t> (std::clamp (octave, 0, 3) * 12 + std::clamp (note, 0, 11));
}

// ---- vibrato ----
inline int vibratoWave (const Tone& t)
{
    for (int i = 0; i < 4; ++i)
        if (t.data[4] == detail::vibWaveBytes[i])
            return i + 1;
    return 1;
}
inline void setVibratoWave (Tone& t, int w) { t.data[4] = detail::vibWaveBytes[std::clamp (w, 1, 4) - 1]; }

// which: 0 delay, 1 rate, 2 depth.  Returns 0-99 (nearest entry when the bytes are unknown).
inline int vibratoValue (const Tone& t, int which)
{
    const auto o = static_cast<std::size_t> (5 + 3 * which);
    const int word = t.data[o + 1] | (t.data[o + 2] << 8);
    const auto* table = detail::vibTable (which);
    int best = 0;
    for (int v = 0; v < 100; ++v)
        if (std::abs (table[v] - word) < std::abs (table[best] - word))
            best = v;
    return best;
}
inline void setVibratoValue (Tone& t, int which, int v)
{
    v = std::clamp (v, 0, 99);
    const auto o = static_cast<std::size_t> (5 + 3 * which);
    const auto word = detail::vibTable (which)[v];
    t.data[o] = static_cast<std::uint8_t> (v);
    t.data[o + 1] = static_cast<std::uint8_t> (word & 0xff);
    t.data[o + 2] = static_cast<std::uint8_t> (word >> 8);
}

// ---- waveforms ----
inline int waveVariant (const Tone& t, int line) { return t.data[static_cast<std::size_t> (15 + 57 * line)] >> 6; }

// slot 0 = first wave (1-8), slot 1 = second wave (0 = none, 1-8)
inline int waveform (const Tone& t, int line, int slot)
{
    const auto b14 = t.data[static_cast<std::size_t> (14 + 57 * line)];
    int code, present = 1;
    if (slot == 0)
        code = b14 >> 5;
    else
    {
        present = (b14 >> 1) & 1;
        code = (b14 >> 2) & 7;
    }
    if (! present)
        return 0;
    int w = 0;
    for (int i = 0; i < 6; ++i)
        if (detail::waveCode[i] == code)
            w = i + 1;
    if (w == 6)
        w += std::clamp (waveVariant (t, line), 1, 3) - 1;
    return w;
}

inline void setWaveform (Tone& t, int line, int slot, int w)
{
    auto& b14 = t.data[static_cast<std::size_t> (14 + 57 * line)];
    auto& b15 = t.data[static_cast<std::size_t> (15 + 57 * line)];
    w = std::clamp (w, slot == 0 ? 1 : 0, 8);
    const int idx = std::min (w, 6);
    const int code = w == 0 ? 0 : detail::waveCode[idx - 1];
    if (slot == 0)
        b14 = static_cast<std::uint8_t> ((b14 & 0x1f) | (code << 5));
    else
        b14 = static_cast<std::uint8_t> ((b14 & ~0x1e) | (code << 2) | (w != 0 ? 2 : 0));
    if (w >= 6)
        b15 = static_cast<std::uint8_t> ((b15 & 0x3f) | ((w - 5) << 6));
}

// ---- key follow: kind 0 = DCA, 1 = DCW; range 0-9 ----
inline int keyFollow (const Tone& t, int line, int kind)
{
    const auto o = static_cast<std::size_t> (16 + 2 * kind + 57 * line);
    return std::min (9, t.data[o] & 15);
}
inline void setKeyFollow (Tone& t, int line, int kind, int range)
{
    range = std::clamp (range, 0, 9);
    const auto o = static_cast<std::size_t> (16 + 2 * kind + 57 * line);
    t.data[o] = static_cast<std::uint8_t> ((t.data[o] & 0xf0) | range);
    t.data[o + 1] = kind == 0 ? detail::kfDcaTable[range] : detail::kfDcwTable[range];
}

// A flat, UI-friendly view of all the simple (integer) parameters above.
struct ParamInfo
{
    const char* name;
    int min, max;
    int (*get) (const Tone&);
    void (*set) (Tone&, int);
};

inline const std::vector<ParamInfo>& simpleParams()
{
    static const std::vector<ParamInfo> list = {
        { "Detune +/-", 0, 1, [] (const Tone& t) { return detunePolarity (t); }, [] (Tone& t, int v) { setDetunePolarity (t, v); } },
        { "Detune OCT", 0, 3, [] (const Tone& t) { return detuneOctave (t); }, [] (Tone& t, int v) { setDetuneOctaveNote (t, v, detuneNote (t)); } },
        { "Detune NOTE", 0, 11, [] (const Tone& t) { return detuneNote (t); }, [] (Tone& t, int v) { setDetuneOctaveNote (t, detuneOctave (t), v); } },
        { "Detune FINE", 0, 60, [] (const Tone& t) { return detuneFine (t); }, [] (Tone& t, int v) { setDetuneFine (t, v); } },
        { "Vibrato wave", 1, 4, [] (const Tone& t) { return vibratoWave (t); }, [] (Tone& t, int v) { setVibratoWave (t, v); } },
        { "Vibrato delay", 0, 99, [] (const Tone& t) { return vibratoValue (t, 0); }, [] (Tone& t, int v) { setVibratoValue (t, 0, v); } },
        { "Vibrato rate", 0, 99, [] (const Tone& t) { return vibratoValue (t, 1); }, [] (Tone& t, int v) { setVibratoValue (t, 1, v); } },
        { "Vibrato depth", 0, 99, [] (const Tone& t) { return vibratoValue (t, 2); }, [] (Tone& t, int v) { setVibratoValue (t, 2, v); } },
        { "L1 wave 1", 1, 8, [] (const Tone& t) { return waveform (t, 0, 0); }, [] (Tone& t, int v) { setWaveform (t, 0, 0, v); } },
        { "L1 wave 2", 0, 8, [] (const Tone& t) { return waveform (t, 0, 1); }, [] (Tone& t, int v) { setWaveform (t, 0, 1, v); } },
        { "L1 DCA key follow", 0, 9, [] (const Tone& t) { return keyFollow (t, 0, 0); }, [] (Tone& t, int v) { setKeyFollow (t, 0, 0, v); } },
        { "L1 DCW key follow", 0, 9, [] (const Tone& t) { return keyFollow (t, 0, 1); }, [] (Tone& t, int v) { setKeyFollow (t, 0, 1, v); } },
        { "L2 wave 1", 1, 8, [] (const Tone& t) { return waveform (t, 1, 0); }, [] (Tone& t, int v) { setWaveform (t, 1, 0, v); } },
        { "L2 wave 2", 0, 8, [] (const Tone& t) { return waveform (t, 1, 1); }, [] (Tone& t, int v) { setWaveform (t, 1, 1, v); } },
        { "L2 DCA key follow", 0, 9, [] (const Tone& t) { return keyFollow (t, 1, 0); }, [] (Tone& t, int v) { setKeyFollow (t, 1, 0, v); } },
        { "L2 DCW key follow", 0, 9, [] (const Tone& t) { return keyFollow (t, 1, 1); }, [] (Tone& t, int v) { setKeyFollow (t, 1, 1, v); } },
    };
    return list;
}

inline std::vector<std::uint8_t> encodeTone (const Tone& tone, int slot, int channel = 0)
{
    std::vector<std::uint8_t> out { 0xF0, 0x44, 0x00, 0x00, static_cast<std::uint8_t> (0x70 + (channel & 15)), 0x21,
                                    static_cast<std::uint8_t> (slot & 0x7f) };
    for (auto b : tone.data)
    {
        out.push_back (b & 0x0f);
        out.push_back (static_cast<std::uint8_t> (b >> 4));
    }
    out.push_back (0xF7);
    return out;
}

struct Dump
{
    std::array<Tone, 64> tones {};
    std::array<bool, 64> present {};
    // Messages other than tone data (e.g. "22 pp" operation memory), kept verbatim.
    std::vector<std::vector<std::uint8_t>> others;
};

// Splits a raw capture into SysEx messages and decodes the tone ones.
inline Dump parseDump (const std::vector<std::uint8_t>& bytes)
{
    Dump dump;
    std::vector<std::uint8_t> current;
    bool inside = false;
    for (auto b : bytes)
    {
        if (b == 0xF0) { current = { b }; inside = true; continue; }
        if (! inside) continue;
        current.push_back (b);
        if (b != 0xF7) continue;
        inside = false;

        const bool isTone = current.size() == 7 + toneSize * 2 + 1 && current[1] == 0x44 && current[5] == 0x21
                            && current[6] < 64;
        if (! isTone) { dump.others.push_back (current); continue; }

        auto& tone = dump.tones[current[6]];
        for (int i = 0; i < toneSize; ++i)
            tone.data[static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (
                (current[static_cast<std::size_t> (7 + i * 2)] & 0x0f) | ((current[static_cast<std::size_t> (8 + i * 2)] & 0x0f) << 4));
        dump.present[current[6]] = true;
    }
    return dump;
}
} // namespace cz
