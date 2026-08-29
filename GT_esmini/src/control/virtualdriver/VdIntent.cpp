#include "gt_esmini/control/virtualdriver/VdIntent.hpp"

// The ONLY place the intent vocabularies become strings (design vd_intent_layer.md section 3).
//
// Confining stringification to one file is what makes the value set testable as a set: a
// switch with no default cannot compile once an enumerator is added, and test_VdIntent.cpp
// walks every enumerator and asserts the tokens are distinct and round-trip. A free-string
// vocabulary fails silently -- not with a typo, but with another perfectly reasonable word --
// and the symptom is a counter that never increments rather than an exception.

namespace gt_esmini
{

const char* IntentKindName(IntentKind kind)
{
    // No default: adding an IntentKind must break this build, not fall through to a
    // plausible-looking string.
    switch (kind)
    {
        case IntentKind::STOP:        return "stop";
        case IntentKind::SLOW:        return "slow";
        case IntentKind::LANE_CHANGE: return "lane_change";
        case IntentKind::TURN:        return "turn";
        case IntentKind::OVERTAKE:    return "overtake";
        case IntentKind::YIELD:       return "yield";
    }
    return "stop";  // unreachable; keeps MSVC C4715 quiet
}

const char* IntentPhaseName(IntentPhase phase)
{
    switch (phase)
    {
        case IntentPhase::POSSIBLE:   return "possible";
        case IntentPhase::PLANNED:    return "planned";
        case IntentPhase::ANNOUNCED:  return "announced";
        case IntentPhase::EXECUTING:  return "executing";
        case IntentPhase::COMPLETING: return "completing";
        case IntentPhase::ABORTING:   return "aborting";
        case IntentPhase::ABANDONED:  return "abandoned";
    }
    return "possible";
}

const char* IntentWhereName(IntentWhere where)
{
    switch (where)
    {
        // "" is the absence of a position, not a fifth position -- see IntentWhere::NONE.
        case IntentWhere::NONE:     return "";
        case IntentWhere::FRONT:    return "front";
        case IntentWhere::SIDE:     return "side";
        case IntentWhere::REAR:     return "rear";
        case IntentWhere::ONCOMING: return "oncoming";
    }
    return "";
}

}  // namespace gt_esmini
