// ---------------------------------------------------------------------------
// spellbook —— **插件可视化**：把所有已加载的插件画成一张复合魔法阵（只读）。
//
// 三件事：
//   1. 往宿主设置表里注册一个「插件图谱」项（动作型，点一下直接开图）；
//   2. 弹窗：**左侧 3/4** 是绕同一个圆心的同心法阵，**右侧 1/4** 是详情；
//   3. 点某一环 → 右侧显示那个插件的档案（名字 / 版本 / 状态 / 依赖 / 能力）。
//
// ---------------------------------------------------------------------------
// 形状从哪来：`plugin/<名字>/shape.json`（没有就用名字哈希派生）
//
// 一个插件的形状 = **若干层**，每层是一条极坐标闭合曲线：
//
//     r(θ) = 半径 · (1 + 振幅 · sin(波瓣·θ + 自转相位))
//
// 层与层之间只差几个数：半径偏移、线宽、波瓣、振幅、转速、颜色。所以"双层 /
// 特别华丽"就是多写一两层；多个插件的半径撞在一起就是**叠成一团**，而各层都带
// alpha，叠起来看就是颜色混出来的新色（`CanvasContext` 没有加法混合模式，这里用
// 半透明叠加近似"像素和"）。
//
// 渲染 / 命中 / 包围盒同源：曲线交给 `ShapeSpec::PolygonPath`（同一份点列），
// 描边交给 `StrokeShape`（一半在外一半在内）。命中用每层一个 `Ring` 形状的透明
// 按钮——环带是旋转对称的，转不转都不影响"点在哪一层上"。
//
// 姿态（`Pose`）现在是两项：**绕点转**（每层自己的 `orbit`，所有层默认同一个值
// 就是整张阵一起转）与**自转**（形状相位自己漂移）。"跟随父级"留给以后依赖形状。
// ---------------------------------------------------------------------------

#include "GoldingCommands.h"
#include "GoldingServices.h"

#include <sdk/GoldingPlugin.h>

#include <GoldingMenu.h>
#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <canvas/CanvasContext.h>
#include <canvas/CanvasData.h>
#include <canvas/InkingStaticCanvas.h>
#include <dataStruct/InkingLayout.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextLayout.h>
#include <image/ImageData.h>
#include <image/InkingStaticImage.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>
#include <timeline/Timeline.h>
#include <widget/InkingDynamicWidget.h>
#include <widget/InkingWidget.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
// `windows.h` 把 `DrawText` 定义成宏（`DrawTextA` / `DrawTextW`），会打断
// `CanvasContext::DrawText`。这里只借 `ShellExecuteW` 开个文件夹，用完立刻撤掉。
#undef DrawText
#endif

namespace
{

namespace theme = inking::prefab::theme;

/**自己的 `Host` 副本：弹窗的 `create` 是 C 函数指针，够不到 `Register` 的参数。*/
golding::Host g_host{nullptr, nullptr};

/**弹窗服务；面板插件缺席时为 nullptr，那时入口点了不会有反应（只打一条 WARN）。*/
const golding::PopupService* g_popup = nullptr;

/**菜单服务：顶栏「插件」那一项要弹两个动作，靠它。*/
const golding::MenuService* g_menu = nullptr;

/**顶栏区域项 id：菜单以它为锚点（面板才知道这个按钮在哪儿）。*/
constexpr const char* kEntryId = "spellbook.entry";

constexpr int kPopupWidth = 1080;
constexpr int kPopupHeight = 720;

/**左侧"法阵区"占弹窗宽度的比例（3/4）。右侧剩下的 1/4 是详情。*/
constexpr int kGrimoireShare = 3;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

/**整张阵的默认绕点转速（度/秒）。shape.json 的顶层 `orbit` 可以改它。*/
constexpr float kDefaultOrbitDeg = 6.0f;

/**每层的采样点数。96 对"看不出棱角"够用；三角化是 O(n²)，别往上堆。*/
constexpr int kLayerSamples = 96;

// ---------------------------------------------------------------------------
// 哈希与颜色
// ---------------------------------------------------------------------------

/**FNV-1a：确定性、无依赖、跨平台一致。同一个插件名永远同一个结果。*/
std::uint32_t HashText(const char* text)
{
    std::uint32_t hash = 2166136261u;
    if (text == nullptr)
    {
        return hash;
    }
    while (*text != '\0')
    {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(*text));
        hash *= 16777619u;
        ++text;
    }
    return hash;
}

/**从哈希里取一个稳定色相，返回 `0xAARRGGBB`。*/
std::uint32_t HashColor(std::uint32_t hash, float alpha)
{
    const float hue = static_cast<float>(hash % 360u);
    const float saturation = 0.62f;
    const float value = 0.95f;
    const float chroma = value * saturation;
    const float sector = hue / 60.0f;
    const float second = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));

    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    if (sector < 1.0f)
    {
        red = chroma;
        green = second;
    }
    else if (sector < 2.0f)
    {
        red = second;
        green = chroma;
    }
    else if (sector < 3.0f)
    {
        green = chroma;
        blue = second;
    }
    else if (sector < 4.0f)
    {
        green = second;
        blue = chroma;
    }
    else if (sector < 5.0f)
    {
        red = second;
        blue = chroma;
    }
    else
    {
        red = chroma;
        blue = second;
    }

    const float match = value - chroma;
    const auto channel = [match](float raw) {
        const float scaled = (raw + match) * 255.0f + 0.5f;
        return static_cast<std::uint32_t>(scaled < 0.0f ? 0.0f : (scaled > 255.0f ? 255.0f : scaled));
    };

    const std::uint32_t alphaDigit =
        static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (alphaDigit << 24) | (channel(red) << 16) | (channel(green) << 8) | channel(blue);
}

/**按需要改写 alpha。颜色本身（色相/明度）由哈希或 shape.json 定死。*/
std::uint32_t WithAlpha(std::uint32_t color, float alpha)
{
    const std::uint32_t digit = static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (digit << 24) | (color & 0x00FFFFFFu);
}

int HexDigit(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

/**
 * @brief 解析 `"rrggbb"` / `"rrggbbaa"` / `"#rrggbb"`；失败返回 false。
 *
 * 6 位时 alpha 取 `alpha` 参数，8 位时用自带的那一位（alpha 参数被忽略）。
 */
bool ParseHexColor(const std::string& text, float alpha, std::uint32_t& out)
{
    std::size_t start = (!text.empty() && text[0] == '#') ? 1 : 0;
    const std::size_t length = text.size() - start;
    if (length != 6 && length != 8)
    {
        return false;
    }
    std::uint32_t color = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const int digit = HexDigit(text[start + i]);
        if (digit < 0)
        {
            return false;
        }
        color = (color << 4) | static_cast<std::uint32_t>(digit);
    }
    if (length == 8)
    {
        out = color;
        return true;
    }
    const std::uint32_t alphaDigit =
        static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    out = (alphaDigit << 24) | color;
    return true;
}

// ---------------------------------------------------------------------------
// 极简 JSON（只为 shape.json；独立于 core 的解析器，插件不链 core）
// ---------------------------------------------------------------------------

namespace json
{

struct Value
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* find(const char* key) const
    {
        if (type != Type::Object)
        {
            return nullptr;
        }
        for (const auto& entry : object)
        {
            if (entry.first == key)
            {
                return &entry.second;
            }
        }
        return nullptr;
    }
};

class Parser
{
public:
    explicit Parser(const std::string& text) : _text(text) {}

    bool ParseAll(Value& out)
    {
        SkipSpace();
        if (!ParseValue(out, 0))
        {
            return false;
        }
        SkipSpace();
        return _pos >= _text.size();
    }

private:
    static constexpr int kMaxDepth = 32;

    const std::string& _text;
    std::size_t _pos = 0;

    void SkipSpace()
    {
        while (_pos < _text.size())
        {
            const char ch = _text[_pos];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
            {
                ++_pos;
                continue;
            }
            break;
        }
    }

    bool Match(char expected)
    {
        if (_pos < _text.size() && _text[_pos] == expected)
        {
            ++_pos;
            return true;
        }
        return false;
    }

    bool Literal(const char* word)
    {
        std::size_t index = 0;
        while (word[index] != '\0')
        {
            if (_pos + index >= _text.size() || _text[_pos + index] != word[index])
            {
                return false;
            }
            ++index;
        }
        _pos += index;
        return true;
    }

