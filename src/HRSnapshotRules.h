#ifndef HR_SNAPSHOT_RULES_H
#define HR_SNAPSHOT_RULES_H

#include <cstdint>

// Restore every saved spec membership independently of the active spec.
// readMask reads the talent record itself because some cores ignore the spec
// argument in Player::HasTalent. Verify again after additions, which may fail.
template<class ReadMask, class AddMask>
bool HRRestoreTalentSpecs(std::uint8_t saved, std::uint8_t specs, ReadMask&& readMask, AddMask&& addMask)
{
    std::uint8_t missing = std::uint8_t(saved & ~readMask());
    for (std::uint8_t spec = 0; spec < specs; ++spec)
        if (missing & (1u << spec))
            addMask(std::uint8_t(1u << spec));
    return (saved & ~readMask()) == 0;
}

#endif
