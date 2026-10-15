#ifndef HR_DRAFT_STATE_H
#define HR_DRAFT_STATE_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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
    bool HasOffer() const
    {
        return offered[0] && offered[1] && offered[2] && offered[0] != offered[1] &&
            offered[0] != offered[2] && offered[1] != offered[2];
    }
    bool Contains(std::uint32_t id) const
    {
        return id && (offered[0] == id || offered[1] == id || offered[2] == id);
    }
    bool CanSelect(std::uint32_t id, bool banned) const { return Remaining() && HasOffer() && Contains(id) && !banned; }
    bool CanReroll(bool replacementAvailable) const { return Remaining() && HasOffer() && rerolls && replacementAvailable; }
    bool CanBan(std::uint32_t id, bool banned, bool replacementAvailable) const
    {
        return bans && CanSelect(id, banned) && replacementAvailable;
    }

    void AwardLevels(std::uint8_t level, std::uint32_t draftsPerLevel, std::uint32_t rerollsPerLevel)
    {
        if (level <= lastLevel)
            return;
        std::uint32_t difference = level - lastLevel;
        auto add = [difference](std::uint32_t balance, std::uint32_t perLevel)
        {
            std::uint64_t total = std::uint64_t(balance) + std::uint64_t(difference) * perLevel;
            constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
            return total > maximum ? maximum : std::uint32_t(total);
        };
        expected = add(expected, draftsPerLevel);
        rerolls = add(rerolls, rerollsPerLevel);
        lastLevel = level;
    }
};

// A category's weight is relative to other nonempty categories, not its spell count.
// Return weights.size() when no usable category exists or the roll is invalid.
inline std::size_t HRChooseWeightedCategory(std::array<std::size_t, 6> const& counts,
    std::array<float, 6> const& weights, double unitRoll)
{
    if (!std::isfinite(unitRoll))
        return weights.size();
    double total = 0;
    std::size_t last = weights.size();
    for (std::size_t i = 0; i < weights.size(); ++i)
        if (counts[i] && std::isfinite(weights[i]) && weights[i] > 0)
        {
            total += weights[i];
            last = i;
        }
    if (!total)
        return weights.size();
    if (unitRoll < 0)
        unitRoll = 0;
    if (unitRoll >= 1)
        unitRoll = std::nextafter(1.0, 0.0);
    double threshold = unitRoll * total;
    double cumulative = 0;
    for (std::size_t i = 0; i < weights.size(); ++i)
        if (counts[i] && std::isfinite(weights[i]) && weights[i] > 0)
        {
            cumulative += weights[i];
            if (threshold < cumulative)
                return i;
        }
    // Floating-point rounding at the upper endpoint still selects a usable category.
    return last;
}

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
