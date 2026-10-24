#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Application/PluginLoader.h"
#include "Tool/PathFinder.h"

namespace golding
{

namespace
{

#ifdef _WIN32

/**GetProcAddress 返回的 FARPROC 与目标函数指针类型不同，直接 reinterpret_cast 会告警；
 *  两者大小相同，走 memcpy 转存是这里最干净的写法。*/
template <typename Fn>
Fn resolve(void* library, const char* symbol)
{
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(library), symbol);
    Fn fn = nullptr;
    static_assert(sizeof(fn) == sizeof(proc));
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

#endif

} // namespace

LoadedPlugin PluginLoader::open(const std::filesystem::path& dllPath, unsigned int expectedAbi)
{
    LoadedPlugin loaded;

#ifdef _WIN32
    void* library = static_cast<void*>(LoadLibraryW(dllPath.wstring().c_str()));
    if (library == nullptr)
    {
        loaded.status = LoadStatus::LibraryError;
        return loaded;
    }

    const auto descriptorFn =
        resolve<GoldingPluginDescriptorFn>(library, GOLDING_PLUGIN_DESCRIPTOR_SYMBOL);
    const auto entryFn = resolve<GoldingPluginEntryFn>(library, GOLDING_PLUGIN_ENTRY_SYMBOL);
    if (descriptorFn == nullptr || entryFn == nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(library));
        loaded.status = LoadStatus::SymbolMissing;
        return loaded;
    }

    const GoldingPluginDescriptor* descriptor = descriptorFn();
    if (descriptor == nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(library));
        loaded.status = LoadStatus::SymbolMissing;
        return loaded;
    }

    // ABI v11：descriptor 里已经没有名字了，日志用 dll 路径点名——那时插件还没 entry，
    // 正好也不需要它的名字。
    const std::string label = PathFinder::toUtf8(dllPath);

    // ABI 闸门：不匹配就地返回，绝不进 C++。
    if (descriptor->abiVersion != expectedAbi)
    {
        FreeLibrary(static_cast<HMODULE>(library));
        loaded.status = LoadStatus::AbiMismatch;
        return loaded;
    }

    // SDK 闸门（v9）：**必须排在 ABI 之后**——ABI 不对时 sdkId 那个指针本身就不可信，
    // 拿它去 strcmp 等于信任一份错位的结构体。
    //
    // 空指针当"不匹配"处理，不当"跳过检查"：插件可以填 nullptr，
    // 而把检查写在 strcmp 后面会让它从"检查"变成"崩溃"。
    if (descriptor->sdkId == nullptr || std::strcmp(descriptor->sdkId, GOLDING_SDK_ID) != 0)
    {
        std::printf("[host] sdkId 不匹配，拒绝加载：%s（期望 %s，实际 %s）\n", label.c_str(), GOLDING_SDK_ID,
                    descriptor->sdkId != nullptr ? descriptor->sdkId : "(空)");
        FreeLibrary(static_cast<HMODULE>(library));
        loaded.status = LoadStatus::SdkMismatch;
        return loaded;
    }

    // sdkVersion 只记不拒（实验期编译栈未定）。
    if (descriptor->sdkVersion == nullptr || std::strcmp(descriptor->sdkVersion, GOLDING_SDK_VERSION) != 0)
    {
        std::printf("[host] %s 的 SDK 版本差异：期望 %s，实际 %s（放行）\n", label.c_str(), GOLDING_SDK_VERSION,
                    descriptor->sdkVersion != nullptr ? descriptor->sdkVersion : "(空)");
    }

    loaded.library = library;
    loaded.descriptor = descriptor;
    loaded.status = LoadStatus::Pass;
#else
    (void)dllPath;
    (void)expectedAbi;
    loaded.status = LoadStatus::LibraryError;
#endif

    return loaded;
}

bool PluginLoader::callEntry(void* library, const GoldingHostApi* hostApi, const char* pluginName)
{
#ifdef _WIN32
    if (library == nullptr)
    {
        return false;
    }
    const auto entryFn = resolve<GoldingPluginEntryFn>(library, GOLDING_PLUGIN_ENTRY_SYMBOL);
    if (entryFn == nullptr)
    {
        return false;
    }
    return entryFn(hostApi, pluginName) == 0;
#else
    (void)library;
    (void)hostApi;
    (void)pluginName;
    return false;
#endif
}

} // namespace golding
