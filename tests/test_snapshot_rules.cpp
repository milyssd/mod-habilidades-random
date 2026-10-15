#include "../src/HRSnapshotRules.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>

struct LegacyPlayer
{
    std::uint8_t active = 0;
    std::uint8_t mask = 0;
    unsigned additions = 0;
    bool refuse = false;
    bool HasTalent(unsigned /*spec*/) const { return (mask & (1u << active)) != 0; }
    void AddTalent(std::uint8_t specMask)
    {
        ++additions;
        if (!refuse)
            mask |= specMask;
    }
};

bool Restore(LegacyPlayer& player, std::uint8_t saved)
{
    return HRRestoreTalentSpecs(saved, 2, [&] { return player.mask; },
        [&](std::uint8_t mask) { player.AddTalent(mask); });
}

int main()
{
    for (std::uint8_t active : {0, 1})
    {
        // Reproduce the uploaded core's misleading HasTalent(id, spec): it
        // reports the active membership even when the inactive spec is asked.
        LegacyPlayer shared{active, std::uint8_t(1u << active)};
        assert(shared.HasTalent(1 - active));
        assert(Restore(shared, 3));
        assert(shared.mask == 3 && shared.additions == 1);

        LegacyPlayer inactive{active};
        std::uint8_t inactiveMask = std::uint8_t(1u << (1 - active));
        assert(Restore(inactive, inactiveMask));
        assert(inactive.mask == inactiveMask && inactive.additions == 1);
        assert(!inactive.HasTalent(1 - active));
        assert(Restore(inactive, inactiveMask));
        assert(inactive.additions == 1);
    }
    LegacyPlayer failed;
    failed.refuse = true;
    assert(!Restore(failed, 3));
    assert(failed.additions == 2);
}