    static void AppendUtf8(std::string& out, std::uint32_t code)
    {
        if (code < 0x80u)
        {
            out.push_back(static_cast<char>(code));
        }
        else if (code < 0x800u)
        {
            out.push_back(static_cast<char>(0xC0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xE0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }

    bool ParseString(std::string& out)
    {
        if (!Match('"'))
        {
            return false;
        }
        out.clear();
        while (_pos < _text.size())
        {
            const char ch = _text[_pos++];
            if (ch == '"')
            {
                return true;
            }
            if (ch != '\\')
            {
                out.push_back(ch);
                continue;
            }
            if (_pos >= _text.size())
            {
                return false;
            }
            const char escape = _text[_pos++];
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                out.push_back(escape);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                if (_pos + 4 > _text.size())
                {
                    return false;
                }
                std::uint32_t code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const int digit = HexDigit(_text[_pos + static_cast<std::size_t>(i)]);
                    if (digit < 0)
                    {
                        return false;
                    }
                    code = (code << 4) | static_cast<std::uint32_t>(digit);
                }
                _pos += 4;
                AppendUtf8(out, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseValue(Value& out, int depth)
    {
        if (depth > kMaxDepth)
        {
            return false;
        }
        SkipSpace();
        if (_pos >= _text.size())
        {
            return false;
        }
        const char ch = _text[_pos];
        if (ch == '{')
        {
            ++_pos;
            out.type = Value::Type::Object;
            SkipSpace();
            if (Match('}'))
            {
                return true;
            }
            while (true)
            {
                SkipSpace();
                std::string key;
                if (!ParseString(key))
                {
                    return false;
                }
                SkipSpace();
                if (!Match(':'))
                {
                    return false;
                }
                Value child;
                if (!ParseValue(child, depth + 1))
                {
                    return false;
                }
                out.object.emplace_back(std::move(key), std::move(child));
                SkipSpace();
                if (Match(','))
                {
                    continue;
                }
                return Match('}');
            }
        }
        if (ch == '[')
        {
            ++_pos;
            out.type = Value::Type::Array;
            SkipSpace();
            if (Match(']'))
            {
                return true;
            }
            while (true)
            {
                Value child;
                if (!ParseValue(child, depth + 1))
                {
                    return false;
                }
                out.array.push_back(std::move(child));
                SkipSpace();
                if (Match(','))
                {
                    continue;
                }
                return Match(']');
            }
        }
        if (ch == '"')
        {
            out.type = Value::Type::String;
            return ParseString(out.text);
        }
        if (Literal("true"))
        {
            out.type = Value::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (Literal("false"))
        {
            out.type = Value::Type::Bool;
            out.boolean = false;
            return true;
        }
        if (Literal("null"))
        {
            out.type = Value::Type::Null;
            return true;
        }
        char* end = nullptr;
        const double parsed = std::strtod(_text.c_str() + _pos, &end);
        if (end == _text.c_str() + _pos)
        {
            return false;
        }
        _pos = static_cast<std::size_t>(end - _text.c_str());
        out.type = Value::Type::Number;
        out.number = parsed;
        return true;
    }
};

bool Parse(const std::string& text, Value& out)
{
    out = Value{};
    Parser parser(text);
    return parser.ParseAll(out);
}

/**取数字；缺省 / 类型不对 → fallback。*/
double Number(const Value& object, const char* key, double fallback)
{
    const Value* node = object.find(key);
    if (node == nullptr || node->type != Value::Type::Number)
    {
        return fallback;
    }
    return node->number;
}

float Float(const Value& object, const char* key, float fallback)
{
    return static_cast<float>(Number(object, key, static_cast<double>(fallback)));
}

std::string Text(const Value& object, const char* key, const std::string& fallback)
{
    const Value* node = object.find(key);
    if (node == nullptr || node->type != Value::Type::String)
    {
        return fallback;
    }
    return node->text;
}

/**取一整个子对象；没有就返回 nullptr（子字段读的时候按缺省处理）。*/
const Value* Object(const Value& object, const char* key)
{
    const Value* node = object.find(key);
    return (node != nullptr && node->type == Value::Type::Object) ? node : nullptr;
}

}  // namespace json

// ---------------------------------------------------------------------------
// 形状：一层 = 一条极坐标闭合曲线 + 它的描边 / 沿线复制元素
// ---------------------------------------------------------------------------

/**一层的图元类型。它们都是"一条中线 + 一圈描边"的不同中线。*/
enum class LayerKind
{
    Curve,    ///< 极坐标波浪曲线（默认；"花瓣"就是它）
    Ring,     ///< 正圆
    Star,     ///< N 角星（内外半径交替）
    Polygon,  ///< 正 N 边形
    Spokes,   ///< 一圈径向辐条（不做成闭合路径，直接画线）
    Path,     ///< 自定义点列（归一化 -1..1）——SVG path 的等价物
};

/**
 * @brief 一层。
 *
 * `radius` 是**归一化**的（0..1 相对法阵外径），所以 shape.json 里写的数
 * 与窗口大小无关。撞在一起就是叠图——`alpha` 让叠出来的颜色混成新色。
 */
struct ShapeLayer
{
    LayerKind kind = LayerKind::Curve;

    float radius = 0.0f;      ///< 0..1；**0 = 用本插件的基准半径**（自动分配的那一份）
    float width = 4.0f;       ///< 描边宽（设计像素）
    int lobes = 5;            ///< Curve = 波瓣数；Star/Polygon = 边数；Spokes = 辐条数
    float innerRatio = 0.5f;  ///< Star 的内/外半径比；Spokes 的起点比例
    float amplitude = 0.05f;  ///< 振幅（相对 radius 的比例，只有 Curve 用）
    float phaseDeg = 0.0f;    ///< 形状起始相位（度）

    float spinDeg = 0.0f;   ///< 自转（度/秒，可负）
    float orbitDeg = 6.0f;  ///< 绕点转（度/秒，可负）

    std::uint32_t color = 0xFFFFFFFFu;  ///< 含 alpha

    /**Path 的点列（`x0,y0,x1,y1,…`，归一化 -1..1，乘半径后贴到画布中心）。*/
    std::vector<float> points;

    // 沿线复制的小元素（塔齿 / 符文点），0 = 不画
    int markCount = 0;
    float markLength = 0.0f;  ///< >0 画径向短线；=0 画点
    float markSize = 3.0f;
    float markOffset = 0.0f;
    std::uint32_t markColor = 0xFFFFFFFFu;

    // `needs` 符印：-1 = 用 needs 条数，0 = 不画，>0 = 指定条数
    int runeCount = -1;
    float runeSize = 4.0f;
    float runeOffset = 8.0f;
    std::uint32_t runeColor = 0xFFFFFFFFu;
};

struct PluginShape
{
    /**本插件的基准半径（0..1）；没写就是 0（交给自动分配）。*/
    float baseRadius = 0.0f;
    bool hasBaseRadius = false;
    std::vector<ShapeLayer> layers;
    bool fromFile = false;
};

/**哈希派生一层：shape.json 里没写的字段全走这里。*/
ShapeLayer MakeHashLayer(std::uint32_t hash, int index)
{
    const std::uint32_t h = hash ^ (static_cast<std::uint32_t>(index) * 0x9E3779B9u);

    ShapeLayer layer;
    layer.lobes = 3 + static_cast<int>((h >> 4) % 6u);                              // 3..8
    layer.amplitude = 0.030f + static_cast<float>((h >> 8) % 6u) * 0.010f;           // 0.030..0.080
    layer.phaseDeg = static_cast<float>((h >> 12) % 360u);
    layer.width = 3.0f + static_cast<float>((h >> 16) % 5u);                        // 3..7
    layer.spinDeg = 2.0f + static_cast<float>((h >> 18) % 10u);                     // 2..11
    if (((h >> 19) & 1u) != 0u)
    {
        layer.spinDeg = -layer.spinDeg;
    }
    layer.orbitDeg = kDefaultOrbitDeg;
    layer.color = HashColor(h, 1.0f);
    layer.markColor = WithAlpha(layer.color, 0.55f);
    layer.runeColor = HashColor(h ^ 0x9E3779B9u, 1.0f);
    return layer;
}

bool ReadAllBytes(const std::string& utf8Path, std::string& out)
{
    const std::u8string wide(utf8Path.begin(), utf8Path.end());
    std::ifstream file(std::filesystem::path(wide), std::ios::binary);
    if (!file)
    {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !out.empty();
}

/**读一层；`base` 是哈希派生出来的那一份，缺的字段就留它。*/
ShapeLayer ReadLayer(const json::Value& node, const ShapeLayer& base, float orbitDefault)
{
    ShapeLayer layer = base;

    const std::string kind = json::Text(node, "kind", "");
    if (kind == "ring")
    {
        layer.kind = LayerKind::Ring;
    }
    else if (kind == "star")
    {
        layer.kind = LayerKind::Star;
    }
    else if (kind == "polygon")
    {
        layer.kind = LayerKind::Polygon;
    }
    else if (kind == "spokes")
    {
        layer.kind = LayerKind::Spokes;
    }
    else if (kind == "path")
    {
        layer.kind = LayerKind::Path;
    }

    layer.radius = json::Float(node, "radius", layer.radius);
    layer.width = json::Float(node, "width", layer.width);
    layer.lobes = static_cast<int>(json::Number(node, "lobes", layer.lobes));
    layer.innerRatio = json::Float(node, "innerRatio", layer.innerRatio);
    layer.amplitude = json::Float(node, "amplitude", layer.amplitude);
    layer.phaseDeg = json::Float(node, "phase", layer.phaseDeg);
    layer.spinDeg = json::Float(node, "spin", layer.spinDeg);
    layer.orbitDeg = json::Float(node, "orbit", orbitDefault);

    const float alpha = json::Float(node, "alpha", 1.0f);
    ParseHexColor(json::Text(node, "color", ""), alpha, layer.color);

    // `points`：`[[x,y], …]`，归一化 -1..1 —— 就是 SVG path 的顶点表。
    if (const json::Value* points = node.find("points");
        points != nullptr && points->type == json::Value::Type::Array)
    {
        layer.points.clear();
        for (const json::Value& pair : points->array)
        {
            if (pair.type == json::Value::Type::Array && pair.array.size() >= 2
                && pair.array[0].type == json::Value::Type::Number
                && pair.array[1].type == json::Value::Type::Number)
            {
                layer.points.push_back(static_cast<float>(pair.array[0].number));
                layer.points.push_back(static_cast<float>(pair.array[1].number));
            }
        }
    }

    if (const json::Value* marks = json::Object(node, "marks"))
    {
        layer.markCount = static_cast<int>(json::Number(*marks, "count", layer.markCount));
        layer.markLength = json::Float(*marks, "length", layer.markLength);
        layer.markSize = json::Float(*marks, "size", layer.markSize);
        layer.markOffset = json::Float(*marks, "offset", layer.markOffset);
        ParseHexColor(json::Text(*marks, "color", ""), json::Float(*marks, "alpha", 0.55f), layer.markColor);
    }

    if (const json::Value* runes = json::Object(node, "runes"))
    {
        layer.runeCount = static_cast<int>(json::Number(*runes, "count", layer.runeCount));
        layer.runeSize = json::Float(*runes, "size", layer.runeSize);
        layer.runeOffset = json::Float(*runes, "offset", layer.runeOffset);
        ParseHexColor(json::Text(*runes, "color", ""), json::Float(*runes, "alpha", 0.9f), layer.runeColor);
    }

    layer.lobes = std::clamp(layer.lobes, 2, 24);
    layer.width = std::clamp(layer.width, 0.5f, 40.0f);
    layer.innerRatio = std::clamp(layer.innerRatio, 0.05f, 0.98f);
    layer.amplitude = std::clamp(layer.amplitude, 0.0f, 0.30f);
    if (layer.radius > 0.0f)
    {
        layer.radius = std::clamp(layer.radius, 0.02f, 1.20f);
    }
    layer.markCount = std::clamp(layer.markCount, 0, 96);
    layer.runeCount = std::clamp(layer.runeCount, -1, 24);
    return layer;
}

/**
 * @brief 读 `plugin/<名字>/shape.json`；没有 / 读坏 / 没层，都算"没有"。
 *
 * 失败不是错误：形状会退回哈希派生——没有 shape.json 的插件照样看得见，
 * 这正是第一版做的事（见文件头的取舍）。
 */
bool LoadShape(const golding::Host& host, const std::string& pluginName, PluginShape& shape)
{
    const char* dir = host.PluginDir(pluginName.c_str());
    if (dir == nullptr || dir[0] == '\0')
    {
        return false;
    }

    std::string text;
    if (!ReadAllBytes(std::string(dir) + "/shape.json", text))
    {
        return false;
    }
    json::Value root;
    if (!json::Parse(text, root) || root.type != json::Value::Type::Object)
    {
        return false;
    }

    const float orbit = json::Float(root, "orbit", kDefaultOrbitDeg);
    if (const json::Value* radius = root.find("radius"); radius != nullptr && radius->type == json::Value::Type::Number)
    {
        shape.baseRadius = std::clamp(static_cast<float>(radius->number), 0.02f, 1.20f);
        shape.hasBaseRadius = true;
    }

    const json::Value* layers = root.find("layers");
    if (layers == nullptr || layers->type != json::Value::Type::Array || layers->array.empty())
    {
        return false;
    }

    const std::uint32_t hash = HashText(pluginName.c_str());
    for (std::size_t index = 0; index < layers->array.size() && index < 6; ++index)
    {
        const json::Value& node = layers->array[index];
        if (node.type != json::Value::Type::Object)
        {
            continue;
        }
        shape.layers.push_back(ReadLayer(node, MakeHashLayer(hash, static_cast<int>(index)), orbit));
    }

    shape.fromFile = !shape.layers.empty();
    return shape.fromFile;
}

// ---------------------------------------------------------------------------
// 状态：一张法阵的全部数据（建弹窗时算一次，之后只读）
// ---------------------------------------------------------------------------

/**一个插件 = 一组同心（或重叠）的层。 */
struct RingItem
{
    std::string name;
    int hole = 0;    ///< 宿主档案下标（`PluginAt` 那套编号）
    int needs = 0;   ///< `needs` 条数（符印的默认值）
    int fanIn = 0;   ///< 被多少插件依赖（自动半径时决定内外）
    float baseRadius = 0.0f;  ///< 0..1，已归一化
    std::vector<ShapeLayer> layers;
};

struct GrimoireState
{
    std::vector<RingItem> rings;
    int selected = -1;

    /**累计时间（秒）。用 double：`float` 跑几小时后 `seconds × speed` 会丢精度。*/
    double seconds = 0.0;

    float centerX = 0.0f;
    float centerY = 0.0f;
    float innerRadius = 0.0f;
    float outerRadius = 0.0f;

    /**视图：滚轮缩放 + 拖拽平移（法阵比视口大时靠它看全）。*/
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;

    /**
     * SVG 底纹节点。它是**静态**的（几何定型，跟着窗口走），缩放 / 平移时没法跟。
     * 所以一旦视图离开 1:1，就把它藏起来、改用画布自绘的盘（那个天生跟着 zoom 走）。
     */
    inking::InkingStaticImage* art = nullptr;
    bool artVisible = true;

    /**
     * 每一层的命中带按钮。它们也是**静态几何**（按 1:1 的半径造出来的），所以
     * 缩放后位置就对不上了——`UpdateArtVisibility` 会把它们一起藏掉，
     * 免得 hover 高亮出现在错误的地方。点击不依赖它们（走 `PickNearest`）。
     */
    std::vector<inking::InkingStaticButton*> layerButtons;

    /**
     * 指针最近的那一层（插件下标，-1 = 没有）。
     *
     * 每帧由 `SpellDriver` 算一次。为什么不让按钮自己 hover：命中带按钮是 1:1 的
     * 静态几何，缩放后被藏起来了，那时只能靠这一份"最近层"来画高亮。
     */
    int hovered = -1;

    inking::InkingStaticCanvas* canvas = nullptr;

    /**右侧那块可滚动的面板：详情文字变长变短时要重新量可滚范围。*/
    inking::InkingStaticPanel* detailPanel = nullptr;

    /**绘制用的点列缓冲：每层每帧一条曲线，复用它就没有每帧的堆分配。*/
    std::vector<float> pathScratch;

    // 右侧详情（换选中只改文字，不重建标签）
    inking::InkingStaticLabel* labelName = nullptr;
    inking::InkingStaticLabel* labelVersion = nullptr;
    inking::InkingStaticLabel* labelState = nullptr;
    inking::InkingStaticLabel* labelDepends = nullptr;
    inking::InkingStaticLabel* labelNeeds = nullptr;
    inking::InkingStaticLabel* labelShape = nullptr;
};

/**
 * @brief 建快照：插件表、形状、依赖、几何分配，**一次算完**。
 *
 * 插件表在 `Ready` 之后运行期不变，所以这些不该每帧重算。
 */
std::shared_ptr<GrimoireState> BuildState(const golding::Host& host, int leftWidth, int height)
{
    auto state = std::make_shared<GrimoireState>();
    const int count = host.PluginCount();

    const float shortSide = static_cast<float>(leftWidth < height ? leftWidth : height);
    const float outer = shortSide * 0.42f;
    const float inner = outer * 0.16f;
    state->centerX = static_cast<float>(leftWidth) * 0.5f;
    state->centerY = static_cast<float>(height) * 0.5f;
    state->innerRadius = inner;
    state->outerRadius = outer;

    state->rings.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        const GoldingPluginInfo* info = host.PluginAt(index);
        RingItem ring;
        ring.name = (info != nullptr && info->name != nullptr) ? info->name : "";
        ring.hole = index;
        ring.needs = host.PluginNeedCount(index);

        PluginShape shape;
        LoadShape(host, ring.name, shape);

        const std::uint32_t hash = HashText(ring.name.c_str());
        if (shape.layers.empty())
        {
            // 没有 shape.json：一层，哈希派生。
            ring.layers.push_back(MakeHashLayer(hash, 0));
        }
        else
        {
            ring.layers = std::move(shape.layers);
        }

        // 基准半径：shape.json 写了就用；否则留给下面自动分配。
        if (shape.hasBaseRadius)
        {
            ring.baseRadius = shape.baseRadius;
        }
        state->rings.push_back(std::move(ring));
    }

    // 扇入：依赖图是"名字对名字"的，两遍走。
    for (std::size_t i = 0; i < state->rings.size(); ++i)
    {
        const int dependCount = host.PluginDependencyCount(state->rings[i].hole);
        for (int d = 0; d < dependCount; ++d)
        {
            const char* target = host.PluginDependencyAt(state->rings[i].hole, d);
            if (target == nullptr)
            {
                continue;
            }
            for (RingItem& candidate : state->rings)
            {
                if (candidate.name == target)
                {
                    ++candidate.fanIn;
                    break;
                }
            }
        }
    }

    std::sort(state->rings.begin(), state->rings.end(), [](const RingItem& lhs, const RingItem& rhs) {
        if (lhs.fanIn != rhs.fanIn)
        {
            return lhs.fanIn > rhs.fanIn;
        }
        return lhs.name < rhs.name;
    });

    // 自动半径：**没写 radius 的**按序号均匀铺在 [0.30, 0.98] 上（被依赖多的靠内）。
    // 写了 radius 的保持原值——所以"叠在一起"就是两个 shape.json 写同一个数。
    std::vector<int> autoSlots;
    for (int i = 0; i < static_cast<int>(state->rings.size()); ++i)
    {
        if (state->rings[static_cast<std::size_t>(i)].baseRadius <= 0.0f)
        {
            autoSlots.push_back(i);
        }
    }
    const float slotStep = autoSlots.size() > 1
                               ? 0.68f / static_cast<float>(autoSlots.size() - 1)
                               : 0.0f;
    for (std::size_t k = 0; k < autoSlots.size(); ++k)
    {
        state->rings[static_cast<std::size_t>(autoSlots[k])].baseRadius = 0.30f + slotStep * static_cast<float>(k);
    }

    // 每层的绝对半径（0..1）在绘制时再乘外径，这里只保证层半径都有值。
    for (RingItem& ring : state->rings)
    {
        for (ShapeLayer& layer : ring.layers)
        {
            if (layer.radius <= 0.0f)
            {
                layer.radius = ring.baseRadius;
            }
        }
    }
    return state;
}

// ---------------------------------------------------------------------------
// 绘制（每帧一次；画布只在脏帧调它）
// ---------------------------------------------------------------------------

/**极坐标中线：给定角度上是多远（相对该层半径的比例）。只有 Curve 用得上。*/
float LayerRadiusRatio(const ShapeLayer& layer, float theta, float spinRad)
{
    return 1.0f + layer.amplitude * std::sin(static_cast<float>(layer.lobes) * theta + spinRad);
}

/**沿线复制元素（塔齿 / 符印）该落在多远：闭合曲线跟着中线走，几何图元落在圆上。*/
float LayerDecorRadius(const ShapeLayer& layer, float theta, float spinRad, float radiusPx)
{
    return layer.kind == LayerKind::Curve ? radiusPx * LayerRadiusRatio(layer, theta, spinRad) : radiusPx;
}

/**
 * @brief 生成一层的点列（画布坐标，`x0,y0,x1,y1,…`），返回**点数**。
 *
 * 六种图元最后都落到同一份顶点表上——这就是"统一基元"：中线不同，描边与命中
 * 都一样。`PolygonPath` 同时决定渲染 / 命中 / 包围盒，所以图元再多也不会分家。
 *
 * @param spinRad  自转相位（弧度）。
 * @param orbitRad 绕法阵中心的转角（弧度）。
 */
int BuildLayerPath(std::vector<float>& out, const GrimoireState& state, const ShapeLayer& layer,
                   float radiusPx, float spinRad, float orbitRad)
{
    const float centerX = state.centerX + state.panX;
    const float centerY = state.centerY + state.panY;

    const auto emitPolar = [&](int index, float radius, float theta) {
        const float angle = theta + orbitRad;
        out[static_cast<std::size_t>(index) * 2 + 0] = centerX + radius * std::cos(angle);
        out[static_cast<std::size_t>(index) * 2 + 1] = centerY + radius * std::sin(angle);
    };

    // Path：归一化点列（-1..1）乘半径再绕中心转 orbit —— SVG path 的等价物。
    if (layer.kind == LayerKind::Path && layer.points.size() >= 6)
    {
        const std::size_t count = layer.points.size() / 2;
        out.resize(count * 2);
        const float cosA = std::cos(orbitRad);
        const float sinA = std::sin(orbitRad);
        for (std::size_t i = 0; i < count; ++i)
        {
            const float x = layer.points[i * 2] * radiusPx;
            const float y = layer.points[i * 2 + 1] * radiusPx;
            out[i * 2 + 0] = centerX + x * cosA - y * sinA;
            out[i * 2 + 1] = centerY + x * sinA + y * cosA;
        }
        return static_cast<int>(count);
    }

    if (layer.kind == LayerKind::Star)
    {
        const int sides = std::clamp(layer.lobes, 3, 24);
        const int count = sides * 2;
        out.resize(static_cast<std::size_t>(count) * 2);
        for (int i = 0; i < count; ++i)
        {
            const float theta = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(count));
            emitPolar(i, (i % 2 == 0) ? radiusPx : radiusPx * layer.innerRatio, theta);
        }
        return count;
    }

    if (layer.kind == LayerKind::Polygon)
    {
        const int sides = std::clamp(layer.lobes, 3, 24);
        out.resize(static_cast<std::size_t>(sides) * 2);
        for (int i = 0; i < sides; ++i)
        {
            const float theta = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(sides));
            emitPolar(i, radiusPx, theta);
        }
        return sides;
    }

    // Curve / Ring / Spokes 都按极坐标采样（Ring 忽略振幅，就是正圆）。
    out.resize(static_cast<std::size_t>(kLayerSamples) * 2);
    for (int i = 0; i < kLayerSamples; ++i)
    {
        const float theta = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(kLayerSamples));
        const float radius =
            (layer.kind == LayerKind::Ring) ? radiusPx : radiusPx * LayerRadiusRatio(layer, theta, spinRad);
        emitPolar(i, radius, theta);
    }
    return kLayerSamples;
}

void PaintLayer(inking::CanvasContext& ctx, GrimoireState& state, const ShapeLayer& layer, int needs,
                bool selected, bool hovered)
{
    // 视图：半径乘 zoom，中心加 pan。线宽按 `sqrt(zoom)` 走——跟着线性放大会糊成一片，
    // 不放大又会在放大后细得像发丝。
    const float zoom = state.zoom;
    const float lineScale = std::sqrt(zoom);
    const float centerX = state.centerX + state.panX;
    const float centerY = state.centerY + state.panY;
    const float radiusPx = layer.radius * state.outerRadius * zoom;
    const float spinRad =
        layer.phaseDeg * kDegToRad + static_cast<float>(state.seconds) * layer.spinDeg * kDegToRad;
    const float orbitRad = static_cast<float>(state.seconds) * layer.orbitDeg * kDegToRad;

    // Spokes：不生成闭合路径，直接画一圈径向线（星芒 / 辐条）。
    if (layer.kind == LayerKind::Spokes)
    {
        const int count = std::clamp(layer.lobes, 2, 48);
        for (int i = 0; i < count; ++i)
        {
            const float theta = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(count));
            const float angle = theta + orbitRad;
            const float inner = radiusPx * layer.innerRatio;
            ctx.DrawLine(centerX + inner * std::cos(angle), centerY + inner * std::sin(angle),
                         centerX + radiusPx * std::cos(angle), centerY + radiusPx * std::sin(angle),
                         layer.width * lineScale, layer.color);
        }
        return;
    }

    const int count = BuildLayerPath(state.pathScratch, state, layer, radiusPx, spinRad, orbitRad);

    // 一圈**常驻的**淡光晕：底纹是金线，冷色环要靠它从底上浮起来。
    ctx.StrokeShape(inking::ShapeSpec::PolygonPath(state.pathScratch.data(), count), 0.0f, 0.0f, ctx.Width(),
                    ctx.Height(), layer.width * 2.2f * lineScale + 2.0f,
                    WithAlpha(layer.color, selected ? 0.16f : 0.07f));

    // 选中的层再铺一圈更宽更淡的：发光，而不是扎一圈刺。
    if (selected)
    {
        ctx.StrokeShape(inking::ShapeSpec::PolygonPath(state.pathScratch.data(), count), 0.0f, 0.0f,
                        ctx.Width(), ctx.Height(), layer.width * 4.4f * lineScale + 6.0f,
                        WithAlpha(layer.color, 0.10f));
    }

    // 悬停高亮：只在"按钮被藏起来"（缩放 / 平移）时由画布画——1:1 时按钮自己会亮，
    // 两边都画就重了。
    if (hovered && !selected)
    {
        ctx.StrokeShape(inking::ShapeSpec::PolygonPath(state.pathScratch.data(), count), 0.0f, 0.0f,
                        ctx.Width(), ctx.Height(), layer.width * 3.0f * lineScale + 4.0f,
                        WithAlpha(layer.color, 0.16f));
    }

    const float width = layer.width * lineScale * (selected ? 1.55f : 1.0f);
    ctx.StrokeShape(inking::ShapeSpec::PolygonPath(state.pathScratch.data(), count), 0.0f, 0.0f, ctx.Width(),
                    ctx.Height(), width, layer.color);

    // 沿线复制的小元素：塔齿（径向短线）或符文点。这就是"一组复制/变换"。
    if (layer.markCount > 0)
    {
        for (int i = 0; i < layer.markCount; ++i)
        {
            const float theta = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(layer.markCount));
            const float radius = LayerDecorRadius(layer, theta, spinRad, radiusPx) + layer.markOffset;
            const float angle = theta + orbitRad;
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            if (layer.markLength > 0.0f)
            {
                ctx.DrawLine(centerX + radius * cosA, centerY + radius * sinA,
                             centerX + (radius + layer.markLength) * cosA,
                             centerY + (radius + layer.markLength) * sinA, std::max(layer.markSize * 0.4f, 1.0f),
                             layer.markColor);
            }
            else
            {
                ctx.FillCircle(centerX + radius * cosA, centerY + radius * sinA, std::max(layer.markSize, 1.5f),
                               layer.markColor);
            }
        }
    }

