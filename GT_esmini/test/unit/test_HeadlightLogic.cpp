/*
 * GT_esmini - F6 AutoLight environment-driven headlight pure-logic unit tests.
 *
 * Covers the dependency-free decision layer (HeadlightLogic.hpp): night decision,
 * TimeOfDay hour parsing, tunnel s-interval, forward-corridor projection and the
 * auto-high-beam hysteresis FSM. Integration behaviour is verified separately by
 * the GT_Loader integration tests.
 */

#include <gtest/gtest.h>

#include "gt_esmini/control/HeadlightLogic.hpp"

namespace gt_esmini
{
namespace headlight
{

// ---------------------------- DecideNight ----------------------------------

TEST(HeadlightNight, IlluminanceTakesPriority)
{
    HeadlightConfig cfg;  // threshold 3000 lux
    EnvSnapshot     env;
    env.has_illuminance = true;

    env.illuminance_lux = 100000.0;  // bright daylight
    EXPECT_EQ(DecideNight(env, cfg), NightState::DAY);

    env.illuminance_lux = 50.0;  // dim
    EXPECT_EQ(DecideNight(env, cfg), NightState::NIGHT);

    // Illuminance wins even when a (contradicting) sun elevation is present.
    env.has_sun_elevation = true;
    env.sun_elevation_rad = 1.0;  // sun well up
    env.illuminance_lux   = 10.0;
    EXPECT_EQ(DecideNight(env, cfg), NightState::NIGHT);
}

TEST(HeadlightNight, SunElevationFallback)
{
    HeadlightConfig cfg;  // elevation threshold 0 rad (horizon)
    EnvSnapshot     env;
    env.has_sun_elevation = true;

    env.sun_elevation_rad = 0.4;  // sun above horizon
    EXPECT_EQ(DecideNight(env, cfg), NightState::DAY);

    env.sun_elevation_rad = -0.2;  // below horizon
    EXPECT_EQ(DecideNight(env, cfg), NightState::NIGHT);

    env.sun_elevation_rad = 0.0;  // exactly horizon -> night (<=)
    EXPECT_EQ(DecideNight(env, cfg), NightState::NIGHT);
}

TEST(HeadlightNight, TimeOfDayFallback)
{
    HeadlightConfig cfg;  // dusk 19, dawn 6
    EnvSnapshot     env;
    env.has_time_of_day = true;

    env.date_time = "2025-06-13T21:00:00.000+00:00";  // 21h -> night
    EXPECT_EQ(DecideNight(env, cfg), NightState::NIGHT);

    env.date_time = "2025-06-13T12:00:00.000+00:00";  // noon -> day
    EXPECT_EQ(DecideNight(env, cfg), NightState::DAY);

    env.date_time = "2025-06-13T03:00:00.000+00:00";  // 3am -> night
    EXPECT_EQ(DecideNight(env, cfg), NightState::NIGHT);
}

TEST(HeadlightNight, TimeOfDayDisabledOrEmptyIsUnknown)
{
    HeadlightConfig cfg;
    EnvSnapshot     env;  // nothing set
    EXPECT_EQ(DecideNight(env, cfg), NightState::UNKNOWN);

    env.has_time_of_day = true;
    env.date_time       = "not-a-date";
    EXPECT_EQ(DecideNight(env, cfg), NightState::UNKNOWN);

    env.date_time      = "2025-06-13T21:00:00";
    cfg.use_time_of_day = false;  // disabled -> unknown
    EXPECT_EQ(DecideNight(env, cfg), NightState::UNKNOWN);
}

// ---------------------------- Hour parsing ---------------------------------

TEST(HeadlightHour, ParsesIso8601)
{
    int hour = -1;
    EXPECT_TRUE(ParseHourFromDateTime("2025-06-13T00:00:00.000+00:00", hour));
    EXPECT_EQ(hour, 0);
    EXPECT_TRUE(ParseHourFromDateTime("2025-10-15T23:59:59Z", hour));
    EXPECT_EQ(hour, 23);
    EXPECT_TRUE(ParseHourFromDateTime("2025-01-01T07:30:00", hour));
    EXPECT_EQ(hour, 7);
}

TEST(HeadlightHour, RejectsMalformed)
{
    int hour = -1;
    EXPECT_FALSE(ParseHourFromDateTime("2025-06-13", hour));      // no T
    EXPECT_FALSE(ParseHourFromDateTime("2025-06-13T9:00", hour)); // single digit -> ':' not digit
    EXPECT_FALSE(ParseHourFromDateTime("junkTxx:00", hour));      // non-digit hour
    EXPECT_FALSE(ParseHourFromDateTime("T", hour));               // truncated
}

TEST(HeadlightHour, NightWindowWraps)
{
    // dawn 6, dusk 19: night before 6 and at/after 19.
    EXPECT_TRUE(IsHourNight(5, 6.0, 19.0));
    EXPECT_FALSE(IsHourNight(6, 6.0, 19.0));
    EXPECT_FALSE(IsHourNight(18, 6.0, 19.0));
    EXPECT_TRUE(IsHourNight(19, 6.0, 19.0));
    EXPECT_TRUE(IsHourNight(23, 6.0, 19.0));
    EXPECT_TRUE(IsHourNight(0, 6.0, 19.0));
}

// ---------------------------- Tunnel ---------------------------------------

TEST(HeadlightTunnel, PointInInterval)
{
    // tunnel from s=100 length 50 -> [100,150]
    EXPECT_FALSE(PointInTunnel(99.9, 100.0, 50.0));
    EXPECT_TRUE(PointInTunnel(100.0, 100.0, 50.0));
    EXPECT_TRUE(PointInTunnel(125.0, 100.0, 50.0));
    EXPECT_TRUE(PointInTunnel(150.0, 100.0, 50.0));
    EXPECT_FALSE(PointInTunnel(150.1, 100.0, 50.0));
}

// ---------------------------- Forward corridor -----------------------------

TEST(HeadlightScan, ForwardDistanceAlongHeading)
{
    // Ego heading = 0 (east). Vehicle 30 m ahead, centred.
    EXPECT_NEAR(ForwardDistanceInCorridor(30.0, 0.0, 1.0, 0.0, 3.0), 30.0, 1e-9);

    // Behind -> negative sentinel.
    EXPECT_LT(ForwardDistanceInCorridor(-30.0, 0.0, 1.0, 0.0, 3.0), 0.0);

    // Ahead but outside the lateral corridor.
    EXPECT_LT(ForwardDistanceInCorridor(30.0, 10.0, 1.0, 0.0, 3.0), 0.0);

    // Oncoming in adjacent lane (lateral ~3.5) is caught by a 6 m half-corridor.
    EXPECT_NEAR(ForwardDistanceInCorridor(40.0, 3.5, 1.0, 0.0, 6.0), 40.0, 1e-9);

    // Heading = 90 deg (north): a vehicle to the north is "ahead".
    EXPECT_NEAR(ForwardDistanceInCorridor(0.0, 25.0, 0.0, 1.0, 3.0), 25.0, 1e-9);
}

// ---------------------------- High-beam hysteresis -------------------------

TEST(HeadlightHighBeam, RaisesAfterOnDelayWhenClear)
{
    HeadlightConfig cfg;
    cfg.highbeam_on_delay_s  = 1.0;
    cfg.highbeam_off_delay_s = 0.3;
    HighBeamHysteresis hb;

    // Road clear (no target => -1). Before on_delay elapses, stays low.
    EXPECT_FALSE(hb.Update(-1.0, 0.5, cfg));
    EXPECT_FALSE(hb.Update(-1.0, 0.4, cfg));  // 0.9 s total
    EXPECT_TRUE(hb.Update(-1.0, 0.2, cfg));   // 1.1 s -> high
}

TEST(HeadlightHighBeam, DimsAfterOffDelayWhenVehicleAppears)
{
    HeadlightConfig cfg;
    cfg.highbeam_on_delay_s  = 1.0;
    cfg.highbeam_off_delay_s = 0.3;
    cfg.highbeam_range_m     = 100.0;
    HighBeamHysteresis hb;

    // Get to high beam first.
    hb.Update(-1.0, 1.5, cfg);
    EXPECT_TRUE(hb.IsHigh());

    // Vehicle at 50 m (within range). After off_delay it dims.
    EXPECT_TRUE(hb.Update(50.0, 0.2, cfg));   // 0.2 s < 0.3 -> still high
    EXPECT_FALSE(hb.Update(50.0, 0.2, cfg));  // 0.4 s -> dim
}

TEST(HeadlightHighBeam, DistanceSchmittPreventsFlicker)
{
    HeadlightConfig cfg;
    cfg.highbeam_on_delay_s        = 0.2;
    cfg.highbeam_off_delay_s       = 0.2;
    cfg.highbeam_range_m           = 100.0;
    cfg.highbeam_range_hysteresis_m = 20.0;  // clear only beyond 120 m
    HighBeamHysteresis hb;

    // Vehicle just inside range -> becomes present -> dims.
    hb.Update(95.0, 0.3, cfg);
    EXPECT_FALSE(hb.IsHigh());

    // Vehicle drifts to 110 m: inside the hysteresis band [100,120] -> still
    // "present", stays dimmed (no flicker back to high).
    hb.Update(110.0, 0.3, cfg);
    EXPECT_FALSE(hb.IsHigh());

    // Beyond 120 m -> clear -> raises again after on_delay.
    hb.Update(130.0, 0.3, cfg);
    EXPECT_TRUE(hb.IsHigh());
}

}  // namespace headlight
}  // namespace gt_esmini
