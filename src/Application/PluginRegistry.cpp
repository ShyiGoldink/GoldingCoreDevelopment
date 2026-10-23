#include <vector>

#include "Application/PluginRegistry.h"

namespace golding
{

namespace
{

struct Entry
{
    std::string name;
    const GoldingWidgetClass* widgetClass = nullptr;
    std::string provider;
};

std::vector<Entry>& storage()
{
    static std::vector<Entry> entries;
    return entries;
}

RegistryStats& statsStorage()
{
    static RegistryStats stats;
    return stats;
}

} // namespace

PluginRegistry::RegisterResult PluginRegistry::registerClass(const GoldingWidgetClass* widgetClass,
                                                             const std::string& provider)
{
    if (widgetClass == nullptr || widgetClass->name == nullptr || widgetClass->name[0] == '\0' ||
        widgetClass->create == nullptr)
    {
        statsStorage().reject("组件类指针 / 名字为空，或没有 create");
        return RegisterResult::Invalid;
    }

    const std::string name(widgetClass->name);
    for (const auto& entry : storage())
    {
        if (entry.name == name)
        {
            statsStorage().reject("同名已存在：" + name);
            return RegisterResult::NameTaken;
        }
    }

    storage().push_back(Entry{name, widgetClass, provider});
    statsStorage().accept();
    return RegisterResult::Ok;
}

const GoldingWidgetClass* PluginRegistry::findClass(const std::string& name)
{
    for (const auto& entry : storage())
    {
        if (entry.name == name)
        {
            return entry.widgetClass;
        }
    }
    return nullptr;
}

const std::string& PluginRegistry::providerOf(const std::string& name)
{
    static const std::string empty;
    for (const auto& entry : storage())
    {
        if (entry.name == name)
        {
            return entry.provider;
        }
    }
    return empty;
}

std::vector<std::string> PluginRegistry::allNames()
{
    std::vector<std::string> names;
    names.reserve(storage().size());
    for (const auto& entry : storage())
    {
        names.push_back(entry.name);
    }
    return names;
}

std::size_t PluginRegistry::count()
{
    return storage().size();
}

const RegistryStats& PluginRegistry::stats()
{
    return statsStorage();
}

void PluginRegistry::clear()
{
    storage().clear();
    statsStorage().reset();
}

} // namespace golding