    // `needs` 符印：贴在中线外侧。`runeCount < 0` 用 needs 条数。
    const int runeCount = layer.runeCount < 0 ? needs : layer.runeCount;
    if (runeCount > 0)
    {
        for (int i = 0; i < runeCount; ++i)
        {
            const float theta = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(runeCount));
            const float radius =
                LayerDecorRadius(layer, theta, spinRad, radiusPx) + layer.runeOffset + layer.width * 0.5f;
            const float angle = theta + orbitRad;
            ctx.FillCircle(centerX + radius * std::cos(angle), centerY + radius * std::sin(angle),
                           std::max(layer.runeSize, 2.0f), layer.runeColor);
        }
    }
}

void PaintGrimoire(inking::CanvasContext& ctx, GrimoireState& state)
{
    const float centerX = state.centerX + state.panX;
    const float centerY = state.centerY + state.panY;

    // 底纹（深盘 + 金纹 + 渐变）是 `assets/grimoire.svg`，由 `spellbook.art` 那张
    // 图片节点负责——它是**静态**的，用 SVG 画比在画布上堆图元省事得多（还能用
    // 渐变和虚线）。画布只管"会动的东西"：每一层的曲线、齿、符印。
    //
    // 但静态图片跟不上缩放：视图一旦离开 1:1，就把它藏起来、由这里自绘一个盘
    // （跟着 zoom / pan 走），否则环会跑到盘外面去。
    if (!state.artVisible)
    {
        const float scale = state.zoom;
        ctx.FillCircle(centerX, centerY, state.outerRadius * scale * 2.36f, 0xFF100C18u);
        for (int i = 3; i >= 1; --i)
        {
            const float radius = state.outerRadius * scale * (1.18f * 0.42f * static_cast<float>(i));
            ctx.FillCircle(centerX, centerY, radius * 2.0f, 0x0FD9B45Au);
        }
        {
            const float rim = state.outerRadius * scale * 1.18f;
            const float box = (rim + 0.75f) * 2.0f;
            ctx.FillShape(inking::ShapeSpec::Ring(1.5f), centerX - box * 0.5f, centerY - box * 0.5f, box, box,
                          0x99D9B45Au);
        }
    }
    if (state.rings.empty())
    {
        ctx.DrawText(std::string("还没有插件"), centerX - 36.0f, centerY - 8.0f, 16.0f, 0xFFB8A98Au);
        return;
    }

    for (std::size_t i = 0; i < state.rings.size(); ++i)
    {
        const RingItem& ring = state.rings[i];
        const bool selected = state.selected == static_cast<int>(i);
        // 悬停高亮只在画布负责时才算（按钮藏起来的那些视图状态）。
        const bool hovered = !state.artVisible && state.hovered == static_cast<int>(i) && !selected;
        for (const ShapeLayer& layer : ring.layers)
        {
            PaintLayer(ctx, state, layer, ring.needs, selected, hovered);
        }
    }

    // 中心核心：一个很小的阵眼。插件自己的形状已经够华丽了，这里只留一点"心"。
    const float core = state.innerRadius * state.zoom * 0.52f;
    ctx.FillCircle(centerX, centerY, core * 2.0f, 0x22D9B45Au);
    const float rotation = static_cast<float>(std::fmod(state.seconds * 9.0, 360.0));
    ctx.StrokeShape(inking::ShapeSpec::Polygon(6, rotation), centerX - core, centerY - core, core * 2.0f,
                    core * 2.0f, 1.0f, 0x99D9B45Au);
    ctx.FillCircle(centerX, centerY, core * 0.55f, 0xFFE8C979u);
}

