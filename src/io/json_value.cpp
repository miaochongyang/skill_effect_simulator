#include "io/json_value.hpp"

#include <cctype>

namespace sim::io {

// 轻量 JSON 解析器设计目标：
// 1) 只覆盖当前模拟器配置所需子集（对象/数组/字符串/数值/布尔/null）；
// 2) 零第三方依赖，保证工程可移植性；
// 3) 失败时返回明确位置，便于快速修配置。
JsonValue::Type JsonValue::GetType() const noexcept {
    if (std::holds_alternative<std::monostate>(value_)) {
        return Type::Null;
    }
    if (std::holds_alternative<bool>(value_)) {
        return Type::Bool;
    }
    if (std::holds_alternative<double>(value_)) {
        return Type::Number;
    }
    if (std::holds_alternative<std::string>(value_)) {
        return Type::String;
    }
    if (std::holds_alternative<Array>(value_)) {
        return Type::Array;
    }
    return Type::Object;
}

bool JsonValue::AsBool() const {
    if (!IsBool()) {
        throw std::runtime_error("JSON type mismatch: expected bool.");
    }
    return std::get<bool>(value_);
}

double JsonValue::AsNumber() const {
    if (!IsNumber()) {
        throw std::runtime_error("JSON type mismatch: expected number.");
    }
    return std::get<double>(value_);
}

const std::string& JsonValue::AsString() const {
    if (!IsString()) {
        throw std::runtime_error("JSON type mismatch: expected string.");
    }
    return std::get<std::string>(value_);
}

const JsonValue::Array& JsonValue::AsArray() const {
    if (!IsArray()) {
        throw std::runtime_error("JSON type mismatch: expected array.");
    }
    return std::get<Array>(value_);
}

const JsonValue::Object& JsonValue::AsObject() const {
    if (!IsObject()) {
        throw std::runtime_error("JSON type mismatch: expected object.");
    }
    return std::get<Object>(value_);
}

bool JsonValue::HasKey(std::string_view key) const {
    if (!IsObject()) {
        return false;
    }
    const auto& obj = std::get<Object>(value_);
    return obj.find(std::string(key)) != obj.end();
}

const JsonValue* JsonValue::TryGet(std::string_view key) const {
    if (!IsObject()) {
        return nullptr;
    }
    const auto& obj = std::get<Object>(value_);
    const auto it = obj.find(std::string(key));
    if (it == obj.end()) {
        return nullptr;
    }
    return &it->second;
}

JsonValue JsonParser::ParseText(const std::string& text) {
    JsonParser parser(text);
    JsonValue root = parser.ParseValue();
    parser.SkipWhitespace();
    if (!parser.End()) {
        throw parser.Error("Unexpected trailing content.");
    }
    return root;
}

JsonValue JsonParser::ParseValue() {
    SkipWhitespace();
    if (End()) {
        throw Error("Unexpected end of input.");
    }

    const char c = Peek();
    if (c == '{') {
        return ParseObject();
    }
    if (c == '[') {
        return ParseArray();
    }
    if (c == '"') {
        return ParseString();
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
        return ParseNumber();
    }
    return ParseLiteral();
}

JsonValue JsonParser::ParseObject() {
    Match('{');
    JsonValue::Object obj;
    SkipWhitespace();
    if (Match('}')) {
        return JsonValue(std::move(obj));
    }

    while (true) {
        SkipWhitespace();
        JsonValue key = ParseString();
        SkipWhitespace();
        if (!Match(':')) {
            throw Error("Expected ':' in object.");
        }

        JsonValue value = ParseValue();
        obj.emplace(key.AsString(), std::move(value));

        SkipWhitespace();
        if (Match('}')) {
            break;
        }
        if (!Match(',')) {
            throw Error("Expected ',' between object members.");
        }
    }
    return JsonValue(std::move(obj));
}

JsonValue JsonParser::ParseArray() {
    Match('[');
    JsonValue::Array arr;
    SkipWhitespace();
    if (Match(']')) {
        return JsonValue(std::move(arr));
    }

    while (true) {
        arr.push_back(ParseValue());
        SkipWhitespace();
        if (Match(']')) {
            break;
        }
        if (!Match(',')) {
            throw Error("Expected ',' between array items.");
        }
    }
    return JsonValue(std::move(arr));
}

JsonValue JsonParser::ParseString() {
    if (!Match('"')) {
        throw Error("Expected string opening quote.");
    }
    std::string out;
    out.reserve(32);

    while (!End()) {
        const char c = text_[pos_++];
        if (c == '"') {
            return JsonValue(std::move(out));
        }
        if (c == '\\') {
            if (End()) {
                throw Error("Invalid escape at end of string.");
            }
            const char e = text_[pos_++];
            switch (e) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
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
                default:
                    throw Error("Unsupported string escape.");
            }
            continue;
        }
        out.push_back(c);
    }
    throw Error("Unterminated string.");
}

JsonValue JsonParser::ParseNumber() {
    const std::size_t begin = pos_;
    if (Match('-')) {
        // minus sign consumed
    }

    if (End()) {
        throw Error("Invalid number.");
    }

    if (Peek() == '0') {
        ++pos_;
    } else {
        if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
            throw Error("Invalid number integer part.");
        }
        while (!End() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            ++pos_;
        }
    }

    if (!End() && Peek() == '.') {
        ++pos_;
        if (End() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
            throw Error("Invalid number fractional part.");
        }
        while (!End() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            ++pos_;
        }
    }

    if (!End() && (Peek() == 'e' || Peek() == 'E')) {
        ++pos_;
        if (!End() && (Peek() == '+' || Peek() == '-')) {
            ++pos_;
        }
        if (End() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
            throw Error("Invalid number exponent.");
        }
        while (!End() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            ++pos_;
        }
    }

    // 通过切片交给 stod 解析，避免手写浮点转换细节。
    const std::string token = text_.substr(begin, pos_ - begin);
    return JsonValue(std::stod(token));
}

JsonValue JsonParser::ParseLiteral() {
    if (text_.compare(pos_, 4, "true") == 0) {
        pos_ += 4;
        return JsonValue(true);
    }
    if (text_.compare(pos_, 5, "false") == 0) {
        pos_ += 5;
        return JsonValue(false);
    }
    if (text_.compare(pos_, 4, "null") == 0) {
        pos_ += 4;
        return JsonValue();
    }
    throw Error("Unknown literal.");
}

void JsonParser::SkipWhitespace() {
    while (!End() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
        ++pos_;
    }
}

char JsonParser::Peek() const {
    if (End()) {
        throw Error("Unexpected end of input.");
    }
    return text_[pos_];
}

bool JsonParser::Match(const char c) {
    if (!End() && text_[pos_] == c) {
        ++pos_;
        return true;
    }
    return false;
}

bool JsonParser::End() const noexcept {
    return pos_ >= text_.size();
}

std::runtime_error JsonParser::Error(const std::string& msg) const {
    return std::runtime_error("JSON parse error at pos " + std::to_string(pos_) + ": " + msg);
}

} // namespace sim::io
