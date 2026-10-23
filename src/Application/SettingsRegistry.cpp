#include <string>
#include <vector>

#include "Application/SettingsRegistry.h"
#include "Application/SlotTable.h"

namespace golding
{

namespace
{

SlotTable<GoldingSettingsDecl>& table()
{
    static SlotTable<GoldingSettingsDecl> slots;
    return slots;
}

RegistryStats& statsStorage()
{
    static RegistryStats stats;
    return stats;
}

/**设置区块：与页同一套规则——`create` 必需，同 order 按 id 字典序。*/
const SlotTable<GoldingSettingsDecl>::Rules& rules()
{
    static const SlotTable<GoldingSettingsDecl>::Rules value{};
    return value;
}

} // namespace

SettingsRegistry::RegisterResult SettingsRegistry::registerSection(const GoldingSettingsDecl* section,
                                                                   const std::string& provider)
{
    switch (table().insert(section, provider, rules(), statsStorage()))
    {
    case SlotTable<GoldingSettingsDecl>::Result::Ok:
        return RegisterResult::Ok;
    case SlotTable<GoldingSettingsDecl>::Result::IdTaken:
        return RegisterResult::IdTaken;
    case SlotTable<GoldingSettingsDecl>::Result::Invalid:
    default:
        return RegisterResult::Invalid;
    }
}

const GoldingSettingsDecl* SettingsRegistry::findSection(const std::string& id)
{
    return table().find(id);
}

const std::string& SettingsRegistry::providerOf(const std::string& id)
{
    return table().providerOf(id);
}

std::vector<const GoldingSettingsDecl*> SettingsRegistry::allSections()
{
    return table().all();
}

std::size_t SettingsRegistry::count()
{
    return table().size();
}

const RegistryStats& SettingsRegistry::stats()
{
    return statsStorage();
}

void SettingsRegistry::clear()
{
    table().clear();
    statsStorage().reset();
}

} // namespace golding