// ---------------------------------------------------------------------------
// 详情（**接口占位**）
// ---------------------------------------------------------------------------

/**
 * @brief 一个插件的详情。
 *
 * **接口占位**：现在只有宿主档案这一路来源（`GoldingPluginInfo` + 依赖 / 能力
 * 查询）。将来"详情"若由插件自己提供，就在 `MakeDetail()` 里先问那个提供方、
 * 取不到再退回这里的档案——右侧面板只认这个结构，换来源不用动界面代码。
 */
struct PluginDetail
{
    std::string name;
    std::string version;
    std::string role;
    std::string state;
    std::vector<std::string> depends;
    std::vector<std::string> needs;
};

const char* StateText(int state)
{
    switch (state)
    {
    case 0:
        return "可用";
    case 1:
        return "manifest 不合法";
    case 2:
        return "打不开 dll";
    case 3:
        return "ABI 不匹配";
    case 5:
        return "entry 失败";
    case 6:
        return "依赖不满足";
    case 7:
        return "apiVersion 过高";
    case 8:
        return "sdkId 不匹配";
    default:
        return "未知状态";
    }
}

std::string RoleText(const std::string& role)
{
    if (role.empty())
    {
        return "普通插件";
    }
    if (role == "panel")
    {
        return "面板插件";
    }
    return role;
}

