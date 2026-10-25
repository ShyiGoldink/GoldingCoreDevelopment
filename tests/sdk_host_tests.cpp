// SDK C++ 面的最小单测：不需要窗口、不需要 InkingUIKit。
//
// 跑法：`ctest -R sdk_host`（`GOLDING_WITH_INKING=OFF` 下也能编能跑，这正是
// "双头方案"第一层的证据：`GoldingPlugin.h` 零 UI 依赖）。
//
// 它验的不是"功能对不对"，而是**接口契约**：
//   - descriptor 只剩二进制身份（abiVersion / sdkId / sdkVersion / ready），
//     且**读它不构造插件对象**——描述在 `plugin.json`，dll 不需要实例；
//   - entry 怎么转发进插件自己的 Register、注册是不是恰好一次；
//   - Ready 的返回值是不是**原样**传给宿主（宿主拿它汇总退出码）；
//   - SDK 版本常量有没有真的接上 CMake（没接上就是兜底值）。
//
// 桩 `GoldingHostApi` 的 22 个函数指针全部填上：漏填一个就会在运行时崩，
// 而"测试自己崩了"最容易被人当成"测试通过了"。

#include <sdk/GoldingPlugin.h>
#include <sdk/golding_json.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{

int g_failures = 0;

/**`TestPlugin` 被构造过几次。ABI v11 的硬契约：**读 descriptor 不许构造插件对象**。*/
int g_constructed = 0;

void check(const char* label, bool ok, const std::string& detail = "")
{
    std::printf("[%s] %-34s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    if (!ok)
    {
        ++g_failures;
    }
}

/**桩宿主：只记账，不实现任何行为。`hostContext` 就指向它自己。*/
struct StubHost
{    int logCalls = 0;
    int registerPageCalls = 0;
    int registerServiceCalls = 0;
    int registerWidgetClassCalls = 0;
    int registerRegionCalls = 0;
    int registerRegionItemCalls = 0;
    int queryServiceCalls = 0;

    /**queryService 要不要给东西（nullptr = 没人提供）。*/
    const void* serviceToReturn = nullptr;

    char lastLog[512] = {};
};

StubHost& self(void* context)
{
    return *static_cast<StubHost*>(context);
}

void stubLog(void* context, int /*level*/, const char* message)
{
    self(context).logCalls += 1;
    std::snprintf(self(context).lastLog, sizeof(StubHost::lastLog), "%s", message != nullptr ? message : "");
}

int stubRegisterWidgetClass(void* context, const GoldingWidgetClass*)
{
    self(context).registerWidgetClassCalls += 1;
    return GOLDING_REGISTER_OK;
}

int stubRegisterPage(void* context, const GoldingPageDecl*)
{
    self(context).registerPageCalls += 1;
    return GOLDING_REGISTER_OK;
}

int stubPageCount(void*) { return 0; }
const GoldingPageDecl* stubPageAt(void*, int) { return nullptr; }
GoldingWidgetHandle stubMountPage(void*, const char*, void*) { return nullptr; }

int stubRegisterSettings(void* context, const GoldingSettingsDecl*)
{
    (void)context;
    return GOLDING_REGISTER_OK;
}
int stubSettingsCount(void*) { return 0; }
const GoldingSettingsDecl* stubSettingsAt(void*, int) { return nullptr; }
GoldingWidgetHandle stubMountSettings(void*, const char*, void*) { return nullptr; }

int stubPluginCount(void*) { return 0; }
const GoldingPluginInfo* stubPluginAt(void*, int) { return nullptr; }

int stubRegisterService(void* context, const char*, const void*)
{
    self(context).registerServiceCalls += 1;
    return GOLDING_REGISTER_OK;
}

const void* stubQueryService(void* context, const char*)
{
    self(context).queryServiceCalls += 1;
    return self(context).serviceToReturn;
}

int stubRegisterRegion(void* context, const GoldingRegionDecl*)
{
    self(context).registerRegionCalls += 1;
    return GOLDING_REGISTER_OK;
}
int stubRegionCount(void*) { return 0; }
const GoldingRegionDecl* stubRegionAt(void*, int) { return nullptr; }

int stubRegisterRegionItem(void* context, const GoldingRegionItemDecl*)
{
    self(context).registerRegionItemCalls += 1;
    return GOLDING_REGISTER_OK;
}
int stubRegionItemCount(void*, const char*) { return 0; }
const GoldingRegionItemDecl* stubRegionItemAt(void*, const char*, int) { return nullptr; }

void* stubUiRoot(void*) { return nullptr; }
const char* stubBootMode(void*) { return ""; }

void fillApi(GoldingHostApi& api, StubHost& stub, unsigned int abi)
{
    api.abiVersion = abi;
    api.hostContext = &stub;
    api.log = &stubLog;
    api.registerWidgetClass = &stubRegisterWidgetClass;
    api.registerPage = &stubRegisterPage;
    api.pageCount = &stubPageCount;
    api.pageAt = &stubPageAt;
    api.mountPage = &stubMountPage;
    api.registerSettingsSection = &stubRegisterSettings;
    api.settingsSectionCount = &stubSettingsCount;
    api.settingsSectionAt = &stubSettingsAt;
    api.mountSettingsSection = &stubMountSettings;
    api.pluginCount = &stubPluginCount;
    api.pluginAt = &stubPluginAt;
    api.registerService = &stubRegisterService;
    api.queryService = &stubQueryService;
    api.registerRegion = &stubRegisterRegion;
    api.regionCount = &stubRegionCount;
    api.regionAt = &stubRegionAt;
    api.registerRegionItem = &stubRegisterRegionItem;
    api.regionItemCount = &stubRegionItemCount;
    api.regionItemAt = &stubRegionItemAt;
    api.uiRoot = &stubUiRoot;
    api.bootMode = &stubBootMode;
}

/**一个只用来验契约的插件：注册 1 个页面 + 1 个服务，`Ready` 故意返回非 0。*/
class TestPlugin final : public golding::Plugin
{
public:
    TestPlugin() { ++g_constructed; }

    bool Register(golding::Host& host) override
    {
        host.AddPage(kPage);
        host.AddService("test.cap", &kVtable);
        return true;
    }

    /**故意返回 7：宿主拿它汇总退出码，"原样传出"是这一层的硬契约。*/
    int Ready(const golding::Host& host) override
    {
        (void)host;
        return 7;
    }

private:
    static GoldingWidgetHandle createNothing(void*, void*)
    {
        return nullptr;
    }

    static const inline GoldingPageDecl kPage{"test.page", "测试页", 0, &createNothing};
    static const inline unsigned int kVtable[1] = {GOLDING_PLUGIN_ABI_VERSION};
};

/**`Use<T>` 要读的首字段。*/
struct FakeService
{
    unsigned int abiVersion;
    int payload;
};

} // namespace

