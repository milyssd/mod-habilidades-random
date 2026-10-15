#ifndef HR_CHARACTER_SAVE_H
#define HR_CHARACTER_SAVE_H

#include "CharacterDatabase.h"
#include "Transaction.h"
#include <memory>

// Player::SaveToDB builds the usual core statements in this transaction.
// Expose them only to execute the same save with checked transaction boundaries.
class HRCharacterSaveTransaction : public Transaction<CharacterDatabaseConnection>
{
public:
    std::vector<SQLElementData> const& Statements() const { return m_queries; }
};

bool HRCommitCharacterSave(std::shared_ptr<HRCharacterSaveTransaction> const& transaction);
void HRCloseCharacterSave();

#endif
