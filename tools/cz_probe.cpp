// Headless probe: boots the emulated CZ machine (no audio device, no GUI),
// sends a SysEx request on MIDI IN and prints what comes back on MIDI OUT.
//
//   cz_probe <roms-dir> <cz1|cz101> <plugins-dir> <hex bytes to send...>
//
// <roms-dir> must already contain the sets in MAME layout (cz1.zip, hd44780.zip
// or set folders).  Each hex argument is a group of bytes sent in order; the
// probe waits for output between groups and prints everything in hex.
#include "EmbeddedEmulatorEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static std::vector<std::uint8_t> parseHex (const std::string& text)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < text.size();)
    {
        if (text[i] == ' ') { ++i; continue; }
        out.push_back (static_cast<std::uint8_t> (std::strtoul (text.substr (i, 2).c_str(), nullptr, 16)));
        i += 2;
    }
    return out;
}

static std::vector<std::uint8_t> collect (ves::EmbeddedEmulatorEngine& engine, int quietMs, int maxMs)
{
    std::vector<std::uint8_t> got;
    std::uint8_t buf[512];
    auto lastData = std::chrono::steady_clock::now();
    const auto start = lastData;
    for (;;)
    {
        const auto n = engine.readMidiOutput (buf, sizeof buf);
        const auto now = std::chrono::steady_clock::now();
        if (n > 0)
        {
            got.insert (got.end(), buf, buf + n);
            lastData = now;
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds> (now - lastData).count() > quietMs
            || std::chrono::duration_cast<std::chrono::milliseconds> (now - start).count() > maxMs)
            break;
        std::this_thread::sleep_for (5ms);
    }
    return got;
}

static void pump (ves::EmbeddedEmulatorEngine& engine, std::vector<ves::StereoFrame>& sink, int ms)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds (ms))
    {
        engine.readAudioFrames (sink.data(), sink.size());
        std::this_thread::sleep_for (10ms);
    }
}

