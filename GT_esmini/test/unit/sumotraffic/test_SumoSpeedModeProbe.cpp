/*
 * feature:F9 -- does setSpeedMode() actually lift the speed cap in libsumo
 * 1.6.0?
 *
 * Design doc GT_esmini/docs/features/sumo_background_traffic.md section 2-4
 * established, by editing the net's lane speed limit, that setSpeed() DOES
 * reach a remote-controlled (moveToXY'd) vehicle and that what clips it is
 * (lane speed limit x speedFactor). It could not establish that the API route
 * out of that clip -- setSpeedMode() -- works, because nothing had called it.
 *
 * This probe calls it. It talks to libsumo directly: no esmini, no scenario, no
 * GT controller, so a result here cannot be explained away by anything in the
 * GT injection path. The two polarities differ in exactly one statement.
 *
 * NOT part of any standing gate. It loads a real SUMO network and takes a few
 * seconds; ctest runs each polarity as its own process (see
 * GT_esmini/test/CMakeLists.txt) so neither depends on libsumo surviving a
 * reload.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "gtest/gtest.h"

// PositionVector first: libsumo/Vehicle.h declares storeShape(..., PositionVector&)
// without forward-declaring it.
#include <utils/geom/PositionVector.h>

#include <libsumo/Simulation.h>
#include <libsumo/TraCIDefs.h>
#include <libsumo/Vehicle.h>

#ifndef GT_SUMO_PROBE_REPO_ROOT
#define GT_SUMO_PROBE_REPO_ROOT "."
#endif

namespace
{
constexpr double kStepLength = 0.05;
constexpr int    kSteps      = 400;  // 20 s

// e6mini: a 4-lane, ~1463 m straight highway edge, every lane limited to
// 13.89 m/s. Starting at s = 700 m keeps the probe ahead of the demand
// vehicles (which depart at the edge base) for the whole run, so nothing is
// ever in front of it -- a leader would clip the speed for a reason that has
// nothing to do with the lane limit.
constexpr char   kEdgeId[]        = "-0.0.00";
constexpr int    kLaneIndex       = 1;
constexpr double kStartS          = 700.0;
constexpr double kLaneSpeedLimit  = 13.89;

// Speed command: 2 s at 3 m/s, a 10 s linear ramp to 20 m/s, then 8 s at
// 20 m/s. Same 3 -> 20 m/s span the cut-in_sumo.xosc Ego drives in the
// section 2-4 measurement.
double CommandedSpeed(double t)
{
    if (t < 2.0)
    {
        return 3.0;
    }
    if (t < 12.0)
    {
        return 3.0 + (20.0 - 3.0) * (t - 2.0) / 10.0;
    }
    return 20.0;
}

struct Sample
{
    double t         = 0.0;
    double commanded = 0.0;
    double reported  = 0.0;  // what SUMO says the vehicle's speed is
};

// Drives one vehicle the way ControllerSumoTraffic drives an injected scenario
// entity: add(), then every step moveToXY() to an externally decided pose plus
// setSpeed() with an externally decided speed.
std::vector<Sample> RunProbe(bool clear_speed_mode)
{
    // GT_SUMO_PROBE_CFG lets the probe run against a .sumocfg carrying
    // <fcd-output>, so that what getSpeed() reports here can be checked against
    // what SUMO writes out by itself. An instrument that is the only witness to
    // its own claim is not evidence.
    const char*       cfg_override = getenv("GT_SUMO_PROBE_CFG");
    const std::string cfg = cfg_override != nullptr ? std::string(cfg_override)
                                                    : std::string(GT_SUMO_PROBE_REPO_ROOT) + "/resources/sumo_inputs/e6mini.sumocfg";

    libsumo::Simulation::load({"-c " + cfg, "--xml-validation", "never", "--step-length", "0.05", "--seed", "42"});
    EXPECT_TRUE(libsumo::Simulation::isLoaded()) << "cannot probe without a loaded network: " << cfg;

    std::vector<Sample> samples;
    double              s = kStartS;

    for (int i = 1; i <= kSteps; i++)
    {
        const double t = i * kStepLength;
        libsumo::Simulation::step(t);

        if (i == 1)
        {
            libsumo::Vehicle::add("probe", "", "DEFAULT_VEHTYPE", "now", "first", "base", "3");
            if (clear_speed_mode)
            {
                // The single statement the two polarities differ in.
                libsumo::Vehicle::setSpeedMode("probe", 0);
            }
        }

        // Sample BEFORE issuing the next command: this is the speed SUMO
        // arrived at for the step just executed. On the very first step the
        // vehicle has been added but has no speed yet and SUMO answers with its
        // INVALID_DOUBLE_VALUE sentinel -- keeping that in the series would put
        // a -1.07e9 in the CSV for a later reader to trip over.
        const double reported = libsumo::Vehicle::getSpeed("probe");
        if (reported >= 0.0)
        {
            samples.push_back(Sample{t, CommandedSpeed(t - kStepLength), reported});
        }

        const double commanded = CommandedSpeed(t);
        s += commanded * kStepLength;

        // Stay on the road by construction: take the pose from the lane's own
        // geometry rather than dead-reckoning a straight line off the edge.
        const libsumo::TraCIPosition here  = libsumo::Simulation::convert2D(kEdgeId, s, kLaneIndex);
        const libsumo::TraCIPosition ahead = libsumo::Simulation::convert2D(kEdgeId, s + 1.0, kLaneIndex);
        const double                 angle = atan2(ahead.x - here.x, ahead.y - here.y) * 180.0 / M_PI;  // navigational degrees

        libsumo::Vehicle::moveToXY("probe", "random", 0, here.x, here.y, angle, 0);
        libsumo::Vehicle::setSpeed("probe", commanded);
    }

    libsumo::Simulation::close();

    // GT_SUMO_PROBE_CSV=<path>: dump every sample so the run can be diffed
    // against SUMO's own fcd-output row by row.
    const char* csv_path = getenv("GT_SUMO_PROBE_CSV");
    if (csv_path != nullptr)
    {
        if (FILE* csv = fopen(csv_path, "w"))
        {
            fprintf(csv, "t,commanded,reported\n");
            for (const Sample& sample : samples)
            {
                fprintf(csv, "%.2f,%.4f,%.4f\n", sample.t, sample.commanded, sample.reported);
            }
            fclose(csv);
            printf("[F9 probe] samples written to %s\n", csv_path);
        }
    }

    return samples;
}

// Mean reported speed over the final plateau (t in [15, 20]), where the command
// is a flat 20 m/s and no ramp or transient is left.
double PlateauMean(const std::vector<Sample>& samples)
{
    double sum   = 0.0;
    int    count = 0;
    for (const Sample& sample : samples)
    {
        if (sample.t >= 15.0)
        {
            sum += sample.reported;
            count++;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double PeakReported(const std::vector<Sample>& samples)
{
    double peak = 0.0;
    for (const Sample& sample : samples)
    {
        peak = std::max(peak, sample.reported);
    }
    return peak;
}
}  // namespace

// Polarity 1: leave SUMO's speed mode at its default (31). The commanded
// 20 m/s must NOT arrive -- it gets clipped at lane limit x speedFactor.
TEST(SumoSpeedModeProbe, DefaultSpeedModeClipsAtTheLaneLimit)
{
    const std::vector<Sample> samples = RunProbe(false);
    ASSERT_FALSE(samples.empty());

    const double plateau = PlateauMean(samples);
    const double peak    = PeakReported(samples);
    printf("[F9 probe] speed mode DEFAULT: plateau mean %.3f m/s, peak %.3f m/s (commanded 20.000, lane limit %.2f)\n",
           plateau,
           peak,
           kLaneSpeedLimit);

    EXPECT_LT(peak, 20.0 - 3.0) << "commanded 20 m/s should not be reachable under the default speed mode";
    // speedFactor is sampled per vehicle (DEFAULT_VEHTYPE: mean 1.0, dev 0.1),
    // so the exact plateau is not predictable -- but it has to sit around the
    // lane limit rather than around the command.
    EXPECT_GT(plateau, kLaneSpeedLimit * 0.9);
    EXPECT_LT(plateau, kLaneSpeedLimit * 1.2);
}

// Polarity 2: setSpeedMode(0) -- all SUMO checks off. The commanded 20 m/s
// must arrive, on the same network, with the same lane limit.
TEST(SumoSpeedModeProbe, ClearedSpeedModeFollowsTheCommandedSpeed)
{
    const std::vector<Sample> samples = RunProbe(true);
    ASSERT_FALSE(samples.empty());

    const double plateau = PlateauMean(samples);
    const double peak    = PeakReported(samples);
    printf("[F9 probe] speed mode 0      : plateau mean %.3f m/s, peak %.3f m/s (commanded 20.000, lane limit %.2f)\n",
           plateau,
           peak,
           kLaneSpeedLimit);

    EXPECT_NEAR(plateau, 20.0, 0.2) << "with every check off, SUMO should report exactly what was commanded";
    EXPECT_GT(peak, kLaneSpeedLimit * 1.2) << "must clear the lane limit, otherwise nothing was lifted";
}
