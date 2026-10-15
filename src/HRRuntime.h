#ifndef HR_RUNTIME_H
#define HR_RUNTIME_H
#include <cstdint>
class Player;
class Creature;
namespace HRRuntime
{
bool Prepare();
void LoadNpcs();
bool PlaceNpc(Player* player);
bool RemoveNpc(Creature* creature);
bool IsNpc(Creature const* creature);
bool IsEventOnlyDruidRace(std::uint32_t race);
}
#endif