int main()
{
    // ---- 1. descriptor：ABI v11 只剩二进制身份 + ready ----
    const GoldingPluginDescriptor* descriptor = golding::detail::BuildDescriptor<TestPlugin>();
    check("descriptor 非空", descriptor != nullptr);
    check("读 descriptor 不构造插件对象", g_constructed == 0, "constructed=" + std::to_string(g_constructed));
    if (descriptor != nullptr)
    {
        check("abiVersion = 编译期常量", descriptor->abiVersion == GOLDING_PLUGIN_ABI_VERSION,
              std::to_string(descriptor->abiVersion).c_str());
        check("ready 已挂钩", descriptor->ready != nullptr);
        check("descriptor 幂等", golding::detail::BuildDescriptor<TestPlugin>() == descriptor);

        // sdkId 由编译期常量填，作者不写；sdkVersion 必须真的接上 CMake。
        check("descriptor.sdkId", descriptor->sdkId != nullptr && std::strcmp(descriptor->sdkId, "golding") == 0,
              descriptor->sdkId != nullptr ? descriptor->sdkId : "null");
        check("descriptor.sdkVersion", descriptor->sdkVersion != nullptr &&
                                           std::strcmp(descriptor->sdkVersion, GOLDING_SDK_VERSION) == 0,
              descriptor->sdkVersion != nullptr ? descriptor->sdkVersion : "null");
    }

    // ---- 2. SDK 标识：这两个常量必须由 CMake 传进来 ----
    check("GOLDING_SDK_ID", std::strcmp(GOLDING_SDK_ID, "golding") == 0, GOLDING_SDK_ID);
    check("GOLDING_SDK_VERSION 已接线", std::strcmp(GOLDING_SDK_VERSION, "0.0.0-dev") != 0, GOLDING_SDK_VERSION);

    // ---- 3. entry：注册恰好一次，返回值 0 ----
    StubHost stub;
    GoldingHostApi api{};
    fillApi(api, stub, GOLDING_PLUGIN_ABI_VERSION);

    const int entryCode = golding::detail::EntryThunk<TestPlugin>(&api, "test");
    check("entry 返回 0", entryCode == 0, std::to_string(entryCode).c_str());
    check("entry 时才构造插件对象", g_constructed == 1, "constructed=" + std::to_string(g_constructed));
    check("注册页面恰好 1 次", stub.registerPageCalls == 1, std::to_string(stub.registerPageCalls).c_str());
    check("注册服务恰好 1 次", stub.registerServiceCalls == 1, std::to_string(stub.registerServiceCalls).c_str());
    check("每次注册都留痕", stub.logCalls >= 2, "logCalls=" + std::to_string(stub.logCalls));
    check("留痕里用了宿主给的名字", std::strstr(stub.lastLog, "test") != nullptr, stub.lastLog);

    // ---- 4. ready：返回值原样传出 ----
    const int readyCode = golding::detail::ReadyThunk<TestPlugin>(&api, "test");
    check("ready 返回值原样传出", readyCode == 7, std::to_string(readyCode).c_str());

    // ---- 5. Use<T>：缺能力返回 nullptr 且留痕；有则返回 ----
    golding::Host host(&api, "test");
    check("缺能力返回 nullptr", host.Use<FakeService>("golding.nope") == nullptr);
    check("缺能力留了痕", std::strstr(stub.lastLog, "缺能力") != nullptr, stub.lastLog);

    FakeService service{GOLDING_PLUGIN_ABI_VERSION, 42};
    stub.serviceToReturn = &service;
    const FakeService* found = host.Use<FakeService>("fake.service");
    check("取到服务", found == &service && found->payload == 42);

    // 分两步：detail 用的是 stub.lastLog，和 Use 的调用写在同一行会吃到"求值顺序未定义"的亏。
    const FakeService* tooNew = host.Use<FakeService>("fake.service", GOLDING_PLUGIN_ABI_VERSION + 1u);
    check("版本不足仍放行", tooNew == &service);
    check("版本不足留的痕可区分", std::strstr(stub.lastLog, "版本不足") != nullptr, stub.lastLog);

    // ---- 6. JSON 解析器（sdk/golding_json.h）----
    //
    // 这一组也同时在回答"开发包能不能单独用"：本目标只链 `golding_sdk`，
    // 解析器是 header-only 的，不链 core 也能解析——这正是旧 `Application/Json.h`
    // 做不到的事（头可见、符号在 golding_core 里 ⇒ 链接期才炸）。
    {
        namespace json = golding::json;

        // object：取键、各标量类型、未命中
        json::Value root;
        const bool okObject = json::parse(R"({"name":"a","n":2.5,"t":true,"f":false,"z":null})", root);
        check("json.parse object", okObject && root.type == json::Value::Type::object);
        const json::Value* name = root.find("name");
        const json::Value* num = root.find("n");
        const json::Value* yes = root.find("t");
        const json::Value* no = root.find("f");
        const json::Value* nil = root.find("z");
        check("json.object 取字符串", name != nullptr && name->text == "a");
        check("json.object 取数字", num != nullptr && num->type == json::Value::Type::number && num->number == 2.5);
        check("json.object 取 true", yes != nullptr && yes->type == json::Value::Type::boolean && yes->boolean);
        check("json.object 取 false", no != nullptr && no->type == json::Value::Type::boolean && !no->boolean);
        check("json.object 取 null", nil != nullptr && nil->type == json::Value::Type::null);
        check("json.find 未命中返回 nullptr", root.find("nope") == nullptr);
        check("json.find 非 object 返回 nullptr", name != nullptr && name->find("any") == nullptr);

        // array
        json::Value list;
        const bool okArray = json::parse("[1, \"two\", [3]]", list);
        check("json.parse array", okArray && list.type == json::Value::Type::array && list.array.size() == 3);
        check("json.array 元素", okArray && list.array[0].number == 1.0 && list.array[1].text == "two" &&
                                    list.array[2].type == json::Value::Type::array);

        // string 转义
        json::Value escaped;
        const bool okEscaped = json::parse(R"("a\"b\\c\nd\te")", escaped);
        check("json.parse string 转义",
              okEscaped && escaped.text == "a\"b\\c\nd\te", escaped.text);

        // \uXXXX 与代理对（BMP 一个用例、平面外一个用例）
        json::Value bmp;
        const bool okBmp = json::parse(R"({"k":"中文"})", bmp);
        const json::Value* bmpText = bmp.find("k");
        check("json.parse \\uXXXX", okBmp && bmpText != nullptr && bmpText->text == "\xe4\xb8\xad\xe6\x96\x87",
              bmpText != nullptr ? bmpText->text : "");

        json::Value emoji;
        const bool okEmoji = json::parse(R"("\uD83D\uDE00")", emoji);
        check("json.parse 代理对", okEmoji && emoji.text == "\xF0\x9F\x98\x80", emoji.text);

        json::Value loneSurrogate;
        check("json.parse 孤立高代理 → false", !json::parse(R"("\uD83D")", loneSurrogate));

        // 深度超限：kMaxDepth=64，压 200 层必须被挡住（挡不住就是栈打穿）
        std::string deep;
        for (int i = 0; i < 200; ++i)
        {
            deep.push_back('[');
        }
        for (int i = 0; i < 200; ++i)
        {
            deep.push_back(']');
        }
        json::Value deepOut;
        check("json.parse 深度超限 → false", !json::parse(deep, deepOut));

        // 严格 JSON：注释 / 尾逗号 / 单引号 / NaN 一律拒
        json::Value junk;
        check("json.parse 尾逗号 → false", !json::parse(R"({"a":1,})", junk));
        check("json.parse 裸键 → false", !json::parse(R"({a:1})", junk));
        check("json.parse 空文本 → false", !json::parse("", junk));
        check("json.parse 后跟垃圾 → false", !json::parse("{} {}", junk));

        // dump 往返：`"` `\` 换行 中文 各来一条，`parse(dump(v))` 必须结构相等
        auto makeString = [](const std::string& text) {
            json::Value out;
            out.type = json::Value::Type::string;
            out.text = text;
            return out;
        };
        auto makeNumber = [](double number) {
            json::Value out;
            out.type = json::Value::Type::number;
            out.number = number;
            return out;
        };

        json::Value sample;
        sample.type = json::Value::Type::object;
        sample.object.emplace_back("quote", makeString("a\"b\\c"));
        sample.object.emplace_back("newline", makeString("d\ne\tf"));
        sample.object.emplace_back("cjk", makeString("中文"));
        json::Value nested;
        nested.type = json::Value::Type::array;
        nested.array.push_back(makeNumber(31.0));
        nested.array.push_back(makeNumber(2.5));
        sample.object.emplace_back("list", nested);

        const std::string written = json::dump(sample);
        json::Value again;
        const bool okRound = json::parse(written, again);
        check("json.dump 往返", okRound && again.type == json::Value::Type::object);
        if (okRound)
        {
            const json::Value* quote = again.find("quote");
            const json::Value* newline = again.find("newline");
            const json::Value* cjk = again.find("cjk");
            const json::Value* list = again.find("list");
            check("json.dump 往返：引号与反斜杠", quote != nullptr && quote->text == "a\"b\\c",
                  quote != nullptr ? quote->text : "");
            check("json.dump 往返：换行与制表", newline != nullptr && newline->text == "d\ne\tf");
            check("json.dump 往返：中文原样", cjk != nullptr && cjk->text == "\xe4\xb8\xad\xe6\x96\x87",
                  cjk != nullptr ? cjk->text : "");
            check("json.dump 往返：嵌套数组",
                  list != nullptr && list->array.size() == 2 && list->array[0].number == 31.0 &&
                      list->array[1].number == 2.5);
        }
        check("json.dump 非 ASCII 不转 \\u", written.find("\\u") == std::string::npos, written);

        check("json.dump 整数不带小数点", json::dump(makeNumber(31.0)) == "31", json::dump(makeNumber(31.0)));
        check("json.dump 负数整数", json::dump(makeNumber(-7.0)) == "-7", json::dump(makeNumber(-7.0)));
        check("json.dump 小数保留", json::dump(makeNumber(2.5)) == "2.5", json::dump(makeNumber(2.5)));

        json::Value third;
        const bool okThird = json::parse(json::dump(makeNumber(1.0 / 3.0)), third);
        check("json.dump 往返精度 %.17g", okThird && third.number == 1.0 / 3.0);
    }

    // ---- 7. 空 api 不崩（宿主缺席时的降级路径）----
    static const GoldingPageDecl kUnusedPage{"unused", "没宿主", 0, nullptr};
    golding::Host noApi(nullptr, nullptr);
    check("空 api 注册返回 false", !noApi.AddPage(kUnusedPage));
    check("空 api 查服务返回 nullptr", noApi.Use<FakeService>("any") == nullptr);
    check("空 api 的 provider 兜底", std::strcmp(noApi.provider(), "?") == 0);

    std::printf("\n== 失败项：%d ==\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
