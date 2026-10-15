#include "../src/HRLearnSpellHook.h"
#include <cassert>
#include <iostream>

struct LegacyCore
{
    explicit LegacyCore(char const*) { }
    virtual ~LegacyCore() = default;
    virtual void OnPlayerLearnSpell(Player*, std::uint32_t) { }
};

struct RecentCore : LegacyCore
{
    using LegacyCore::LegacyCore;
    virtual bool OnPlayerCanLearnSpell(Player*, std::uint32_t) { return true; }
};

struct DifferentSignatureCore : LegacyCore
{
    using LegacyCore::LegacyCore;
    virtual bool OnPlayerCanLearnSpell(Player const*, std::uint32_t) const { return true; }
};

struct Handlers
{
    inline static unsigned before = 0;
    inline static unsigned after = 0;
    inline static std::uint32_t last = 0;
    static bool BeforeLearn(Player*, std::uint32_t id)
    {
        ++before;
        last = id;
        return id == 42;
    }
    static void AfterLearn(Player*, std::uint32_t id)
    {
        ++after;
        last = id;
    }
};

int main()
{
    HRLearnSpellHook<LegacyCore, Handlers> oldHook("old");
    LegacyCore* oldBase = &oldHook;
    oldBase->OnPlayerLearnSpell(nullptr, 19);
    assert(Handlers::after == 1 && Handlers::before == 0 && Handlers::last == 19);

    HRLearnSpellHook<RecentCore, Handlers> recentHook("recent");
    RecentCore* recentBase = &recentHook;
    assert(!recentBase->OnPlayerCanLearnSpell(nullptr, 19));
    assert(recentBase->OnPlayerCanLearnSpell(nullptr, 42));
    assert(Handlers::before == 2 && Handlers::after == 1 && Handlers::last == 42);
    recentBase->OnPlayerLearnSpell(nullptr, 19);
    assert(Handlers::after == 1); // The modern route does not run post-learning removal.

    HRLearnSpellHook<DifferentSignatureCore, Handlers> changedHook("changed");
    LegacyCore* changedBase = &changedHook;
    changedBase->OnPlayerLearnSpell(nullptr, 27);
    assert(Handlers::after == 2 && Handlers::last == 27);
    std::cout << "PASS: virtual dispatch on legacy/recent APIs and exact-signature fallback\n";
}
