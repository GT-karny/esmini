#include "gt_esmini/control/manualdrive/ScriptedInputSource.hpp"

#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/common/SimpleJson.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "logger.hpp"

#include <utility>

namespace gt_esmini
{

namespace
{

// PRESENT-BUT-UNPARSEABLE VS ABSENT (see the matching header paragraph next
// to "omitted channel means 0.0"). A channel key that is ABSENT from a
// keyframe object is legitimate and reads as 0.0 (`out` is left at whatever
// the caller pre-set it to -- the Keyframe default). A channel key that IS
// PRESENT but cannot be read as a number (wrong JSON type, or a string that
// does not parse as one) is a MALFORMED PROFILE and must fail Init() loudly
// -- see the header paragraph for why silently coercing it to 0.0 would be
// a fabricated measurement, not a degraded one.
//
// Value::GetDouble() alone cannot make this distinction: it returns false
// for BOTH "key absent" and "key present but not coercible" (SimpleJson.hpp
// -- `if (!value) return false;` and the not-Number/not-coercible-String
// fallthrough both return false the same way). Value::Find() is what
// distinguishes them (nullptr iff the key is textually absent from the
// object), so this helper is built on Find() + the same coercion logic
// GetDouble() uses (Number as-is, String via the public static
// Value::StringToNumber), not a private/duplicated parser. No new SimpleJson
// accessor was needed for this -- Find(), IsNumber()/IsString(), and the
// public static StringToNumber() are all that's required.
//
// Returns true and leaves `out` untouched when `key` is absent from
// `kf_node`. Returns true and writes the coerced value into `out` when
// present and numeric/numeric-string. Returns false (and logs, naming the
// keyframe index and the key) when present but not coercible.
bool ReadOptionalNumericChannel(const simplejson::Value& kf_node,
                                 const char*              key,
                                 double&                  out,
                                 size_t                   keyframe_index,
                                 const std::string&       profile_path)
{
    const simplejson::Value* value = kf_node.Find(key);
    if (!value)
    {
        return true;  // absent: `out` keeps its caller-supplied default (0.0)
    }
    if (value->IsNumber())
    {
        out = value->number_value;
        return true;
    }
    if (value->IsString() && simplejson::Value::StringToNumber(value->string_value, out))
    {
        return true;
    }
    LOG_ERROR(
        "ScriptedInputSource: profile '{}' keyframe {} has a '{}' value that is present but not a "
        "number (rejecting rather than silently defaulting it to 0.0 -- a present-but-unparseable "
        "channel is a malformed profile, not an omitted one)",
        profile_path, keyframe_index, key);
    return false;
}

}  // namespace

// req-vd-ad:REQ-AD-025..031, vd-func:FUNC-075 -- STEP 2: real implementation.
// See ScriptedInputSource.hpp for the profile schema, interpolation rules
// and failure policy; every branch below is pinned by a dedicated TEST in
// test_ScriptedInputSource.cpp.

bool ScriptedInputSource::Init(const ManualDriveConfig& config)
{
    connected_ = false;
    keyframes_.clear();
    clock_s_ = 0.0;

    const std::string& profile_file = config.input_scripted.profile_file;
    if (profile_file.empty())
    {
        LOG_ERROR("ScriptedInputSource: input_scripted.profile_file is empty");
        return false;
    }

    // Config-relative unless absolute -- same convention ConfigLoader uses
    // for other config-referenced files (see the header's PATH RESOLUTION
    // section).
    std::string path = profile_file;
    if (!ConfigLoader::IsAbsolutePath(path) && !config.config_dir.empty())
    {
        path = config.config_dir + "/" + profile_file;
    }

    simplejson::Value root;
    std::string        error;
    if (!simplejson::LoadFile(path, root, &error))
    {
        LOG_ERROR("ScriptedInputSource: failed to load/parse profile '{}' (resolved from '{}'): {}",
                   path, profile_file, error);
        return false;
    }

    const simplejson::Value* keyframes_node = root.Find("keyframes");
    if (!keyframes_node || keyframes_node->type != simplejson::Value::Type::Array ||
        keyframes_node->array_value.empty())
    {
        LOG_ERROR("ScriptedInputSource: profile '{}' has a missing, non-array, or empty 'keyframes'", path);
        return false;
    }

    std::vector<Keyframe> parsed;
    parsed.reserve(keyframes_node->array_value.size());
    for (size_t i = 0; i < keyframes_node->array_value.size(); ++i)
    {
        const simplejson::Value& kf_node = keyframes_node->array_value[i];
        if (kf_node.type != simplejson::Value::Type::Object)
        {
            LOG_ERROR("ScriptedInputSource: profile '{}' has a non-object 'keyframes' entry (index {})", path, i);
            return false;
        }

        Keyframe kf;  // default-constructed: every channel starts at 0.0/0
                      // (int)/0 (uint32_t) -- the "omitted channel means
                      // 0.0" rule falls out of this default plus
                      // ReadOptionalNumericChannel leaving `kf.<field>`
                      // untouched when a key is absent. A key that IS
                      // present but not numeric is rejected instead (see
                      // ReadOptionalNumericChannel above / the header's
                      // "PRESENT-BUT-UNPARSEABLE VS ABSENT" paragraph).
        double t = 0.0;
        if (!kf_node.GetDouble("t", t))
        {
            LOG_ERROR("ScriptedInputSource: profile '{}' has a keyframe with a missing/invalid 't' (index {})",
                       path, i);
            return false;
        }
        kf.t = t;

        if (!ReadOptionalNumericChannel(kf_node, "throttle", kf.throttle, i, path))
        {
            return false;
        }
        if (!ReadOptionalNumericChannel(kf_node, "brake", kf.brake, i, path))
        {
            return false;
        }
        if (!ReadOptionalNumericChannel(kf_node, "steering", kf.steering, i, path))
        {
            return false;
        }

        double gear_raw = 0.0;
        if (!ReadOptionalNumericChannel(kf_node, "gear", gear_raw, i, path))
        {
            return false;
        }
        kf.gear = static_cast<int>(gear_raw);

        double buttons_raw = 0.0;
        if (!ReadOptionalNumericChannel(kf_node, "buttons", buttons_raw, i, path))
        {
            return false;
        }
        kf.buttons = static_cast<uint32_t>(buttons_raw);

        parsed.push_back(kf);
    }

    // Keyframes must be STRICTLY increasing in t. A tie or a reordering both
    // fail: a zero-duration segment has no well-defined interpolation
    // fraction, and this is a load-time correctness check on a verification
    // asset, not a place to silently pick a tie-break rule.
    for (size_t i = 1; i < parsed.size(); ++i)
    {
        if (!(parsed[i].t > parsed[i - 1].t))
        {
            LOG_ERROR(
                "ScriptedInputSource: profile '{}' keyframes are not strictly increasing in 't' "
                "(index {}: t={} is not > previous t={})",
                path, i, parsed[i].t, parsed[i - 1].t);
            return false;
        }
    }

    keyframes_ = std::move(parsed);
    connected_ = true;
    LOG_INFO("ScriptedInputSource: loaded profile '{}' ({} keyframes)", path, keyframes_.size());
    return true;
}

InputFrame ScriptedInputSource::Poll(double dt)
{
    // Advance by the caller-supplied dt only -- deterministic replay against
    // SIMULATION time, independent of wall-clock time (class comment,
    // "self-determinism" guarantee). Accumulate BEFORE sampling: two Poll(0.5)
    // calls land the sample at t=1.0, not t=0.5.
    clock_s_ += dt;

    InputFrame frame;
    frame.connected   = connected_;
    frame.pedal_steer = SampleAt(clock_s_);
    return frame;
}

void ScriptedInputSource::Shutdown()
{
    connected_ = false;
    keyframes_.clear();
    clock_s_ = 0.0;
}

PedalSteerCommand ScriptedInputSource::SampleAt(double t) const
{
    PedalSteerCommand cmd;  // all-zero default

    if (keyframes_.empty())
    {
        return cmd;
    }

    if (t <= keyframes_.front().t)
    {
        const Keyframe& kf = keyframes_.front();
        cmd.throttle = kf.throttle;
        cmd.brake    = kf.brake;
        cmd.steering = kf.steering;
        cmd.gear     = kf.gear;
        cmd.buttons  = kf.buttons;
        return cmd;
    }

    if (t >= keyframes_.back().t)
    {
        const Keyframe& kf = keyframes_.back();
        cmd.throttle = kf.throttle;
        cmd.brake    = kf.brake;
        cmd.steering = kf.steering;
        cmd.gear     = kf.gear;
        cmd.buttons  = kf.buttons;
        return cmd;
    }

    // Find the bracketing pair: keyframes_[i].t <= t < keyframes_[i+1].t.
    size_t i = 0;
    for (; i + 1 < keyframes_.size(); ++i)
    {
        if (t < keyframes_[i + 1].t)
        {
            break;
        }
    }
    const Keyframe& a    = keyframes_[i];
    const Keyframe& b    = keyframes_[i + 1];
    const double    span = b.t - a.t;
    const double    frac = (span > 0.0) ? (t - a.t) / span : 0.0;

    // throttle/brake/steering: LINEAR interpolation between the bracket.
    cmd.throttle = a.throttle + (b.throttle - a.throttle) * frac;
    cmd.brake    = a.brake + (b.brake - a.brake) * frac;
    cmd.steering = a.steering + (b.steering - a.steering) * frac;

    // gear/buttons: STEP-HELD -- the EARLIER bracket keyframe's own stored
    // value ("the value of the most recent keyframe at or before t").
    // Interpolating a bitmask or a gear number is meaningless.
    cmd.gear    = a.gear;
    cmd.buttons = a.buttons;

    return cmd;
}

} // namespace gt_esmini