PluginDetail MakeDetail(const golding::Host& host, int index)
{
    PluginDetail detail;
    const GoldingPluginInfo* info = host.PluginAt(index);
    if (info != nullptr)
    {
        detail.name = info->name != nullptr ? info->name : "(无名)";
        detail.version = info->version != nullptr ? info->version : "?";
        detail.role = RoleText(info->role != nullptr ? info->role : "");
        detail.state = StateText(info->state);
    }

    const int dependCount = host.PluginDependencyCount(index);
    for (int i = 0; i < dependCount; ++i)
    {
        const char* target = host.PluginDependencyAt(index, i);
        if (target != nullptr)
        {
            detail.depends.emplace_back(target);
        }
    }

    const int needCount = host.PluginNeedCount(index);
    for (int i = 0; i < needCount; ++i)
    {
        unsigned int version = 0;
        const char* name = host.PluginNeedAt(index, i, &version);
        if (name == nullptr)
        {
            continue;
        }
        std::string text = name;
        if (version > 0)
        {
            text += " ≥ ";
            text += std::to_string(version);
        }
        detail.needs.push_back(std::move(text));
    }
    return detail;
}

std::string Join(const std::vector<std::string>& items, const char* separator)
{
    std::string joined;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
        {
            joined += separator;
        }
        joined += items[i];
    }
    return joined;
}

void UpdateDetail(GrimoireState& state)
{
    if (state.labelName == nullptr)
    {
        return;
    }

    if (state.selected < 0 || state.selected >= static_cast<int>(state.rings.size()))
    {
        state.labelName->SetText("未选中");
        state.labelVersion->SetText("点左侧的环，或上面列表里的一项。");
        state.labelState->SetText("");
        state.labelDepends->SetText("");
        state.labelNeeds->SetText("");
        state.labelShape->SetText("");
        return;
    }

    const RingItem& ring = state.rings[static_cast<std::size_t>(state.selected)];
    const PluginDetail detail = MakeDetail(g_host, ring.hole);

    state.labelName->SetText(detail.name);
    state.labelVersion->SetText(detail.version + " · " + detail.role);
    state.labelState->SetText("状态：" + detail.state);
    state.labelDepends->SetText(detail.depends.empty() ? "依赖：无" : "依赖：" + Join(detail.depends, "、"));
    state.labelNeeds->SetText(detail.needs.empty() ? "能力：无" : "能力：" + Join(detail.needs, "、"));
    state.labelShape->SetText("形状：" + std::to_string(ring.layers.size()) + " 层 · 半径 " +
                              std::to_string(static_cast<int>(ring.baseRadius * 100.0f)) + "%");
}

/**
 * @brief 视图是不是还在 1:1。
 *
 * 只有 1:1 时那张固定尺寸的 SVG 底纹才和环对得上；一旦缩放或平移，就切到画布
 * 自绘的盘（它跟着 zoom / pan 走），否则环会跑到盘外面去。
 */
