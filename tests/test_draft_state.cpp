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
    stats.rerolls = stats.bans = 0;
    assert(!stats.CanReroll(true));
    assert(!stats.CanBan(200, false, true));
    stats.successful = stats.expected;
    assert(!stats.CanSelect(100, false));
    // Old inconsistent data must not underflow into billions of pending drafts.
    stats.successful = stats.expected + 1;
    assert(stats.Remaining() == 0);
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
    std::cout << "PASS: forged choices, bans, exhausted pools, balances, opt-in and level-80 boundary\n";
}
