#include <string>
#include <vector>

#include "Application/ServiceRegistry.h"

namespace golding
{

namespace
{

struct Entry
{
    std::string name;
    const void* vtable = nullptr;
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

ServiceRegistry::RegisterResult ServiceRegistry::registerService(const char* name, const void* vtable)
{
    if (name == nullptr || name[0] == '\0' || vtable == nullptr)
    {
        statsStorage().reject("服务名 / vtable 为空");
        return RegisterResult::Invalid;
    }

    const std::string serviceName(name);
    for (const auto& entry : storage())
    {
        if (entry.name == serviceName)
        {
            statsStorage().reject("服务名已存在：" + serviceName);
            return RegisterResult::NameTaken;
        }
    }

    storage().push_back(Entry{serviceName, vtable});
    statsStorage().accept();
    return RegisterResult::Ok;
}

const void* ServiceRegistry::queryService(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const auto& entry : storage())
    {
        if (entry.name == name)
        {
            return entry.vtable;
        }
    }
    return nullptr;
}

std::vector<std::string> ServiceRegistry::allNames()
{
    std::vector<std::string> names;
    names.reserve(storage().size());
    for (const auto& entry : storage())
    {
        names.push_back(entry.name);
    }
    return names;
}

const RegistryStats& ServiceRegistry::stats()
{
    return statsStorage();
}

void ServiceRegistry::clear()
{
    storage().clear();
    statsStorage().reset();
}

} // namespace golding
