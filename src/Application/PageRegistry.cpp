#include <string>
#include <vector>

#include "Application/PageRegistry.h"
#include "Application/SlotTable.h"

namespace golding
{

namespace
{

/**页表本体：规则见 `rules()`。表在函数内静态量里（避开静态初始化顺序问题）。*/
SlotTable<GoldingPageDecl>& table()
{
    static SlotTable<GoldingPageDecl> slots;
    return slots;
}

RegistryStats& statsStorage()
{
    static RegistryStats stats;
    return stats;
}

/**页：必须有 `create`（没有就画不出来），同 order 按 id 字典序——结果与加载先后无关。*/
const SlotTable<GoldingPageDecl>::Rules& rules()
{
    static const SlotTable<GoldingPageDecl>::Rules value{};
    return value;
}

} // namespace

PageRegistry::RegisterResult PageRegistry::registerPage(const GoldingPageDecl* page, const std::string& provider)
{
    switch (table().insert(page, provider, rules(), statsStorage()))
    {
    case SlotTable<GoldingPageDecl>::Result::Ok:
        return RegisterResult::Ok;
    case SlotTable<GoldingPageDecl>::Result::IdTaken:
        return RegisterResult::IdTaken;
    case SlotTable<GoldingPageDecl>::Result::Invalid:
    default:
        return RegisterResult::Invalid;
    }
}

const GoldingPageDecl* PageRegistry::findPage(const std::string& id)
{
    return table().find(id);
}

const std::string& PageRegistry::providerOf(const std::string& id)
{
    return table().providerOf(id);
}

std::vector<const GoldingPageDecl*> PageRegistry::allPages()
{
    return table().all();
}

std::size_t PageRegistry::count()
{
    return table().size();
}

const RegistryStats& PageRegistry::stats()
{
    return statsStorage();
}

void PageRegistry::clear()
{
    table().clear();
    statsStorage().reset();
}

} // namespace golding
