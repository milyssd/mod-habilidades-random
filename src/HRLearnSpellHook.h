#ifndef HR_LEARN_SPELL_HOOK_H
#define HR_LEARN_SPELL_HOOK_H

#include <cstdint>
#include <type_traits>

class Player;

// Old cores expose only the post-learning hook. Keep an actual override on that API.
template<class Base, class Callbacks, class = void>
class HRLearnSpellHook : public Base
{
public:
    explicit HRLearnSpellHook(char const* name) : Base(name) { }
    void OnPlayerLearnSpell(Player* player, std::uint32_t spellId) override
    {
        Callbacks::AfterLearn(player, spellId);
    }
};

// Select the preventive hook only when its exact signature exists in the core headers.
// This detection also works in MSVC; no guessed version number or override suppression is needed.
template<class Base, class Callbacks>
class HRLearnSpellHook<Base, Callbacks, std::void_t<decltype(
    static_cast<bool (Base::*)(Player*, std::uint32_t)>(&Base::OnPlayerCanLearnSpell))>> : public Base
{
public:
    explicit HRLearnSpellHook(char const* name) : Base(name) { }
    bool OnPlayerCanLearnSpell(Player* player, std::uint32_t spellId) override
    {
        return Callbacks::BeforeLearn(player, spellId);
    }
};

#endif
