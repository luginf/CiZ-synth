// Round-trip test: parse a real CZ-1 dump, re-encode every tone, compare bytes.
//   cz_tone_test <dump.bin>
#include "../Source/CzTone.h"

#include <cstdio>
#include <fstream>
#include <iterator>

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: cz_tone_test <dump.bin>\n"); return 2; }

    std::ifstream in (argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto dump = cz::parseDump (bytes);

    int tones = 0, mismatches = 0;
    for (int slot = 0; slot < 64; ++slot)
    {
        if (! dump.present[static_cast<std::size_t> (slot)]) continue;
        ++tones;
        const auto encoded = cz::encodeTone (dump.tones[static_cast<std::size_t> (slot)], slot);

        // find the original message
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> cur; bool inside = false;
        for (auto b : bytes)
        {
            if (b == 0xF0) { cur = { b }; inside = true; continue; }
            if (! inside) continue;
            cur.push_back (b);
            if (b == 0xF7) { inside = false; if (cur.size() > 7 && cur[5] == 0x21 && cur[6] == slot) { original = cur; break; } }
        }
        if (original != encoded) { ++mismatches; std::printf ("slot %d differs\n", slot); }
    }

    // Envelope codec: decode + encode must reproduce every active step byte for byte.
    int envChecked = 0, envBad = 0;
    for (int slot = 0; slot < 64; ++slot)
        for (int line = 0; line < 2; ++line)
            for (auto kind : { cz::EnvKind::dca, cz::EnvKind::dcw, cz::EnvKind::dco })
            {
                auto tone = dump.tones[static_cast<size_t> (slot)];
                const auto env = cz::getEnvelope (tone, kind, line);
                cz::setEnvelope (tone, kind, line, env);
                ++envChecked;
                if (tone.data != dump.tones[static_cast<size_t> (slot)].data)
                {
                    ++envBad;
                    if (envBad <= 5)
                        std::printf ("envelope mismatch: slot %d line %d kind %d\n", slot, line, static_cast<int> (kind));
                }
            }
    std::printf ("envelopes: %d checked, %d mismatches\n", envChecked, envBad);

    // Simple parameters: reading then writing back must not change a single byte.
    int parChecked = 0, parBad = 0;
    for (int slot = 0; slot < 64; ++slot)
        for (const auto& param : cz::simpleParams())
        {
            auto tone = dump.tones[static_cast<size_t> (slot)];
            const int v = param.get (tone);
            param.set (tone, v);
            ++parChecked;
            if (v < param.min || v > param.max || tone.data != dump.tones[static_cast<size_t> (slot)].data)
            {
                ++parBad;
                if (parBad <= 10)
                    std::printf ("param mismatch: slot %d %s value %d\n", slot, param.name, v);
            }
        }
    std::printf ("params: %d checked, %d mismatches\n", parChecked, parBad);

    cz::Tone t = dump.tones[0];
    std::printf ("%d tones, %d mismatches, %zu other messages\n", tones, mismatches, dump.others.size());
    std::printf ("slot 0: name='%s' octave=%d line=%d\n", t.name().c_str(), t.octave(), t.lineSelect());
    t.setName ("TEST");
    std::printf ("setName('TEST') -> '%s' (raw '%.16s')\n", t.name().c_str(), reinterpret_cast<const char*> (t.data.data() + cz::bodySize));
    return mismatches == 0 && tones == 64 && envBad == 0 && parBad == 0 ? 0 : 1;
}
