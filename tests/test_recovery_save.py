"""Exercise the real recovery method with a logout that tries to save again."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def recovery_method():
    source = (ROOT / 'src/HabilidadesRandom.cpp').read_text(encoding='utf-8')
    start = source.index('void HabilidadesRandomMgr::RequireRecovery(Player* player)')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


FIXTURE = r'''
#include <cassert>
#include <string>
#include <unordered_set>

struct Database
{
    int gold = 100;
    bool item = false;
    unsigned writes = 0;
};

struct Player;
struct Session
{
    Player* player;
    bool disconnected = false;
    void KickPlayer(std::string const& reason);
};

struct Player
{
    Database& database;
    int gold = 90;
    bool item = true;
    bool itemDirty = true;
    bool suppressed = false;
    Session session{this};

    void SetModuleSaveSuppressed(bool value) { suppressed = value; }
    Session* GetSession() { return &session; }

    // Model the core's consumed dirty flags followed by rollback, or by a
    // committed transaction whose COMMIT acknowledgement was lost.
    void UnconfirmedSave(bool committed)
    {
        itemDirty = false;
        if (committed)
        {
            database.gold = gold;
            database.item = item;
        }
    }

    void SaveGoldToDB()
    {
        if (suppressed)
            return;
        database.gold = gold;
        ++database.writes;
    }
    void SaveInventoryAndGoldToDB()
    {
        if (suppressed)
            return;
        if (itemDirty)
        {
            database.item = item;
            itemDirty = false;
        }
        SaveGoldToDB();
    }
    void SaveToDB() { SaveInventoryAndGoldToDB(); }
};

void Session::KickPlayer(std::string const& reason)
{
    assert(!reason.empty());
    // Logout calls SaveToDB before OnPlayerLogout. Also exercise the two
    // partial-save routes that could run while the disconnection is pending.
    player->SaveToDB();
    player->SaveInventoryAndGoldToDB();
    player->SaveGoldToDB();
    disconnected = true;
}

unsigned Guid(Player const*) { return 7; }
class HabilidadesRandomMgr
{
public:
    std::unordered_set<unsigned> _recoveryRequired;
    void Close(Player* player)
    {
#ifdef ACORE_MODULE_SAVE_GUARD
        // Protection must be set before any work that could disconnect/save.
        assert(player->suppressed);
#else
        (void)player;
#endif
    }
    void RequireRecovery(Player* player);
};

// Inserted below: the actual module method, including its compile-time guard.
'''

MAIN = r'''
int main()
{
    for (bool committed : {false, true})
    {
        Database database;
        Player player{database};
        player.UnconfirmedSave(committed);
        HabilidadesRandomMgr manager;
        manager.RequireRecovery(&player);
        assert(player.session.disconnected);
        assert(manager._recoveryRequired.count(Guid(&player)));
#ifdef ACORE_MODULE_SAVE_GUARD
        assert(database.writes == 0);
        assert(database.gold == (committed ? 90 : 100));
        assert(database.item == committed);
#else
        // Negative control: disconnecting alone recreates the reported loss.
        assert(database.writes > 0);
        assert(database.gold == 90);
        assert(database.item == committed);
#endif
    }
}
'''


@unittest.skipUnless(shutil.which('g++'), 'G++ is required for the recovery regression')
class RecoverySaveTests(unittest.TestCase):
    def run_fixture(self, guarded):
        with tempfile.TemporaryDirectory(prefix='hr recovery ') as directory:
            source = Path(directory) / 'recovery.cpp'
            executable = Path(directory) / 'recovery'
            source.write_text(FIXTURE + recovery_method() + MAIN, encoding='utf-8')
            command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror']
            if guarded:
                command.append('-DACORE_MODULE_SAVE_GUARD=1')
            subprocess.run(command + [str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_real_recovery_suppresses_logout_and_partial_saves_before_disconnect(self):
        self.run_fixture(True)

    def test_negative_control_disconnect_alone_loses_the_uncommitted_item(self):
        self.run_fixture(False)


if __name__ == '__main__':
    unittest.main()
