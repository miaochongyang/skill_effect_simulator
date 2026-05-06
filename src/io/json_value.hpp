#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sim::io {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;

    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    JsonValue() = default;
    explicit JsonValue(bool v) : value_(v) {}
    explicit JsonValue(double v) : value_(v) {}
    explicit JsonValue(std::string v) : value_(std::move(v)) {}
    explicit JsonValue(Array v) : value_(std::move(v)) {}
    explicit JsonValue(Object v) : value_(std::move(v)) {}

    [[nodiscard]] Type GetType() const noexcept;
    [[nodiscard]] bool IsNull() const noexcept { return std::holds_alternative<std::monostate>(value_); }
    [[nodiscard]] bool IsBool() const noexcept { return std::holds_alternative<bool>(value_); }
    [[nodiscard]] bool IsNumber() const noexcept { return std::holds_alternative<double>(value_); }
    [[nodiscard]] bool IsString() const noexcept { return std::holds_alternative<std::string>(value_); }
    [[nodiscard]] bool IsArray() const noexcept { return std::holds_alternative<Array>(value_); }
    [[nodiscard]] bool IsObject() const noexcept { return std::holds_alternative<Object>(value_); }

    [[nodiscard]] bool AsBool() const;
    [[nodiscard]] double AsNumber() const;
    [[nodiscard]] const std::string& AsString() const;
    [[nodiscard]] const Array& AsArray() const;
    [[nodiscard]] const Object& AsObject() const;

    [[nodiscard]] bool HasKey(std::string_view key) const;
    [[nodiscard]] const JsonValue* TryGet(std::string_view key) const;

private:
    std::variant<std::monostate, bool, double, std::string, Array, Object> value_;
};

class JsonParser {
public:
    static JsonValue ParseText(const std::string& text);

private:
    explicit JsonParser(const std::string& text) : text_(text), pos_(0) {}

    [[nodiscard]] JsonValue ParseValue();
    [[nodiscard]] JsonValue ParseObject();
    [[nodiscard]] JsonValue ParseArray();
    [[nodiscard]] JsonValue ParseString();
    [[nodiscard]] JsonValue ParseNumber();
    [[nodiscard]] JsonValue ParseLiteral();

    void SkipWhitespace();
    [[nodiscard]] char Peek() const;
    [[nodiscard]] bool Match(char c);
    [[nodiscard]] bool End() const noexcept;
    [[nodiscard]] std::runtime_error Error(const std::string& msg) const;

    const std::string& text_;
    std::size_t pos_;
};

} // namespace sim::io
