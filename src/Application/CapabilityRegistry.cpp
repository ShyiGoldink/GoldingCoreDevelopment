#include <string>
#include <vector>

#include "Application/CapabilityRegistry.h"
#include "Application/ServiceRegistry.h"

namespace golding
{

namespace
{

/**声明面的一行：名字 + 声称提供它的插件。字符串存在 deque 里，指针全程有效。*/
struct Declaration
{
    std::string name;
    std::string declarer;
};

/**运行时面的一行：谁真的注册了。vtable 的真身在 `ServiceRegistry` 里。*/
struct ProviderRow
{
    std::string name;
    std::string provider;
};

std::vector<Declaration>& declarations()
{
    static std::vector<Declaration> rows;
    return rows;
}

std::vector<ProviderRow>& providers()
{
    static std::vector<ProviderRow> rows;
    return rows;
}

bool& strictFlag()
{
    static bool strict = true;
    return strict;
}

const Declaration* findDeclaration(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const auto& row : declarations())
    {
        if (row.name == name)
        {
            return &row;
        }
    }
    return nullptr;
}

const ProviderRow* findProvider(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const auto& row : providers())
    {
        if (row.name == name)
        {
            return &row;
        }
    }
    return nullptr;
}

} // namespace

void CapabilityRegistry::declare(const char* name, const char* declarer)
{
    if (name == nullptr || name[0] == '\0')
    {
        return;
    }
    if (findDeclaration(name) != nullptr)
    {
        return; // 同名重复声明：第一个说了算，后面的不覆盖
    }
    declarations().push_back(Declaration{name, declarer != nullptr ? declarer : "?"});
}

bool CapabilityRegistry::isDeclared(const char* name)
{
    return findDeclaration(name) != nullptr;
}

const char* CapabilityRegistry::declarerOf(const char* name)
{
    const Declaration* row = findDeclaration(name);
    return row != nullptr ? row->declarer.c_str() : nullptr;
}

CapabilityRegistry::RegisterResult CapabilityRegistry::registerCapability(const char* name, const void* vtable,
                                                                          const char* provider)
{
    // 先落 ServiceRegistry：同名驳回、非法参数都在那里判，判定规则只留一份。
    switch (ServiceRegistry::registerService(name, vtable))
    {
    case ServiceRegistry::RegisterResult::Ok:
        providers().push_back(ProviderRow{name, provider != nullptr ? provider : "?"});
        return RegisterResult::Ok;
    case ServiceRegistry::RegisterResult::NameTaken:
        return RegisterResult::NameTaken;
    case ServiceRegistry::RegisterResult::Invalid:
    default:
        return RegisterResult::Invalid;
    }
}

const void* CapabilityRegistry::query(const char* name)
{
    return ServiceRegistry::queryService(name);
}

const char* CapabilityRegistry::providerOf(const char* name)
{
    const ProviderRow* row = findProvider(name);
    return row != nullptr ? row->provider.c_str() : nullptr;
}

std::vector<std::string> CapabilityRegistry::allNames()
{
    return ServiceRegistry::allNames();
}

void CapabilityRegistry::setStrict(bool strict)
{
    strictFlag() = strict;
}

bool CapabilityRegistry::strict()
{
    return strictFlag();
}

void CapabilityRegistry::reset()
{
    declarations().clear();
    providers().clear();
    ServiceRegistry::clear();
}

} // namespace golding
