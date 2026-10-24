#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "Application/PluginLibrary.h"

namespace golding
{

namespace
{

struct Entry
{
    GoldingPluginInfo info{};
    std::vector<std::string> depends;
    /**ABI v9 的静态宣言副本。*/
    const char* sdk = "";
    std::vector<ManifestCapability> provides;
    std::vector<ManifestCapability> needs;
    /**插件被扫描到的目录（UTF-8 绝对路径）；空 = 没登记过。*/
    std::string directory;
};

std::deque<std::string>& strings()
{
    static std::deque<std::string> storage;
    return storage;
}

std::vector<Entry>& entries()
{
    static std::vector<Entry> storage;
    return storage;
}

const char* keep(const char* text)
{
    return strings().emplace_back(text != nullptr ? text : "").c_str();
}

} // namespace

void PluginLibrary::add(const char* name, const char* title, const char* version, const char* role, int abiVersion,
                        PluginState state, const std::vector<std::string>& depends, const ManifestFacts& facts)
{
    Entry entry;
    entry.info.name = keep(name);
    entry.info.title = keep(title);
    entry.info.version = keep(version);
    entry.info.role = keep(role);
    entry.info.abiVersion = abiVersion;
    entry.info.state = toInt(state);

    entry.depends = depends;

    // 两份拼成一行：档案输出要的是"这套插件是给哪个 SDK 的哪个版本编的"，
    // 分开存反而要多两列。空 sdkId 说明插件没填（v9 之后不该发生）。
    const std::string sdk = std::string(facts.sdkId != nullptr ? facts.sdkId : "") + "/" +
                            std::string(facts.sdkVersion != nullptr ? facts.sdkVersion : "");
    entry.sdk = keep(sdk.c_str());
    // 能力声明直接拷 `plugin.json` 那份（名字 + 版本），不指向扫描结果——
    // 扫描结果会随 `prepare` 的局部量消失。
    if (facts.provides != nullptr)
    {
        entry.provides = *facts.provides;
    }
    if (facts.needs != nullptr)
    {
        entry.needs = *facts.needs;
    }

    entries().push_back(std::move(entry));
}

void PluginLibrary::setState(int index, PluginState state)
{
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return;
    }
    entries()[static_cast<std::size_t>(index)].info.state = toInt(state);
}

void PluginLibrary::reset()
{
    entries().clear();
    strings().clear();
}

int PluginLibrary::count()
{
    return static_cast<int>(entries().size());
}

const GoldingPluginInfo* PluginLibrary::at(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return nullptr;
    }
    return &entries()[static_cast<std::size_t>(index)].info;
}

const std::vector<std::string>& PluginLibrary::dependsOf(int index)
{
    static const std::vector<std::string> empty;
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return empty;
    }
    return entries()[static_cast<std::size_t>(index)].depends;
}

const char* PluginLibrary::sdkOf(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return "";
    }
    return entries()[static_cast<std::size_t>(index)].sdk;
}

const std::vector<ManifestCapability>& PluginLibrary::providesOf(int index)
{
    static const std::vector<ManifestCapability> empty;
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return empty;
    }
    return entries()[static_cast<std::size_t>(index)].provides;
}

const std::vector<ManifestCapability>& PluginLibrary::needsOf(int index)
{
    static const std::vector<ManifestCapability> empty;
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return empty;
    }
    return entries()[static_cast<std::size_t>(index)].needs;
}

void PluginLibrary::setDirectory(int index, const char* utf8)
{
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size() || utf8 == nullptr)
    {
        return;
    }
    entries()[static_cast<std::size_t>(index)].directory = utf8;
}

int PluginLibrary::indexOf(const char* name)
{
    if (name == nullptr)
    {
        return -1;
    }
    for (std::size_t index = 0; index < entries().size(); ++index)
    {
        const char* stored = entries()[index].info.name;
        if (stored != nullptr && std::strcmp(stored, name) == 0)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

const char* PluginLibrary::directoryOf(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= entries().size())
    {
        return nullptr;
    }
    const std::string& directory = entries()[static_cast<std::size_t>(index)].directory;
    return directory.empty() ? nullptr : directory.c_str();
}

} // namespace golding
