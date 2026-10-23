#include <algorithm>
#include <string>
#include <vector>

#include "Application/RegionRegistry.h"
#include "Application/SlotTable.h"

namespace golding
{

namespace
{

/**
 * 一个区域：声明 + 它里面的条目。
 *
 * 条目表直接用 `SlotTable`——它和页表、设置表是同一个东西（id 唯一、按 order 排、
 * 带提供方），差别只在规则：这里 `create` 可空（纯动作条目）、同 order 先到先得、
 * 重名是否允许由区域自己声明（`allowDuplicateIds`）。
 */
struct RegionEntry
{
    const GoldingRegionDecl* region = nullptr;
    std::string provider;
    SlotTable<GoldingRegionItemDecl> items;
};

std::vector<RegionEntry>& storage()
{
    static std::vector<RegionEntry> entries;
    return entries;
}

RegistryStats& statsStorage()
{
    static RegistryStats stats;
    return stats;
}

/**区域 id → 表里那一项；没有返回 nullptr。*/
RegionEntry* findEntry(const std::string& id)
{
    for (auto& entry : storage())
    {
        if (entry.region != nullptr && entry.region->id != nullptr && id == entry.region->id)
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

RegionRegistry::RegisterResult RegionRegistry::registerRegion(const GoldingRegionDecl* region,
                                                              const std::string& provider)
{
    if (region == nullptr || region->id == nullptr || region->id[0] == '\0')
    {
        statsStorage().reject("区域指针 / id 为空");
        return RegisterResult::Invalid;
    }
    if (findEntry(region->id) != nullptr)
    {
        statsStorage().reject("区域 id 已存在：" + std::string(region->id));
        return RegisterResult::NameTaken;
    }

    RegionEntry entry;
    entry.region = region;
    entry.provider = provider;
    storage().push_back(std::move(entry));
    statsStorage().accept();
    return RegisterResult::Ok;
}

RegionRegistry::RegisterResult RegionRegistry::registerItem(const GoldingRegionItemDecl* item,
                                                            const std::string& provider)
{
    if (item == nullptr || item->regionId == nullptr || item->regionId[0] == '\0' || item->id == nullptr ||
        item->id[0] == '\0')
    {
        statsStorage().reject("条目指针 / 区域 id / 条目 id 为空");
        return RegisterResult::Invalid;
    }

    RegionEntry* entry = findEntry(item->regionId);
    if (entry == nullptr)
    {
        // 面板插件不在、或没声明这个区域。驳回，但调用方的其它注册照常。
        statsStorage().reject("区域不存在：" + std::string(item->regionId) + "（条目 " + item->id + "）");
        return RegisterResult::UnknownRegion;
    }

    SlotTable<GoldingRegionItemDecl>::Rules rules;
    rules.tieBreak = SlotTieBreak::ByArrival;                            // 面板按注册顺序摆，重排会跳
    rules.requireCreate = false;                                         // 纯动作条目没有 create
    rules.allowDuplicateIds = entry->region->allowDuplicateIds != 0;     // 由区域自己决定
    rules.scope = "区域 " + std::string(item->regionId) + " 内";          // 驳回原因要能定位到区域

    switch (entry->items.insert(item, provider, rules, statsStorage()))
    {
    case SlotTable<GoldingRegionItemDecl>::Result::Ok:
        return RegisterResult::Ok;
    case SlotTable<GoldingRegionItemDecl>::Result::IdTaken:
        return RegisterResult::NameTaken;
    case SlotTable<GoldingRegionItemDecl>::Result::Invalid:
    default:
        return RegisterResult::Invalid;
    }
}

const GoldingRegionDecl* RegionRegistry::findRegion(const std::string& id)
{
    const RegionEntry* entry = findEntry(id);
    return entry != nullptr ? entry->region : nullptr;
}

const std::string& RegionRegistry::regionProviderOf(const std::string& id)
{
    static const std::string empty;
    const RegionEntry* entry = findEntry(id);
    return entry != nullptr ? entry->provider : empty;
}

std::vector<const GoldingRegionDecl*> RegionRegistry::allRegions()
{
    std::vector<const GoldingRegionDecl*> regions;
    regions.reserve(storage().size());
    for (const auto& entry : storage())
    {
        regions.push_back(entry.region);
    }

    std::sort(regions.begin(), regions.end(),
              [](const GoldingRegionDecl* left, const GoldingRegionDecl* right) {
                  if (left->order != right->order)
                  {
                      return left->order < right->order;
                  }
                  return std::string(left->id) < std::string(right->id);
              });
    return regions;
}

std::size_t RegionRegistry::itemCount(const std::string& regionId)
{
    const RegionEntry* entry = findEntry(regionId);
    return entry != nullptr ? entry->items.size() : 0;
}

const GoldingRegionItemDecl* RegionRegistry::itemAt(const std::string& regionId, std::size_t index)
{
    const RegionEntry* entry = findEntry(regionId);
    return entry != nullptr ? entry->items.at(index) : nullptr;
}

const std::string& RegionRegistry::itemProviderOf(const std::string& regionId, const std::string& itemId)
{
    static const std::string empty;
    const RegionEntry* entry = findEntry(regionId);
    return entry != nullptr ? entry->items.providerOf(itemId) : empty;
}

std::size_t RegionRegistry::regionCount()
{
    return storage().size();
}

const RegistryStats& RegionRegistry::stats()
{
    return statsStorage();
}

void RegionRegistry::clear()
{
    storage().clear();
    statsStorage().reset();
}

} // namespace golding
