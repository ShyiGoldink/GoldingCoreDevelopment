#pragma once

#include "Application/CoreInternal.h"

#include <cstddef>
#include <string>
#include <vector>

#include "Application/RegistryStats.h"

namespace golding
{

/**
 * @brief 服务表：名字 → 插件提供的 vtable。
 *
 * 这是插件之间**唯一**的横向通道。两个插件互相看不见对方的类型，
 * 只按名字取一份函数指针表，首字段是 abiVersion 由双方各自校验。
 */
class ServiceRegistry
{
public:
    enum class RegisterResult
    {
        Ok,
        NameTaken,
        Invalid,
    };

    static RegisterResult registerService(const char* name, const void* vtable);
    static const void* queryService(const char* name);
    static std::vector<std::string> allNames();

    /**计数与最后一次驳回原因（见 `RegistryStats`）。*/
    static const RegistryStats& stats();

    static void clear();
};

} // namespace golding
