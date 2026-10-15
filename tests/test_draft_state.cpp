#include "../src/HRDraftState.h"
#include <cassert>
#include <iostream>

int main()
{
    // A forged or stale click cannot spend a draft; bans apply on the server too.
    HRStats stats;
    stats.expected = 2;
    stats.rerolls = 1;
    stats.bans = 1;
    stats.offered = {100, 200, 300};
    assert(stats.CanSelect(100, false));
    assert(!stats.CanSelect(999, false));
    assert(!stats.CanSelect(100, true));
    assert(!stats.CanSelect(0, false));
    // Exhausted replacement pool must preserve rerolls and bans.
    assert(!stats.CanReroll(false));
    assert(!stats.CanBan(200, false, false));
    assert(stats.CanReroll(true));
    assert(stats.CanBan(200, false, true));
    // An empty or malformed saved offer cannot consume rerolls, bans or choices.
    for (auto offer : {std::array<std::uint32_t, 3>{}, {100, 200, 0}, {100, 100, 200}})
    {
        stats.offered = offer;
        assert(!stats.HasOffer());
        assert(!stats.CanSelect(100, false));
        assert(!stats.CanReroll(true));
        assert(!stats.CanBan(100, false, true));
    }
    stats.offered = {100, 200, 300};
    stats.rerolls = stats.bans = 0;
    assert(!stats.CanReroll(true));
    assert(!stats.CanBan(200, false, true));
    stats.successful = stats.expected;
    assert(!stats.CanSelect(100, false));
    // Old inconsistent data must not underflow into billions of pending drafts.
    stats.successful = stats.expected + 1;
    assert(stats.Remaining() == 0);
    // A multi-level jump awards every level once; lowering and re-raising cannot farm balances.
    HRStats levels;
    levels.lastLevel = 1;
    levels.expected = 1;
    levels.rerolls = 5;
    levels.bans = 5;
    levels.offered = {10, 20, 30};
    levels.AwardLevels(5, 1, 2);
    assert(levels.lastLevel == 5 && levels.expected == 5 && levels.rerolls == 13);
    levels.AwardLevels(2, 1, 2);
    levels.AwardLevels(5, 1, 2);
    assert(levels.lastLevel == 5 && levels.expected == 5 && levels.rerolls == 13);
    levels.AwardLevels(8, 3, 4);
    assert(levels.lastLevel == 8 && levels.expected == 14 && levels.rerolls == 25);
    assert(levels.bans == 5 && levels.offered == (std::array<std::uint32_t, 3>{10, 20, 30}));
    // Stored balances or oversized rates must not wrap into a smaller number.
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    levels.expected = maximum - 1;
    levels.rerolls = maximum - 1;
    levels.AwardLevels(10, maximum, maximum);
    assert(levels.expected == maximum && levels.rerolls == maximum && levels.lastLevel == 10);
    levels.AwardLevels(11, 0, 0);
    assert(levels.expected == maximum && levels.rerolls == maximum && levels.lastLevel == 11);
    // Relative category weights retain small probabilities without integer quotas.
    std::array<std::size_t, 6> counts = {100, 1, 0, 0, 0, 1};
    std::array<float, 6> weights = {0.50f, 0.27f, 0.14f, 0.06f, 0.03f, 0.01f};
    assert(HRChooseWeightedCategory(counts, weights, 0.0) == 0);
    assert(HRChooseWeightedCategory(counts, weights, 0.5) == 0);
    assert(HRChooseWeightedCategory(counts, weights, 0.7) == 1);
    assert(HRChooseWeightedCategory(counts, weights, 0.999) == 5);
    assert(HRChooseWeightedCategory(counts, weights, -1.0) == 0);
    assert(HRChooseWeightedCategory(counts, weights, 1.0) == 5);
    // Depleting a category immediately redistributes its probability to those left.
    counts[0] = 0;
    assert(HRChooseWeightedCategory(counts, weights, 0.0) == 1);
    assert(HRChooseWeightedCategory(counts, weights, 0.98) == 5);
    // Zero, negative and nonfinite weights never participate, even when spells exist.
    weights[1] = 0;
    assert(HRChooseWeightedCategory(counts, weights, 0.0) == 5);
    weights[1] = -1;
    assert(HRChooseWeightedCategory(counts, weights, 0.0) == 5);
    weights[1] = std::numeric_limits<float>::quiet_NaN();
    weights[5] = std::numeric_limits<float>::infinity();
    assert(HRChooseWeightedCategory(counts, weights, 0.0) == 6);
    weights = {};
    assert(HRChooseWeightedCategory(counts, weights, 0.5) == 6);
    weights[1] = 1;
    counts = {};
    assert(HRChooseWeightedCategory(counts, weights, 0.5) == 6);
    counts[1] = 1;
    assert(HRChooseWeightedCategory(counts, weights, std::numeric_limits<double>::quiet_NaN()) == 6);
    assert(HRChooseWeightedCategory(counts, weights, std::numeric_limits<double>::infinity()) == 6);
    // Participation is explicit; level 80 triggers restoration and completion unlocks talents.
    assert(!HRShouldRestore(false, false, 80));
    assert(!HRShouldRestore(true, false, 1));
    assert(!HRShouldRestore(true, false, 79));
    assert(HRShouldRestore(true, false, 80));
    assert(HRShouldRestore(true, false, 81));
    assert(!HRShouldRestore(true, true, 80));
    assert(HRTalentsLocked(true, false, false));
    assert(!HRTalentsLocked(false, false, false));
    assert(!HRTalentsLocked(true, true, false));
    assert(!HRTalentsLocked(true, false, true));
    std::cout << "PASS: forged/malformed choices, exhausted pools, saturated level awards, weighted rarities and level-80 boundary\n";
}
