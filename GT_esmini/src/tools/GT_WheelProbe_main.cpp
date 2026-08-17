// GT_WheelProbe -- feature:F8
//
// A read-only SDL2 joystick probe: enumerate wheels, and stream their raw axis
// / button state as JSON lines, optionally alongside the normalized values a
// given axis mapping would produce.
//
// WHY THIS EXISTS AS A SEPARATE EXECUTABLE
//
// Making the wheel axis mapping configurable is useless without a way to learn
// WHICH axis is which on a given device -- that is a per-device fact only the
// device can tell you. The number has to come from the SAME index space
// SDL2WheelInput reads at runtime, which rules out the browser Gamepad API
// (its own index space, mapped per-device by the browser; a "move an axis to
// assign it" UI built on it would confidently write the wrong number). So the
// discovery path is SDL, in-process with the same SDL version the simulator
// links.
//
// DELIBERATE NON-DEPENDENCIES
//
//  - No CommonMini / logger. esmini's logger writes to stdout by default, and
//    stdout here is a JSON stream consumed by the web backend; a single log
//    line would corrupt it. (It would also create a stray log.txt next to
//    wherever the probe was launched from.)
//  - No ManualDriveConfig. The mapping arrives on the COMMAND LINE rather than
//    being read from manual_drive.json, so the GUI can preview values for a
//    mapping the user has edited but not yet saved -- computed by the real
//    WheelAxisMapping normalizer, not a re-implementation of it.
//  - No SDL_Haptic, ever. This tool must never energise a wheel: it is the
//    thing a user runs while poking at pedals with the engine of the
//    simulation not running, possibly with their hands off.
//
// Exit codes (project convention: distinct causes get distinct codes)
//   0  success
//   2  usage error
//   3  SDL initialization failed
//   4  requested device index does not exist

#include "gt_esmini/control/manualdrive/WheelAxisMapping.hpp"

// SDL_MAIN_HANDLED: on Windows SDL.h #defines main -> SDL_main and expects the
// SDL2main shim to provide the real entry point. This is a plain console tool
// whose stdout is a pipe, so we keep our own main() and tell SDL so (plus the
// SDL_SetMainReady() call in main below, which is what SDL2main would have
// done). Linking SDL2main instead would also drag in the WinMain path.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

// Minimal JSON string escaping. Device names are vendor-supplied strings and
// have been observed to contain quotes and backslashes.
std::string JsonEscape(const char* s)
{
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p; ++p)
    {
        switch (*p)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(*p) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(*p));
                    out += buf;
                }
                else
                {
                    out += *p;
                }
        }
    }
    return out;
}

void PrintUsage()
{
    std::cout <<
        "GT_WheelProbe -- read-only SDL2 wheel/pedal axis probe (feature:F8)\n"
        "\n"
        "  GT_WheelProbe --list\n"
        "      Enumerate joysticks as one JSON object on stdout.\n"
        "\n"
        "  GT_WheelProbe [--device N] [--hz N] [--frames N] [mapping options]\n"
        "      Stream one JSON object per line: raw axis/button state plus the\n"
        "      normalized values the given mapping produces.\n"
        "\n"
        "Options\n"
        "  --device N            device index (default 0)\n"
        "  --hz N                sample rate, 1..250 (default 30)\n"
        "  --frames N            stop after N frames (default 0 = until killed)\n"
        "  --no-pump             do not call SDL_PumpEvents each frame (diagnostic)\n"
        "  --no-haptic-init      do not initialize the HAPTIC subsystem (diagnostic)\n"
        "\n"
        "Mapping options (default to the shipped G29 layout)\n"
        "  --steer-axis N            --steer-invert\n"
        "  --steer-raw-center N      --steer-raw-full N\n"
        "  --throttle-axis N         --throttle-raw-released N  --throttle-raw-full N\n"
        "  --brake-axis N            --brake-raw-released N     --brake-raw-full N\n"
        "  --clutch-axis N           --clutch-raw-released N    --clutch-raw-full N\n"
        "  (an axis index of -1 marks that function unassigned)\n"
        "\n"
        "This tool never opens SDL_Haptic and never applies force feedback.\n";
}

