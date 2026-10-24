#include "Application/PanelRootRegistry.h"

namespace golding
{

namespace
{

/**槽位本体。`set` 与 `widgetClass != nullptr` 分开存：`clear()` 之后两者都要复位。*/
struct Slot
{
    const GoldingWidgetClass* widgetClass = nullptr;
    std::string provider;
    bool set = false;
};

Slot& storage()
{
    static Slot slot;
    return slot;
}

RegistryStats& statsStorage()
{
    static RegistryStats stats;
    return stats;
}

} // namespace

PanelRootRegistry::Result PanelRootRegistry::setRoot(const GoldingWidgetClass* widgetClass,
                                                     const std::string& provider)
{
    if (widgetClass == nullptr || widgetClass->name == nullptr || widgetClass->name[0] == '\0' ||
        widgetClass->create == nullptr)
    {
        statsStorage().reject("面板根工厂指针 / 名字为空，或没有 create");
        return Result::Invalid;
    }

    if (storage().set)
    {
        statsStorage().reject("面板根工厂已由 " + storage().provider + " 提供，不再接受 " + provider);
        return Result::AlreadySet;
    }

    storage().widgetClass = widgetClass;
    storage().provider = provider;
    storage().set = true;
    statsStorage().accept();
    return Result::Ok;
}

const GoldingWidgetClass* PanelRootRegistry::root()
{
    return storage().widgetClass;
}

const std::string& PanelRootRegistry::provider()
{
    static const std::string empty;
    return storage().set ? storage().provider : empty;
}

const RegistryStats& PanelRootRegistry::stats()
{
    return statsStorage();
}

void PanelRootRegistry::clear()
{
    storage() = Slot{};
    statsStorage().reset();
}

} // namespace golding