void UpdateArtVisibility(GrimoireState& state)
{
    const bool plain = std::fabs(state.zoom - 1.0f) < 0.02f && std::fabs(state.panX) < 0.5f
                       && std::fabs(state.panY) < 0.5f;
    if (state.artVisible == plain)
    {
        return;
    }
    state.artVisible = plain;
    if (state.art != nullptr)
    {
        state.art->SetVisible(plain);
    }
    for (inking::InkingStaticButton* button : state.layerButtons)
    {
        if (button != nullptr)
        {
            button->SetVisible(plain);
        }
    }
}

void SelectRing(GrimoireState& state, int index)
{
    state.selected = index;
    UpdateDetail(state);
    // 文字变了要重排：`SetText` 只把这一段标脏，`Stack` 不会自己重算兄弟的位置
    // （几何没变），所以详情换行成两行时下一段会压上来。
    if (state.detailPanel != nullptr)
    {
        state.detailPanel->Relayout();
        state.detailPanel->UpdateScrollRange();
    }
}

/**
 * @brief 算"指针最近的那一层"属于哪个插件（-1 = 没有）。
 *
 * 不选、只算——点击与每帧高亮都问它同一份答案，两边不会分叉。
 */
int FindNearest(const GrimoireState& state)
{
    if (state.canvas == nullptr || state.rings.empty())
    {
        return -1;
    }
    inking::InkingScene* scene = state.canvas->GetScene();
    if (scene == nullptr)
    {
        return -1;
    }

    // 指针是世界坐标，几何是画布本地坐标：沿父链把偏移加起来即可（这一层没有变换）。
    float originX = 0.0f;
    float originY = 0.0f;
    for (const inking::InkingWidget* node = state.canvas; node != nullptr; node = node->GetParent())
    {
        originX += static_cast<float>(node->GetX());
        originY += static_cast<float>(node->GetY());
    }

    // 视图反变换：先减掉 pan，再除以 zoom，回到"世界尺寸下的半径"。
    const float zoom = state.zoom > 0.001f ? state.zoom : 1.0f;
    const float dx = ((scene->GetPointerX() - originX) - (state.centerX + state.panX)) / zoom;
    const float dy = ((scene->GetPointerY() - originY) - (state.centerY + state.panY)) / zoom;
    const float distance = std::sqrt(dx * dx + dy * dy);

    int best = -1;
    float bestGap = 1.0e9f;
    for (std::size_t i = 0; i < state.rings.size(); ++i)
    {
        for (const ShapeLayer& layer : state.rings[i].layers)
        {
            const float radiusPx = layer.radius * state.outerRadius;
            const float gap = std::fabs(distance - radiusPx);
            if (gap < bestGap)
            {
                bestGap = gap;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

/**
 * @brief 选**最近的那一层**，而不是"指针落在哪个环带里"。
 *
 * 环带一放宽就会互相重叠，落在重叠区时谁赢取决于按钮的 z 序——那正是"点着 A
 * 选中 B"。换成"算指针到每一层中线的距离、取最小"之后，碰撞区可以给得很慷慨
 * （24px 起步）也不会串。
 */
void PickNearest(GrimoireState& state)
{
    const int best = FindNearest(state);
    if (best >= 0)
    {
        SelectRing(state, best);
    }
}

// ---------------------------------------------------------------------------
// 时钟：法阵要转，就得有人每帧喊重画
// ---------------------------------------------------------------------------

/**
 * @brief 只做时钟的容器（1×1，不画、不吃点击）。
 *
 * 画布是静态档（没有 `Tick`），"每帧重画"要由动态组件代劳。时间源用库的
 * `Timeline`（逻辑步 / 每帧 / 自定义频率三条通道）：法阵现在只要"每帧"，
 * 将来要"按 30Hz 采样"加一行 `Subscribe` 就够，驱动这一层不用再动。
 */
class SpellDriver final : public inking::InkingDynamicWidget
{
public:
    SpellDriver(inking::InkingWidget* parent, std::shared_ptr<GrimoireState> state)
        : inking::InkingDynamicWidget(parent, std::string("spellbook.driver")), _state(std::move(state))
    {
        Resize(1, 1);
        _timeline.SetFrameCallback([this](double deltaSeconds) {
            if (!_state)
            {
                return;
            }
            _state->seconds += deltaSeconds;
            // 每帧重算"指针最近的那一层"：1:1 时按钮自己会 hover，缩放后按钮藏了，
            // 就得靠这份结果让画布把高亮画出来。
            _state->hovered = FindNearest(*_state);
            if (_state->canvas != nullptr)
            {
                // 只标**重绘脏**：几何没动，命中表一条都不用改。
                _state->canvas->Invalidate();
            }
        });
    }

protected:
    void onTick(float deltaSeconds) override
    {
        _timeline.Advance(static_cast<double>(deltaSeconds));
    }

    void onRender(float /*pixelX*/, float /*pixelY*/) const override {}

private:
    std::shared_ptr<GrimoireState> _state;
    inking::Timeline _timeline;
};

/**
 * @brief 法阵的输入层：**滚轮缩放、拖拽平移、点击选最近的一层**。
 *
 * 为什么不用一个按钮：按钮只有"点 / 悬停"，拿不到滚轮、也拿不到拖动增量。这里
 * 三件事都要（插件一多，环距会小于手抖精度，缩放是唯一出路），所以落成一个最小
 * 的 widget——重写指针钩子即可。它是**静态档**：几何构造即定型，所以照常进命中表。
 *
 * `HandlesSubtreeWheel` 返回 true 才收得到滚轮（场景沿父链找"要滚轮的人"）。
 */
class GrimoireView final : public inking::InkingStaticWidget
{
public:
    GrimoireView(inking::InkingWidget* parent, const inking::AnchorData& data,
                 std::shared_ptr<GrimoireState> state)
        : inking::InkingStaticWidget(parent, data), _state(std::move(state))
    {
    }

    bool HitTest(float worldX, float worldY) const override
    {
        float originX = 0.0f;
        float originY = 0.0f;
        for (const inking::InkingWidget* node = this; node != nullptr; node = node->GetParent())
        {
            originX += static_cast<float>(node->GetX());
            originY += static_cast<float>(node->GetY());
        }
        return worldX >= originX && worldX < originX + static_cast<float>(GetWidth()) && worldY >= originY
            && worldY < originY + static_cast<float>(GetHeight());
    }

protected:
    bool HandlesSubtreeWheel() const noexcept override { return true; }

    void onRender(float /*pixelX*/, float /*pixelY*/) const override {}

    void onPointerPress(inking::PointerButton button, bool press) override
    {
        if (button == inking::PointerButton::Primary && press)
        {
            _dragged = false;
        }
    }

    void onPointerDrag(float /*designX*/, float /*designY*/, float deltaX, float deltaY) override
    {
        if (!_state)
        {
            return;
        }
        // 拖过就不算"点击"（否则松手时会把拖动当成一次选环）。
        _dragged = true;
        _state->panX += deltaX;
        _state->panY += deltaY;
        UpdateArtVisibility(*_state);
        if (_state->canvas != nullptr)
        {
            _state->canvas->Invalidate();
        }
    }

    void onPointerClick(inking::PointerButton button, float /*x*/, float /*y*/) override
    {
        if (!_state || button != inking::PointerButton::Primary || _dragged)
        {
            return;
        }
        PickNearest(*_state);
    }

    void onPointerWheel(float /*deltaX*/, float deltaY) override
    {
        if (!_state)
        {
            return;
        }
        // 一格滚轮 ≈ ×1.12；夹在 [0.5, 5]——再小看不清，再大就只剩一条线。
        _state->zoom = std::clamp(_state->zoom * std::pow(1.12f, deltaY), 0.5f, 5.0f);
        UpdateArtVisibility(*_state);
        if (_state->canvas != nullptr)
        {
            _state->canvas->Invalidate();
        }
    }

private:
    std::shared_ptr<GrimoireState> _state;
    bool _dragged = false;
};

// ---------------------------------------------------------------------------
// 弹窗内容
// ---------------------------------------------------------------------------

/**
 * @brief 一条详情文字。
 *
 * **一律单行 + 省略号**：`Stack` 不会因为"文字换行变高了"而重排兄弟（几何没变），
 * 多行文字的下一段会压上来。详情是"看一眼"的东西，截断比叠字可接受。
 */
inking::LabelData MakeDetailLabel(const char* name, const char* text, float fontSize,
                                  std::uint32_t color, int width, int height)
{
    inking::LabelData label;
    label.name = name;
    label.width = width;
    label.height = height;
    label.text = text;
    label.color = color;
    label.style.fontSize = fontSize;
    label.style.alignV = inking::TextAlignV::Top;
    label.maxLines = 1;
    label.overflow = inking::TextOverflow::Ellipsis;
    return label;
}

GoldingWidgetHandle CreateGrimoire(void* /*hostContext*/, void* parent)
{
    auto* surface = static_cast<inking::InkingWidget*>(parent);
    if (surface == nullptr)
    {
        return nullptr;
    }

    const int width = surface->GetWidth();
    const int height = surface->GetHeight();
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    // 左 3 / 右 1。**所有坐标都以内容容器左上角为原点**，只有一套。
    const int leftWidth = width * kGrimoireShare / 4;
    const int detailWidth = width - leftWidth;

    std::shared_ptr<GrimoireState> state = BuildState(g_host, leftWidth, height);

    // ---- 左：底纹（一张 SVG，静态）----
    //
    // 正方形、居中放：法阵的圆心就是左区中心，所以图片中心必须对到那儿。
    // `runtimeAsset` 走 `AssetLoad`，它认**绝对路径**（`AssetResolve` 那一条），
    // 所以直接给插件目录下的文件即可，不用管 exe 旁的资源根。
    // `hitPassThrough`：底盘不吃点击，否则下面那圈环按钮一个都点不到。
    {
        const float artSize = static_cast<float>(leftWidth < height ? leftWidth : height);
        inking::ImageData art;
        art.name = "spellbook.art";
        art.width = static_cast<int>(artSize);
        art.height = static_cast<int>(artSize);
        art.offsetX = (static_cast<float>(leftWidth) - artSize) * 0.5f;
        art.offsetY = (static_cast<float>(height) - artSize) * 0.5f;
        art.runtimeAsset = true;
        art.hitPassThrough = true;
        art.normal.type = inking::AppearanceType::Image;
        art.normal.source = std::string(g_host.PluginDir()) + "/assets/grimoire.svg";
        art.shape = inking::ShapeSpec::Rect();
        art.zIndex = 0;
        state->art = static_cast<inking::InkingStaticImage*>(
            surface->AddChild(std::make_unique<inking::InkingStaticImage>(surface, art)));
    }

    // ---- 左：法阵画布（只画，不参与命中）----
    inking::CanvasData canvasData;
    canvasData.name = "spellbook.grimoire";
    canvasData.width = leftWidth;
    canvasData.height = height;
    canvasData.color = 0x00000000u;  // 本体透明：这里只要内容
    canvasData.shape = inking::ShapeSpec::Rect();

    auto canvas = std::make_unique<inking::InkingStaticCanvas>(surface, canvasData);
    inking::InkingStaticCanvas* canvasRaw = canvas.get();
    if (surface->AddChild(std::move(canvas)) == nullptr)
    {
        return nullptr;
    }
    state->canvas = canvasRaw;
    canvasRaw->SetPainter([state](inking::CanvasContext& ctx) { PaintGrimoire(ctx, *state); });

    // ---- 左：命中层 ----
    //
    // 分两层：
    //   1. **一张铺满左区的大按钮**（z=5）：把"点空白也算数"接住；
    //   2. **每层一圈环带**（`Ring`）：它的作用是**悬停高亮**——按钮的 hover 态
    //      就是这一层的环带，设成层色 + 低 alpha，指针一靠近就亮一圈，不用自己写
    //      hover 逻辑。
    //
    // 两个的点击都走 `PickNearest()`：选"指针最近的那一层"，而不是"落在哪个带里"。
    // 带放宽之后本来就会互相重叠，靠 z 序裁决必然出现"点着 A 选中 B"。
    //
    // 颜色 alpha = 1 而不是 0：全透明会触发按钮的"配错了"兜底（品红）。1/255 的
    // 白在视觉上等于看不见。
    {
        // 输入层：滚轮缩放 + 拖拽平移 + 点击选最近层。它盖满左区、本身不画东西，
        // 所以"点空白"也能解析出一个最近的层。
        inking::AnchorData view;
        view.width = leftWidth;
        view.height = height;
        view.zIndex = 5;
        surface->AddChild(std::make_unique<GrimoireView>(surface, view, state));
    }

    for (std::size_t i = 0; i < state->rings.size(); ++i)
    {
        const RingItem& ring = state->rings[i];
        int slot = 0;
        for (const ShapeLayer& layer : ring.layers)
        {
            const float radiusPx = layer.radius * state->outerRadius;

            // 碰撞带：**要按"手指"给，不是按"线宽"给**。
            // 一条 1.6px 的线只配 8px 的带，鼠标都得瞄——24px 起步才勉强好点。
            // 星形/多边形还多一层：视觉范围从内半径一直到外半径（星尖在外、星凹在
            // 内），所以带要更宽，否则只有星尖那一圈能点中。
            float band = std::max(layer.width * 5.0f, 24.0f);
            if (layer.kind == LayerKind::Star || layer.kind == LayerKind::Polygon)
            {
                band = std::max(band, radiusPx * (1.0f - layer.innerRatio) * 0.8f + 20.0f);
            }
            const float outer = radiusPx + band * 0.5f;

            inking::ButtonData hit;
            hit.name = "spellbook.ring." + std::to_string(i) + "." + std::to_string(slot);
            hit.width = static_cast<int>(outer * 2.0f);
            hit.height = hit.width;
            hit.normal.type = inking::AppearanceType::Color;
            hit.normal.color = 0x01FFFFFFu;
            hit.hover.type = inking::AppearanceType::Color;
            hit.hover.color = WithAlpha(layer.color, 0.13f);
            hit.hoverInheritsNormal = false;
            hit.onclicked.type = inking::AppearanceType::Color;
            hit.onclicked.color = WithAlpha(layer.color, 0.22f);
            hit.onclickedInheritsNormal = false;
            hit.offsetX = state->centerX - outer;
            hit.offsetY = state->centerY - outer;
            hit.shape = inking::ShapeSpec::Ring(band);
            hit.zIndex = 10 + static_cast<int>(slot);

            auto button = std::make_unique<inking::InkingStaticButton>(surface, hit);
            auto* buttonRaw = button.get();
            if (surface->AddChild(std::move(button)) == nullptr)
            {
                continue;
            }
            state->layerButtons.push_back(buttonRaw);
            buttonRaw->SetOnClicked([state] { PickNearest(*state); });
            ++slot;
        }
    }

    // ---- 右：可滚动的"插件列表 + 详情"----
    //
    // 一块 `InkingStaticPanel` 同时干两件事：竖排（`Stack`）+ 滚动（`ScrollSpec`）。
    // 列表在上、详情在下，内容比视口高就滚——这就是方案 D 的载体（点列表项 = 选中环）。
    const int padding = 18;
    const int scrollReserve = 14;  // 竖直滚动条浮在内容上，右边多留一条给它
    inking::PanelData detail;
    detail.name = "spellbook.detail";
    detail.width = detailWidth;
    detail.height = height;
    detail.startOpen = true;
    detail.closed.color = theme::kSurfaceAlt;
    detail.open = detail.closed;
    detail.offsetX = static_cast<float>(leftWidth);
    detail.shape = inking::ShapeSpec::Rect();
    detail.clipChildren = true;
    detail.scroll.enabled = true;
    detail.scroll.vertical = true;
    detail.scroll.barWidth = 8.0f;
    detail.layout.kind = inking::LayoutKind::Stack;
    detail.layout.axis = inking::LayoutAxis::Vertical;
    detail.layout.gap = 8.0f;
    detail.layout.padding = inking::LayoutSpacing{static_cast<float>(padding),
                                                  static_cast<float>(padding + scrollReserve),
                                                  static_cast<float>(padding), static_cast<float>(padding)};

    auto detailPanel = std::make_unique<inking::InkingStaticPanel>(surface, detail);
    inking::InkingStaticPanel* detailRaw = detailPanel.get();
    if (surface->AddChild(std::move(detailPanel)) == nullptr)
    {
        return nullptr;
    }
    state->detailPanel = detailRaw;

    const int textWidth = detailWidth - padding * 2 - scrollReserve;
    auto addLabel = [detailRaw, textWidth](inking::LabelData label) {
        auto node = std::make_unique<inking::InkingStaticLabel>(detailRaw, label);
        auto* raw = node.get();
        detailRaw->AddChild(std::move(node));
        return raw;
    };

    // ---- 列表：一项一个插件，点它就等于点那一环 ----
    addLabel(MakeDetailLabel("spellbook.list.title", ("插件 · " + std::to_string(state->rings.size())).c_str(),
                             17.0f, theme::kTextPrimary, textWidth, 26));
    for (std::size_t i = 0; i < state->rings.size(); ++i)
    {
        const RingItem& ring = state->rings[i];
        inking::ButtonData item;
        item.name = "spellbook.item." + std::to_string(i);
        item.width = textWidth;
        item.height = 34;
        item.normal.type = inking::AppearanceType::Color;
        item.normal.color = theme::kSurface;
        item.normal.themeColor = "panelSurface";
        item.hover.type = inking::AppearanceType::Color;
        item.hover.color = theme::kHoverNeutral;
        item.onclicked.type = inking::AppearanceType::Color;
        item.onclicked.color = theme::kAccentSoft;
        item.hoverInheritsNormal = false;
        item.onclickedInheritsNormal = false;
        item.label = ring.name;
        item.textColor = theme::kTextPrimary;
        item.text.fontSize = 13.0f;
        item.text.align = inking::TextAlignH::Left;
        item.text.alignV = inking::TextAlignV::Middle;
        item.text.leftSpace = 10.0f;
        item.shape = inking::ShapeSpec::RoundedRect(8.0f);

        auto node = std::make_unique<inking::InkingStaticButton>(detailRaw, item);
        auto* raw = node.get();
        if (detailRaw->AddChild(std::move(node)) == nullptr)
        {
            continue;
        }
        const int index = static_cast<int>(i);
        raw->SetOnClicked([state, index] { SelectRing(*state, index); });
    }

    // ---- 详情（列表下面；内容总高超过视口时整块滚）----
    addLabel(MakeDetailLabel("spellbook.detail.head", "详情", 15.0f, theme::kTextPrimary, textWidth, 24));
    state->labelName =
        addLabel(MakeDetailLabel("spellbook.detail.name", "", 19.0f, theme::kTextPrimary, textWidth, 34));
    state->labelVersion =
        addLabel(MakeDetailLabel("spellbook.detail.version", "", 13.0f, theme::kTextMuted, textWidth, 22));
    state->labelState =
        addLabel(MakeDetailLabel("spellbook.detail.state", "", 13.0f, theme::kTextPrimary, textWidth, 22));
    state->labelDepends =
        addLabel(MakeDetailLabel("spellbook.detail.depends", "", 13.0f, theme::kTextMuted, textWidth, 22));
    state->labelNeeds =
        addLabel(MakeDetailLabel("spellbook.detail.needs", "", 13.0f, theme::kTextMuted, textWidth, 22));
    state->labelShape =
        addLabel(MakeDetailLabel("spellbook.detail.shape", "", 13.0f, theme::kTextMuted, textWidth, 22));

    // ---- 时钟 ----
    if (surface->AddChild(std::make_unique<SpellDriver>(surface, state)) == nullptr)
    {
        return nullptr;
    }

    UpdateDetail(*state);
    // 列表 + 详情都挂完了：排一次版，再量一次内容高度（不然滚轮的可滚范围是 0）。
    detailRaw->Relayout();
    detailRaw->UpdateScrollRange();
    g_host.Info("spellbook：法阵图已建立");
    return surface;
}

const golding::GoldingPopupDecl kGrimoirePopup = {"spellbook.grimoire.popup", kPopupWidth, kPopupHeight, 0, 0, 1,
                                                  &CreateGrimoire, nullptr};

/**打开法阵图。设置菜单点「插件图谱」、和兜底面板里的按钮，走的都是这一条。*/
void OpenGrimoire(void* /*hostContext*/)
{
    if (g_popup == nullptr)
    {
        g_host.Warn("spellbook：没有 golding.popup，法阵图打不开");
        return;
    }
    g_popup->push(nullptr, &kGrimoirePopup);
}

// ---------------------------------------------------------------------------
// 顶栏「插件」：一个区域项 + 两份动作
// ---------------------------------------------------------------------------
//
// 不再往"设置"里塞区块，而是成为顶栏独立的一栏。点它弹一份菜单：
// 「插件图谱」/「打开插件文件夹」。
//
// 顶栏项的 `onActivate` 不带坐标（见 `GoldingRegionItemDecl`），所以"菜单开在
// 哪个按钮下面"要交给面板——`golding.menu` 的锚点就是**区域项 id**。

/**打开系统文件管理器，停在 `plugin/` 目录（自己目录往上两级：core → plugin）。*/
void OpenPluginFolder(void* /*hostContext*/, const char* /*itemId*/)
{
#ifdef _WIN32
    const char* selfDir = g_host.PluginDir();
    if (selfDir == nullptr || selfDir[0] == '\0')
    {
        g_host.Warn("spellbook：拿不到自己的插件目录，打不开文件夹");
        return;
    }
    const std::string selfText(selfDir);
    const std::filesystem::path selfPath(std::u8string(selfText.begin(), selfText.end()));
    const std::filesystem::path pluginDir = selfPath.parent_path().parent_path();

    std::error_code error;
    if (!std::filesystem::exists(pluginDir, error))
    {
        g_host.Warn("spellbook：插件目录不存在（按 `<plugin>/core/<名字>` 上溯两级没找到），不打开");
        return;
    }
    const std::wstring wide = pluginDir.wstring();
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32)
    {
        g_host.Warn("spellbook：打开插件文件夹失败");
    }
#else
    g_host.Warn("spellbook：只有 Windows 上实现了打开文件夹");
#endif
}

void OnMenuOpenGrimoire(void* /*hostContext*/, const char* /*itemId*/)
{
    OpenGrimoire(nullptr);
}

/**顶栏「插件」被点：弹两份动作。*/
void OnEntryActivate(void* /*hostContext*/)
{
    if (g_menu == nullptr)
    {
        // 没有菜单服务：直接开图谱，功能不丢（文件夹那条只能作罢）。
        g_host.Warn("spellbook：没有 golding.menu，直接打开图谱");
        OpenGrimoire(nullptr);
        return;
    }

    static const golding::MenuItem kItems[] = {
        {"spellbook.open", "插件图谱", nullptr, 0, &OnMenuOpenGrimoire},
        {"spellbook.folder", "打开插件文件夹", nullptr, 0, &OpenPluginFolder},
    };
    const golding::MenuDecl decl{"spellbook.menu", kItems, 2};
    if (g_menu->open(nullptr, kEntryId, &decl) != 0)
    {
        g_host.Warn("spellbook：菜单没打开（锚点找不到？）");
    }
}

/**顶栏那一项：面板画成按钮，文字就是 title。*/
const GoldingRegionItemDecl kEntry = {"menubar", kEntryId, "插件", nullptr, 60, nullptr, &OnEntryActivate};

// ---------------------------------------------------------------------------
// 命令面板（`golding.commands`）：把这两个动作也挂进去
// ---------------------------------------------------------------------------
//
// **不写 `needs`**：命令面板不在时本插件照常工作，只是少两条命令（`Use` 取不到
// 就 WARN 跳过）。反过来写就成了"没有命令面板就装不上插件"。
//
// 回调直接复用菜单那两个——命令面板与菜单只是两条入口，动作只有一份。

/**`golding.commands` 要的是 `void(void*)`，而菜单项是 `void(void*, const char*)`。*/
void OnCommandFolder(void* /*hostContext*/) { OpenPluginFolder(nullptr, nullptr); }

const golding::CommandDecl kCommandOpenGrimoire{
    "spellbook.open",
    "打开插件图谱",
    "grimoire tupu plugin chajian",
    &OpenGrimoire,
};

const golding::CommandDecl kCommandOpenFolder{
    "spellbook.folder",
    "打开插件文件夹",
    "folder dir wenjianjia",
    &OnCommandFolder,
};

/**注册失败不算错误：打一行 WARN，插件照常工作。 */
void RegisterCommands(const golding::Host& host)
{
    const golding::CommandService* commands =
        host.Use<golding::CommandService>(golding::kCommandsServiceName, golding::kCommandsServiceVersion);
    if (commands == nullptr)
    {
        host.Warn("spellbook：没有 golding.commands（命令面板缺席），两条命令不注册");
        return;
    }
    if (commands->add(nullptr, &kCommandOpenGrimoire) != 0)
    {
        host.Warn("spellbook：命令 spellbook.open 注册被驳回（id 已占用？）");
    }
    if (commands->add(nullptr, &kCommandOpenFolder) != 0)
    {
        host.Warn("spellbook：命令 spellbook.folder 注册被驳回（id 已占用？）");
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 插件本体
// ---------------------------------------------------------------------------

class SpellbookPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host;
        g_popup = host.Use<golding::PopupService>(golding::kPopupServiceName, golding::kPopupServiceVersion);
        g_menu = host.Use<golding::MenuService>(golding::kMenuServiceName, 1);

        if (g_popup == nullptr)
        {
            host.Warn("spellbook：没有 golding.popup（面板插件缺席），图谱打不开");
        }
        if (g_menu == nullptr)
        {
            host.Warn("spellbook：没有 golding.menu（面板插件缺席），顶栏入口会退化成直接开图谱");
        }
        if (!host.AddRegionItem(kEntry))
        {
            // 区域不存在（换了个面板插件）不该让整个插件失败：图谱还能从别处开。
            host.Warn("spellbook：顶栏「插件」没注册上（menubar 区域不在）");
        }
        RegisterCommands(host);
        return true;
    }

    int Ready(const golding::Host& host) override
    {
        // "没有插件"是**合法状态**（空界面、退出码 0），不该算失败——只留一句提示。
        if (host.PluginCount() <= 0)
        {
            host.Warn("spellbook：插件表是空的，法阵图只会画出中心");
        }
        return 0;
    }
};

GOLDING_EXPORT_PLUGIN(SpellbookPlugin)