// --flag VALUE parsing. Returns false on a missing or non-numeric value, which
// the caller turns into a usage error -- a probe that silently substituted a
// default for a mistyped flag would report values for a mapping the caller did
// not ask for.
bool TakeInt(int argc, char** argv, int& i, int& out)
{
    if (i + 1 >= argc) return false;
    char*     end = nullptr;
    const long v  = std::strtol(argv[i + 1], &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    ++i;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    SDL_SetMainReady();  // see the SDL_MAIN_HANDLED note at the top of this file

    bool list_only = false;
    int  device    = 0;
    int  hz        = 30;
    int  frames    = 0;
    // Both default ON because that is what makes the probe read the device the
    // same way SDL2WheelInput does. Measured on a real G29 (2026-08-06): with
    // JOYSTICK-only init and no SDL_PumpEvents, SDL_JoystickUpdate returned
    // raw=0 on all four axes indefinitely -- the probe looked like a wheel with
    // dead pedals. See the comment at the SDL_Init call below. The --no-* flags
    // exist so that measurement stays reproducible instead of becoming a
    // sentence nobody can re-check.
    bool pump_events = true;
    bool haptic_init = true;

    gt_esmini::WheelAxisMapping map;  // shipped G29 defaults

    for (int i = 1; i < argc; ++i)
    {
        const char* a  = argv[i];
        bool        ok = true;
        if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h"))
        {
            PrintUsage();
            return 0;
        }
        else if (!std::strcmp(a, "--list"))                  list_only = true;
        else if (!std::strcmp(a, "--no-pump"))               pump_events = false;
        else if (!std::strcmp(a, "--no-haptic-init"))        haptic_init = false;
        else if (!std::strcmp(a, "--device"))                ok = TakeInt(argc, argv, i, device);
        else if (!std::strcmp(a, "--hz"))                    ok = TakeInt(argc, argv, i, hz);
        else if (!std::strcmp(a, "--frames"))                ok = TakeInt(argc, argv, i, frames);
        else if (!std::strcmp(a, "--steer-axis"))            ok = TakeInt(argc, argv, i, map.steer.index);
        else if (!std::strcmp(a, "--steer-invert"))          map.steer.invert = true;
        else if (!std::strcmp(a, "--steer-raw-center"))      ok = TakeInt(argc, argv, i, map.steer.raw_center);
        else if (!std::strcmp(a, "--steer-raw-full"))        ok = TakeInt(argc, argv, i, map.steer.raw_full);
        else if (!std::strcmp(a, "--throttle-axis"))         ok = TakeInt(argc, argv, i, map.throttle.index);
        else if (!std::strcmp(a, "--throttle-raw-released")) ok = TakeInt(argc, argv, i, map.throttle.raw_released);
        else if (!std::strcmp(a, "--throttle-raw-full"))     ok = TakeInt(argc, argv, i, map.throttle.raw_full);
        else if (!std::strcmp(a, "--brake-axis"))            ok = TakeInt(argc, argv, i, map.brake.index);
        else if (!std::strcmp(a, "--brake-raw-released"))    ok = TakeInt(argc, argv, i, map.brake.raw_released);
        else if (!std::strcmp(a, "--brake-raw-full"))        ok = TakeInt(argc, argv, i, map.brake.raw_full);
        else if (!std::strcmp(a, "--clutch-axis"))           ok = TakeInt(argc, argv, i, map.clutch.index);
        else if (!std::strcmp(a, "--clutch-raw-released"))   ok = TakeInt(argc, argv, i, map.clutch.raw_released);
        else if (!std::strcmp(a, "--clutch-raw-full"))       ok = TakeInt(argc, argv, i, map.clutch.raw_full);
        else
        {
            std::cerr << "GT_WheelProbe: unknown option '" << a << "'\n";
            PrintUsage();
            return 2;
        }
        if (!ok)
        {
            std::cerr << "GT_WheelProbe: option '" << a << "' needs an integer value\n";
            return 2;
        }
    }

    if (hz < 1 || hz > 250)
    {
        std::cerr << "GT_WheelProbe: --hz must be 1..250\n";
        return 2;
    }

    // Subsystem selection is NOT cosmetic here: it decides which Windows
    // joystick backend SDL picks, and therefore whether the axis INDICES this
    // tool reports are the same ones SDL2WheelInput will read at runtime. A
    // probe that reported a different index space than the runtime would be
    // worse than no probe at all, so it initializes the same pair
    // SDL2WheelInput does (JOYSTICK | HAPTIC).
    //
    // Initializing the HAPTIC SUBSYSTEM does not energise anything: force
    // requires SDL_HapticOpen plus a created-and-run effect, and this tool
    // never calls either. That is the safety property being preserved -- not
    // "HAPTIC is never mentioned".
    const Uint32 subsystems = SDL_INIT_JOYSTICK | (haptic_init ? SDL_INIT_HAPTIC : 0u);
    if (SDL_Init(subsystems) < 0)
    {
        std::cout << "{\"type\":\"error\",\"message\":\"SDL_Init failed: " << JsonEscape(SDL_GetError())
                  << "\"}" << std::endl;
        return 3;
    }

    const int num_joysticks = SDL_NumJoysticks();

    if (list_only)
    {
        std::cout << "{\"type\":\"devices\",\"devices\":[";
        for (int i = 0; i < num_joysticks; ++i)
        {
            SDL_Joystick* js = SDL_JoystickOpen(i);
            if (i > 0) std::cout << ",";
            std::cout << "{\"index\":" << i
                      << ",\"name\":\"" << JsonEscape(SDL_JoystickNameForIndex(i)) << "\""
                      << ",\"num_axes\":" << (js ? SDL_JoystickNumAxes(js) : -1)
                      << ",\"num_buttons\":" << (js ? SDL_JoystickNumButtons(js) : -1)
                      << ",\"num_hats\":" << (js ? SDL_JoystickNumHats(js) : -1) << "}";
            if (js) SDL_JoystickClose(js);
        }
        std::cout << "]}" << std::endl;
        SDL_QuitSubSystem(subsystems);
        return 0;
    }

    if (device < 0 || device >= num_joysticks)
    {
        std::cout << "{\"type\":\"error\",\"message\":\"device index " << device << " not available ("
                  << num_joysticks << " joystick(s) detected)\"}" << std::endl;
        SDL_QuitSubSystem(subsystems);
        return 4;
    }

    SDL_Joystick* js = SDL_JoystickOpen(device);
    if (!js)
    {
        std::cout << "{\"type\":\"error\",\"message\":\"SDL_JoystickOpen failed: "
                  << JsonEscape(SDL_GetError()) << "\"}" << std::endl;
        SDL_QuitSubSystem(subsystems);
        return 4;
    }

    const int n_axes    = SDL_JoystickNumAxes(js);
    const int n_buttons = SDL_JoystickNumButtons(js);

    // Meta line first, so a consumer knows how many bars to draw and whether the
    // mapping it asked for even fits this device, before any frame arrives.
    std::vector<std::string> problems;
    map.CollectProblems(n_axes, problems);
    std::cout << "{\"type\":\"meta\",\"index\":" << device
              << ",\"name\":\"" << JsonEscape(SDL_JoystickName(js)) << "\""
              << ",\"num_axes\":" << n_axes
              << ",\"num_buttons\":" << n_buttons
              << ",\"num_hats\":" << SDL_JoystickNumHats(js)
              << ",\"hz\":" << hz
              << ",\"problems\":[";
    for (size_t i = 0; i < problems.size(); ++i)
    {
        if (i > 0) std::cout << ",";
        std::cout << "\"" << JsonEscape(problems[i].c_str()) << "\"";
    }
    std::cout << "]}" << std::endl;

    const Uint32 delay_ms = static_cast<Uint32>(1000 / hz);
    const Uint32 t0       = SDL_GetTicks();

    // Per-axis "has this axis ever reported a non-zero value since open" latch,
    // reported alongside the raw values.
    //
    // WHY THIS IS NOT COSMETIC. A wheel can enumerate (name, axis count, button
    // count all correct) while reporting raw=0 on every axis -- measured on this
    // machine's G29 on 2026-08-06, and reproduced by an independent PySDL2
    // reader, so it is a device state and not an SDL usage error. With the G29
    // pedal convention (released = +32767) raw=0 normalizes to ~0.5, so a panel
    // that only drew normalized bars would show HALF-PRESSED PEDALS on a wheel
    // that is saying nothing at all -- and a user would "calibrate" against
    // that. Reporting the latch lets the consumer distinguish "released" from
    // "no data yet", which normalized values alone cannot express.
    std::vector<int> reported(static_cast<size_t>(n_axes < 0 ? 0 : n_axes), 0);

    auto read_axis = [&](int index) -> int {
        if (index < 0 || index >= n_axes) return 0;
        return SDL_JoystickGetAxis(js, index);
    };

    for (int frame = 0; frames == 0 || frame < frames; ++frame)
    {
        // SDL_PumpEvents before SDL_JoystickUpdate: on Windows the joystick
        // state a driver reports arrives through the message queue, and a
        // process with no window and no pump can sit at raw=0 forever (measured
        // on a real G29 -- see the flag defaults above). SDL2WheelInput runs
        // inside GT_Sim, which pumps; a standalone tool has to do it itself.
        if (pump_events)
        {
            SDL_PumpEvents();
        }
        SDL_JoystickUpdate();

        std::cout << "{\"type\":\"frame\",\"t\":" << (SDL_GetTicks() - t0) / 1000.0 << ",\"axes\":[";
        for (int i = 0; i < n_axes; ++i)
        {
            const int raw = SDL_JoystickGetAxis(js, i);
            if (raw != 0) reported[static_cast<size_t>(i)] = 1;
            if (i > 0) std::cout << ",";
            std::cout << raw;
        }
        std::cout << "],\"reported\":[";
        for (int i = 0; i < n_axes; ++i)
        {
            if (i > 0) std::cout << ",";
            std::cout << (reported[static_cast<size_t>(i)] ? "true" : "false");
        }
        std::cout << "],\"buttons\":[";
        for (int i = 0; i < n_buttons; ++i)
        {
            if (i > 0) std::cout << ",";
            std::cout << (SDL_JoystickGetButton(js, i) ? 1 : 0);
        }
        // Normalized values through the REAL runtime normalizer, so what the GUI
        // shows is what the simulator would feed the vehicle -- not a
        // re-implementation that can drift from it.
        //
        // The "no HID report yet" sentinel (SDL2WheelInput::ReadPedal) is NOT
        // applied here on purpose: this tool exists to show what the device
        // actually reports, including a pedal that has not reported yet. A probe
        // that substituted a plausible value would hide the very condition a
        // user is trying to diagnose.
        std::cout << "],\"norm\":{"
                  << "\"steering\":" << (map.steer.IsAssigned() ? map.steer.Normalize(read_axis(map.steer.index)) : 0.0)
                  << ",\"throttle\":" << (map.throttle.IsAssigned() ? map.throttle.Normalize(read_axis(map.throttle.index)) : 0.0)
                  << ",\"brake\":" << (map.brake.IsAssigned() ? map.brake.Normalize(read_axis(map.brake.index)) : 0.0)
                  << ",\"clutch\":" << (map.clutch.IsAssigned() ? map.clutch.Normalize(read_axis(map.clutch.index)) : 0.0)
                  << "}}" << std::endl;  // endl: the consumer is a pipe reader, so every frame must flush

        // Exit when the consumer goes away. On Windows a broken pipe makes the
        // write fail silently (badbit) rather than raising, and a child process
        // is NOT killed with its parent -- so without this check the probe
        // outlives an abruptly-terminated web backend and keeps polling the
        // wheel forever. Observed for real: killing the server left a
        // GT_WheelProbe running with the device open, which is exactly the kind
        // of orphan that later gets diagnosed as "the wheel is silent".
        if (!std::cout)
        {
            return 0;
        }

        SDL_Delay(delay_ms);
    }

    SDL_JoystickClose(js);
    SDL_QuitSubSystem(subsystems);
    return 0;
}