static void printHex (const char* label, const std::vector<std::uint8_t>& bytes)
{
    std::printf ("%s (%zu bytes):", label, bytes.size());
    for (auto b : bytes)
        std::printf (" %02X", b);
    std::printf ("\n");
}

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr, "usage: cz_probe <roms-dir> <cz1|cz101> <plugins-dir> [hex ...]\n");
        return 2;
    }

    const std::string machine = argv[2];
    const auto tmp = std::filesystem::temp_directory_path() / "cz_probe";
    std::filesystem::remove_all (tmp);
    std::filesystem::create_directories (tmp / "cfg");
    std::filesystem::create_directories (tmp / "nvram");

    ves::EmbeddedEmulatorEngineSettings settings;
    settings.rom_path = argv[1];
    settings.cfg_path = (tmp / "cfg").string();
    settings.nvram_path = (tmp / "nvram").string();
    // Copy the plugins and add a tiny "inject" plugin that presses panel buttons on request.
    const auto injectDir = tmp;   // cmd.txt / log.txt live here
    std::filesystem::create_directories (tmp / "plugins");
    std::filesystem::copy (argv[3], tmp / "plugins", std::filesystem::copy_options::recursive);
    {
        // Graft the button injector into the copied layout plugin (the only plugin the engine starts).
        const auto layoutLua = tmp / "plugins" / "layout" / "init.lua";
        std::ifstream in (layoutLua);
        std::string lua ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const std::string marker = "function layout.startplugin()\n";
        const auto pos = lua.find (marker);
        const std::string dir = tmp.string();
        const std::string injector = std::string (R"LUA(
	local held = {}
	local inject_subscription
	local function note(text) local m = io.open(")LUA") + dir + R"LUA(/log.txt", "a"); if m then m:write(text .. "\n"); m:close() end end
	note("startplugin ran")
	layout.inject_sub = emu.add_machine_frame_notifier(function ()
	  local ok, err = pcall(function ()
		local f = io.open(")LUA" + dir + R"LUA(/cmd.txt", "r")
		if f then
			local line = f:read("l")
			f:close()
			os.remove(")LUA" + dir + R"LUA(/cmd.txt")
			local name = line and line:match("^press (.+)$")
			note("cmd " .. tostring(line))
			if name then
				for tag, port in pairs(manager.machine.ioport.ports) do
					local field = port.fields[name]
					if field then
						note("press " .. name .. " on " .. tag)
						field:set_value(1)
						held[#held + 1] = { field = field, frames = 8 }
					end
				end
			end
		end
		for i = #held, 1, -1 do
			held[i].frames = held[i].frames - 1
			if held[i].frames <= 0 then
				held[i].field:set_value(0)
				table.remove(held, i)
			end
		end
	  end)
	  if not ok then note("error: " .. tostring(err)) end
	end)
)LUA";
        if (pos != std::string::npos)
            lua.insert (pos + marker.size(), injector);
        std::ofstream (layoutLua) << lua;
    }
    settings.plugins_path = (tmp / "plugins").string();
    settings.driver_name = machine;
    settings.sample_rate = 48000;
    settings.native_midi_input_option = "midiin";
    settings.native_midi_output_option = std::getenv ("CZ_OUT_OPT") ? std::getenv ("CZ_OUT_OPT") : (machine == "cz1" ? "midiout1" : "midiout");
    settings.engine_generation = 1;
    settings.save_nvram = false;

    ves::EmbeddedEmulatorEngine engine (std::move (settings));
    engine.setVideoDisplayActive (true);
    engine.requestVideoCaptureWidth (1280);
    engine.start();
    engine.noteHostAudioConfiguration (48000.0, 512);

    // Drain audio so MAME's queue never blocks, and wait for boot.
    std::vector<ves::StereoFrame> sink (4096);
    const auto boot = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - boot < 4s)
    {
        engine.readAudioFrames (sink.data(), sink.size());
        if (engine.diagnostics().machine_exited.load() != 0)
        {
            std::fprintf (stderr, "MAME exited during boot (code %d)\n", engine.mameResult());
            return 1;
        }
        std::this_thread::sleep_for (10ms);
    }
    std::printf ("booted: started=%llu frames=%llu\n",
                 static_cast<unsigned long long> (engine.diagnostics().machine_started.load()),
                 static_cast<unsigned long long> (engine.diagnostics().video_frames_produced.load()));

    collect (engine, 100, 200); // discard anything sent at boot

    // Remaining arguments are steps:
    //   <hex bytes>          send on MIDI IN, then print the reply
    //   press:<Field Name>   press a front-panel button (via the inject plugin)
    //   wait:<ms>            keep draining audio for a while
    //   shot:<file.ppm>      save the current video frame
    for (int i = 4; i < argc; ++i)
    {
        const std::string step = argv[i];
        if (step.rfind ("press:", 0) == 0)
        {
            std::ofstream (injectDir / "cmd.txt") << "press " << step.substr (6) << "\n";
            std::printf ("press %s\n", step.c_str() + 6);
            pump (engine, sink, 400);
        }
        else if (step.rfind ("click:", 0) == 0)
        {
            int x = 0, y = 0;
            std::sscanf (step.c_str() + 6, "%d,%d", &x, &y);
            engine.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, x, y);
            pump (engine, sink, 100);
            engine.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftDown, x, y);
            pump (engine, sink, 250);
            engine.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftUp, x, y);
            pump (engine, sink, 350);
            std::printf ("click %d,%d\n", x, y);
        }
        else if (step.rfind ("listen:", 0) == 0)
        {
            // listen:<file>:<ms>  - record MIDI OUT for a while, write raw bytes to file
            const auto colon = step.rfind (':');
            const auto file = step.substr (7, colon - 7);
            const int ms = std::atoi (step.c_str() + colon + 1);
            std::vector<std::uint8_t> all;
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds (ms))
            {
                engine.readAudioFrames (sink.data(), sink.size());
                std::uint8_t buf[512];
                const auto n = engine.readMidiOutput (buf, sizeof buf);
                all.insert (all.end(), buf, buf + n);
                std::this_thread::sleep_for (5ms);
            }
            std::ofstream (file, std::ios::binary).write (reinterpret_cast<const char*> (all.data()), static_cast<std::streamsize> (all.size()));
            std::printf ("listen: %zu bytes -> %s\n", all.size(), file.c_str());
        }
        else if (step.rfind ("ram:", 0) == 0)
        {
            // ram:<tag>:<file>  - copy a shared RAM block of the machine to a file
            const auto colon = step.find (':', 4);
            const auto tag = step.substr (4, colon - 4);
            const auto file = step.substr (colon + 1);
            std::vector<std::uint8_t> bytes (engine.ramSize (tag.c_str()));
            const auto n = engine.readRam (tag.c_str(), 0, bytes.data(), bytes.size());
            std::ofstream (file, std::ios::binary).write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (n));
            std::printf ("ram %s: %zu bytes -> %s\n", tag.c_str(), n, file.c_str());
        }
        else if (step.rfind ("wram:", 0) == 0)
        {
            // wram:<tag>:<offset>:<hex bytes>  - write bytes into a shared RAM block
            const auto c1 = step.find (':', 5);
            const auto c2 = step.find (':', c1 + 1);
            const auto tag = step.substr (5, c1 - 5);
            const auto offset = static_cast<std::size_t> (std::strtoul (step.substr (c1 + 1, c2 - c1 - 1).c_str(), nullptr, 0));
            const auto bytes = parseHex (step.substr (c2 + 1));
            const auto n = engine.writeRam (tag.c_str(), offset, bytes.data(), bytes.size());
            std::printf ("wram %s +%zu: %zu bytes\n", tag.c_str(), offset, n);
        }
        else if (step.rfind ("sweep:", 0) == 0)
        {
            // sweep:<x>,<y>,<count>,<file>  - tap a panel button <count> times and append the
            // sub CPU's 128-byte edit buffer (subram[8192..]) after the initial state and after each tap.
            int x = 0, y = 0, count = 0;
            char file[512] = {};
            std::sscanf (step.c_str() + 6, "%d,%d,%d,%511s", &x, &y, &count, file);
            std::ofstream out (file, std::ios::binary);
            std::uint8_t snap[128];
            for (int n = 0; n <= count; ++n)
            {
                if (n > 0)
                {
                    engine.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, x, y);
                    pump (engine, sink, 30);
                    engine.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftDown, x, y);
                    pump (engine, sink, 90);
                    engine.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftUp, x, y);
                    pump (engine, sink, 120);
                }
                engine.readRam ("subram", 8192, snap, sizeof snap);
                out.write (reinterpret_cast<const char*> (snap), sizeof snap);
            }
            std::printf ("sweep %d taps at %d,%d -> %s\n", count, x, y, file);
        }
        else if (step.rfind ("wait:", 0) == 0)
            pump (engine, sink, std::atoi (step.c_str() + 5));
        else if (step.rfind ("shot:", 0) == 0)
        {
            ves::VideoFrameSnapshot frame;
            if (engine.copyLatestVideoFrame (frame) && frame.width > 0)
            {
                std::ofstream out (step.substr (5), std::ios::binary);
                out << "P6\n" << frame.width << " " << frame.height << "\n255\n";
                for (auto px : frame.pixels)
                {
                    const char rgb[3] { static_cast<char> ((px >> 16) & 0xff), static_cast<char> ((px >> 8) & 0xff),
                                        static_cast<char> (px & 0xff) };
                    out.write (rgb, 3);
                }
                std::printf ("shot %s (%dx%d)\n", step.c_str() + 5, frame.width, frame.height);
            }
            else
                std::printf ("shot: no frame\n");
        }
        else
        {
            const auto bytes = parseHex (step);
            printHex ("send", bytes);
            engine.sendMidiBytes (bytes.data(), bytes.size());
            const auto reply = collect (engine, std::getenv ("CZ_QUIET") ? std::atoi (std::getenv ("CZ_QUIET")) : 600, 12000);
            printHex ("recv", reply);
        }
    }

    const auto& d = engine.diagnostics();
    std::printf ("peak_abs=%.4f audio_frames_written=%llu\n", (double) d.peak_abs.load(),
                 (unsigned long long) d.audio_frames_written.load());
    std::printf ("diag: midi_in_open=%llu midi_out_open=%llu bytes_queued=%llu bytes_consumed=%llu overflow=%llu\n",
                 (unsigned long long) d.midi_input_open_count.load(), (unsigned long long) d.midi_output_open_count.load(),
                 (unsigned long long) d.midi_bytes_queued.load(), (unsigned long long) d.midi_bytes_consumed.load(),
                 (unsigned long long) d.midi_queue_overflow.load());
    engine.stopAndJoin (5s);
    return 0;
}
