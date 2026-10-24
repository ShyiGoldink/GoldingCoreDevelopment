#pragma once

// SDK 的第二层：给"要画界面"的插件一套像功能类一样用的构件包装。
//
// 为什么是**组合**而不是继承 `InkingWidget`：
// InkingUIKit 的组件是"构造时吃一个 Data 结构体"，继承会让每个派生类的构造签名被
// 基类绑死；而换 InkingUIKit 版本时，开销从"改这一个包装类"变成"改所有派生类"。
//
// 分工：
//   - **契约在 SDK，规格在插件**：这里只有"能对一个节点做什么"（8 个操作 + 下转型），
//     具体构件的配置字段属于提供它的插件（见 T-09 的 widgets 插件）。
//   - 纯逻辑插件**不要** include 这个头——它一进来就依赖 InkingUIKit 了。
//     想要"一个按钮但不想 include 任何 InkingUIKit 头"的插件走**构件服务**
//     `golding.widgets`（用下面那个 `WidgetsFrom(host)` 取）：由核心插件库里的
//     widgets 插件按"名字 + 配置串"建构件，规格在它自己的头里，SDK 只钉住四个
//     函数的形状。
//
// 用法：
//
//   golding::Widget<inking::InkingStaticPanel> root = golding::AddWidget<inking::InkingStaticPanel>(parent, data);
//   root->AddChild(...);          // 需要具体类型时走 ->
//   root.SetVisible(false);       // 通用操作走包装
//   golding::Widget<inking::InkingStaticButton> button = ...;
//   button.OnClicked([] { ... });

#include <sdk/GoldingPlugin.h>

#include <widget/InkingWidget.h>

#include <button/InkingButtonLike.h>
#include <widget/InkingDynamicWidget.h>

#include <functional>
#include <memory>
#include <utility>

namespace golding
{

/**构件服务的名字。由 `plugins/core/widgets` 提供（服务结构见 `golding_plugin.h`）。*/
inline constexpr const char* kWidgetsServiceName = GOLDING_WIDGETS_SERVICE_NAME;

/**
 * 从宿主取构件服务；没人提供时返回 nullptr——**调用方要能降级成"不画"**。
 *
 * 走 `Host::Use`，所以每次取都留一行痕，"缺能力"与"取用"在日志里可区分。
 */
template <typename THost>
inline const GoldingWidgetsService* WidgetsFrom(const THost& host)
{
    return host.template Use<GoldingWidgetsService>(kWidgetsServiceName);
}

/**
 * @brief 一个节点的**非拥有**包装。
 *
 * 只包最常用的 8 个操作：`AddChild` / `SetVisible` / `Resize` / `OnClicked` /
 * `GetWidth` / `GetHeight` / `GetX` / `GetY`。刻意不做成"全量镜像"——
 * 包装越厚，换 InkingUIKit 版本时越难对齐；没包到的操作用 `get()` 拿裸指针，
 * 那是**有意留的出口**，不是遗漏。
 *
 * 空包装（默认构造 / 传 nullptr）上所有操作都是无操作，读尺寸返回 0。
 */
class WidgetBase
{
public:
    WidgetBase() = default;
    WidgetBase(inking::InkingWidget* widget) : _widget(widget) {} // NOLINT(google-explicit-constructor)：包装成对地当参数传

    /**裸指针出口（未包装的边角操作用它）。*/
    inking::InkingWidget* get() const noexcept { return _widget; }
    bool Valid() const noexcept { return _widget != nullptr; }
    explicit operator bool() const noexcept { return _widget != nullptr; }
    /**节点名（诊断 / 断言里定位用）。*/
    const char* GetName() const noexcept { return _widget != nullptr ? _widget->GetName().c_str() : ""; }

    /**
     * @brief 挂一个子节点（所有权交给本节点），返回**新建的那个**包装。
     *
     * 约定同 InkingUIKit：`AddChild` 返回的是刚挂上的裸指针，失败为 nullptr。
     */
    WidgetBase AddChild(std::unique_ptr<inking::InkingWidget> child) const
    {
        if (_widget == nullptr || child == nullptr)
        {
            return WidgetBase();
        }
        return WidgetBase(_widget->AddChild(std::move(child)));
    }

    bool SetVisible(bool visible) const
    {
        return _widget != nullptr ? _widget->SetVisible(visible) : false;
    }

    /**
     * @brief 改尺寸。**只有动态档组件能改**（静态档"几何即定型"是编译期保证）。
     *
     * 返回 false = 这个节点没有几何写入口，尺寸没变——别把它当成"改成功了"。
     */
    bool Resize(int width, int height) const
    {
        if (auto* dynamic = dynamic_cast<inking::InkingDynamicWidget*>(_widget))
        {
            return dynamic->Resize(width, height);
        }
        return false;
    }

    /**
     * @brief 挂点击回调（"替换"语义：再挂一次就是换掉）。
     *
     * 只对**按钮类**组件有效（`InkingButtonLike` 的派生），其余返回 false——
     * 普通面板收不收到点击是它自己的事，包装层不做转发。
     */
    bool OnClicked(std::function<void()> callback) const
    {
        if (auto* button = dynamic_cast<inking::InkingButtonLike*>(_widget))
        {
            button->SetOnClicked(std::move(callback));
            return true;
        }
        return false;
    }

    int GetWidth() const noexcept { return _widget != nullptr ? _widget->GetWidth() : 0; }
    int GetHeight() const noexcept { return _widget != nullptr ? _widget->GetHeight() : 0; }
    float GetX() const noexcept { return _widget != nullptr ? _widget->GetX() : 0.0f; }
    float GetY() const noexcept { return _widget != nullptr ? _widget->GetY() : 0.0f; }

protected:
    inking::InkingWidget* _widget = nullptr;
};

/**
 * @brief 带具体类型的包装：`Widget<inking::InkingStaticButton>`。
 *
 * 下行转换用 `static_cast`（不做 RTTI 检查）——和 InkingUIKit 自己的写法一致：
 * 类型是在**建节点的那一行**就知道的，转换点必须紧贴创建点。
 */
template <typename T>
class Widget : public WidgetBase
{
public:
    Widget() = default;
    explicit Widget(inking::InkingWidget* widget) : WidgetBase(widget) {}

    /**把一个通用包装收紧成具体类型（`AddChild` 的返回值就是通用包装）。*/
    static Widget<T> Wrap(const WidgetBase& base) { return Widget<T>(base.get()); }

    T* get() const noexcept { return static_cast<T*>(WidgetBase::get()); }
    T* operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return WidgetBase::Valid(); }
};

/**
 * @brief 建一个子节点并包好：`AddWidget<inking::InkingStaticPanel>(parent, panelData)`。
 *
 * 参数除父节点外原样转给组件的构造函数（这些组件都是 `(parent, data)` 形态）。
 */
template <typename T, typename... TArgs>
Widget<T> AddWidget(const WidgetBase& parent, TArgs&&... args)
{
    if (!parent.Valid())
    {
        return Widget<T>();
    }
    return Widget<T>(parent.AddChild(std::make_unique<T>(parent.get(), std::forward<TArgs>(args)...)).get());
}

} // namespace golding
