#ifndef HR_DRAFT_STATE_H
#define HR_DRAFT_STATE_H

#include <array>
#include <cstdint>

// Pure draft rules shared by the server and regression tests.
struct HRStats
{
    std::uint32_t playerId = 0;
    std::uint32_t successful = 0;
    std::uint32_t expected = 0;
    std::uint32_t rerolls = 0;
    std::uint32_t bans = 0;
    std::array<std::uint32_t, 3> offered{};
    std::uint8_t lastLevel = 0;

    std::uint32_t Remaining() const { return expected > successful ? expected - successful : 0; }
    bool Contains(std::uint32_t id) const
    {
        return id && (offered[0] == id || offered[1] == id || offered[2] == id);
    }
    bool CanSelect(std::uint32_t id, bool banned) const { return Remaining() && Contains(id) && !banned; }
    bool CanReroll(bool replacementAvailable) const { return Remaining() && rerolls && replacementAvailable; }
    bool CanBan(std::uint32_t id, bool banned, bool replacementAvailable) const
    {
        return bans && CanSelect(id, banned) && replacementAvailable;
    }
};

inline bool HRShouldRestore(bool enrolled, bool completed, std::uint8_t level)
{
    return enrolled && !completed && level >= 80;
}

inline bool HRTalentsLocked(bool enrolled, bool completed, bool transitioning)
{
    // Includes the short safe-restoration window after reaching 80 in combat.
    return enrolled && !completed && !transitioning;
}

#endif
