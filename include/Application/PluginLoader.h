#pragma once

#include <filesystem>

#include "sdk/golding_plugin.h"

namespace golding
{

/**加载阶段的结果。*/
enum class LoadStatus
{
    Pass,          // 库打开成功，abiVersion 与 sdkId 都与宿主一致
    LibraryError,  // 库打不开（不存在 / 位数不对 / 缺依赖）
    SymbolMissing, // 缺 golding_plugin_descriptor 或 golding_plugin_entry
    AbiMismatch,   // abiVersion 不一致——到此为止，绝不调 entry
    SdkMismatch,   // sdkId 不一致（不是给这套 SDK 编的）或为空——同样绝不调 entry
};

/**一次加载的产物：库句柄 + descriptor。*/
struct LoadedPlugin
{
    void* library = nullptr;
    const GoldingPluginDescriptor* descriptor = nullptr;
    LoadStatus status = LoadStatus::LibraryError;
};

/**
 * @brief dll 的三步：打开 → 比对 ABI → 调 entry。
 *
 * **先比 ABI 再进 C++**：ABI 不对时调用 entry 的表现是崩溃，不是报错。
 */
class PluginLoader
{
public:
    /**打开库并读取 descriptor，比对 abiVersion。失败时已自动释放句柄。*/
    static LoadedPlugin open(const std::filesystem::path& dllPath, unsigned int expectedAbi);

    /**调用 golding_plugin_entry；返回 true 表示插件成功完成注册。*/
    static bool callEntry(void* library, const GoldingHostApi* hostApi);

    /**释放库句柄（只支持整体重加载，不单独卸载）。*/
    static void close(void* library);
};

} // namespace golding
