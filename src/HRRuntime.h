#ifndef HR_RUNTIME_H
#define HR_RUNTIME_H
#include <cstdint>
class Player;
class Creature;
namespace HRRuntime
{
// Startup-only compatibility profiles keep converted characters loadable even
// while enrollment is disabled. This does not open storage or register the NPC.
bool PrepareProfiles();
bool Prepare();
void LoadNpcs();
bool PlaceNpc(Player* player);
bool RemoveNpc(Creature* creature);
bool IsNpc(Creature const* creature);
bool IsEventOnlyDruidRace(std::uint32_t race);
}
#endif
