// QubitOS - Lightweight JSON Parser and Serializer for C++17
// MIT License - Copyright (c) 2026 QubitOS Project
// Based on standard JSON specification RFC 8259

#ifndef QUBITOS_JSON_HPP
#define QUBITOS_JSON_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cctype>
#include <cmath>

namespace qubitos {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class Json {
public:
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<Json> arr_val;
    std::map<std::string, Json> obj_val;

    Json() : type(JsonType::Null) {}
    Json(std::nullptr_t) : type(JsonType::Null) {}
    Json(bool b) : type(JsonType::Boolean), bool_val(b) {}
    Json(int n) : type(JsonType::Number), num_val(n) {}
    Json(unsigned int n) : type(JsonType::Number), num_val(n) {}
    Json(long n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    Json(unsigned long n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    Json(long long n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    Json(unsigned long long n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    Json(double n) : type(JsonType::Number), num_val(n) {}
    Json(const char* s) : type(JsonType::String), str_val(s) {}
    Json(const std::string& s) : type(JsonType::String), str_val(s) {}
    Json(std::string&& s) : type(JsonType::String), str_val(std::move(s)) {}
    Json(const std::vector<Json>& arr) : type(JsonType::Array), arr_val(arr) {}
    Json(std::vector<Json>&& arr) : type(JsonType::Array), arr_val(std::move(arr)) {}
    Json(const std::map<std::string, Json>& obj) : type(JsonType::Object), obj_val(obj) {}
    Json(std::map<std::string, Json>&& obj) : type(JsonType::Object), obj_val(std::move(obj)) {}

    static Json array() {
        Json j;
        j.type = JsonType::Array;
        return j;
    }

    static Json object() {
        Json j;
        j.type = JsonType::Object;
        return j;
    }

    bool is_null() const { return type == JsonType::Null; }
    bool is_bool() const { return type == JsonType::Boolean; }
    bool is_number() const { return type == JsonType::Number; }
    bool is_string() const { return type == JsonType::String; }
    bool is_array() const { return type == JsonType::Array; }
    bool is_object() const { return type == JsonType::Object; }

    template<typename T>
    T get() const;

    Json& operator[](const std::string& key) {
        if (type != JsonType::Object) {
            type = JsonType::Object;
            obj_val.clear();
        }
        return obj_val[key];
    }

    const Json& operator[](const std::string& key) const {
        if (type != JsonType::Object) {
            throw std::runtime_error("Json is not an object");
        }
        auto it = obj_val.find(key);
        if (it == obj_val.end()) {
            throw std::runtime_error("Key not found in Json object: " + key);
        }
        return it->second;
    }

    Json& operator[](size_t index) {
        if (type != JsonType::Array) {
            type = JsonType::Array;
            arr_val.clear();
        }
        if (index >= arr_val.size()) {
            arr_val.resize(index + 1);
        }
        return arr_val[index];
    }

    const Json& operator[](size_t index) const {
        if (type != JsonType::Array) {
            throw std::runtime_error("Json is not an array");
        }
        if (index >= arr_val.size()) {
            throw std::out_of_range("Json array index out of bounds");
        }
        return arr_val[index];
    }

    bool contains(const std::string& key) const {
        if (type != JsonType::Object) return false;
        return obj_val.find(key) != obj_val.end();
    }

    size_t size() const {
        if (type == JsonType::Array) return arr_val.size();
        if (type == JsonType::Object) return obj_val.size();
        if (type == JsonType::String) return str_val.size();
        return 0;
    }

    bool empty() const {
        return size() == 0;
    }

    void push_back(const Json& val) {
        if (type != JsonType::Array) {
            type = JsonType::Array;
            arr_val.clear();
        }
        arr_val.push_back(val);
    }

    void push_back(Json&& val) {
        if (type != JsonType::Array) {
            type = JsonType::Array;
            arr_val.clear();
        }
        arr_val.push_back(std::move(val));
    }

    std::string dump(int indent = -1, int current_indent = 0) const {
        std::ostringstream ss;
        std::string ind_str = (indent > 0) ? std::string(current_indent, ' ') : "";
        std::string next_ind_str = (indent > 0) ? std::string(current_indent + indent, ' ') : "";
        std::string nl = (indent >= 0) ? "\n" : "";
        std::string sp = (indent >= 0) ? " " : "";

        switch (type) {
            case JsonType::Null:
                ss << "null";
                break;
            case JsonType::Boolean:
                ss << (bool_val ? "true" : "false");
                break;
            case JsonType::Number:
                if (std::floor(num_val) == num_val && !std::isinf(num_val) && std::abs(num_val) < 1e15) {
                    ss << static_cast<long long>(num_val);
                } else {
                    ss << std::setprecision(10) << num_val;
                }
                break;
            case JsonType::String:
                ss << escape_string(str_val);
                break;
            case JsonType::Array:
                if (arr_val.empty()) {
                    ss << "[]";
                } else {
                    ss << "[" << nl;
                    for (size_t i = 0; i < arr_val.size(); ++i) {
                        ss << next_ind_str << arr_val[i].dump(indent, current_indent + indent);
                        if (i + 1 < arr_val.size()) ss << ",";
                        ss << nl;
                    }
                    ss << ind_str << "]";
                }
                break;
            case JsonType::Object:
                if (obj_val.empty()) {
                    ss << "{}";
                } else {
                    ss << "{" << nl;
                    size_t i = 0;
                    for (auto it = obj_val.begin(); it != obj_val.end(); ++it, ++i) {
                        ss << next_ind_str << escape_string(it->first) << ":" << sp
                           << it->second.dump(indent, current_indent + indent);
                        if (i + 1 < obj_val.size()) ss << ",";
                        ss << nl;
                    }
                    ss << ind_str << "}";
                }
                break;
        }
        return ss.str();
    }

    static std::string escape_string(const std::string& s) {
        std::ostringstream ss;
        ss << '"';
        for (char c : s) {
            switch (c) {
                case '"': ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        ss << c;
                    }
                    break;
            }
        }
        ss << '"';
        return ss.str();
    }

    // Parser implementation
    static Json parse(const std::string& src) {
        size_t idx = 0;
        skip_whitespace(src, idx);
        Json result = parse_value(src, idx);
        skip_whitespace(src, idx);
        if (idx < src.size()) {
            throw std::runtime_error("Unexpected trailing character at index " + std::to_string(idx));
        }
        return result;
    }

private:
    static void skip_whitespace(const std::string& s, size_t& idx) {
        while (idx < s.size() && (s[idx] == ' ' || s[idx] == '\t' || s[idx] == '\n' || s[idx] == '\r')) {
            idx++;
        }
    }

    static Json parse_value(const std::string& s, size_t& idx) {
        skip_whitespace(s, idx);
        if (idx >= s.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        char c = s[idx];
        if (c == 'n') return parse_null(s, idx);
        if (c == 't' || c == 'f') return parse_bool(s, idx);
        if (c == '"') return parse_string(s, idx);
        if (c == '[') return parse_array(s, idx);
        if (c == '{') return parse_object(s, idx);
        if (c == '-' || std::isdigit(c)) return parse_number(s, idx);
        throw std::runtime_error(std::string("Unexpected character: '") + c + "' at position " + std::to_string(idx));
    }

    static Json parse_null(const std::string& s, size_t& idx) {
        if (s.substr(idx, 4) == "null") {
            idx += 4;
            return Json(nullptr);
        }
        throw std::runtime_error("Expected null");
    }

    static Json parse_bool(const std::string& s, size_t& idx) {
        if (s.substr(idx, 4) == "true") {
            idx += 4;
            return Json(true);
        }
        if (s.substr(idx, 5) == "false") {
            idx += 5;
            return Json(false);
        }
        throw std::runtime_error("Expected boolean");
    }

    static Json parse_number(const std::string& s, size_t& idx) {
        size_t start = idx;
        if (s[idx] == '-') idx++;
        while (idx < s.size() && std::isdigit(s[idx])) idx++;
        if (idx < s.size() && s[idx] == '.') {
            idx++;
            while (idx < s.size() && std::isdigit(s[idx])) idx++;
        }
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            idx++;
            if (idx < s.size() && (s[idx] == '+' || s[idx] == '-')) idx++;
            while (idx < s.size() && std::isdigit(s[idx])) idx++;
        }
        std::string num_str = s.substr(start, idx - start);
        double val = std::stod(num_str);
        return Json(val);
    }

    static Json parse_string(const std::string& s, size_t& idx) {
        idx++; // skip opening quote
        std::string res;
        while (idx < s.size()) {
            char c = s[idx++];
            if (c == '"') return Json(res);
            if (c == '\\') {
                if (idx >= s.size()) throw std::runtime_error("Unterminated escape sequence in string");
                char esc = s[idx++];
                switch (esc) {
                    case '"': res += '"'; break;
                    case '\\': res += '\\'; break;
                    case '/': res += '/'; break;
                    case 'b': res += '\b'; break;
                    case 'f': res += '\f'; break;
                    case 'n': res += '\n'; break;
                    case 'r': res += '\r'; break;
                    case 't': res += '\t'; break;
                    case 'u': {
                        if (idx + 4 > s.size()) throw std::runtime_error("Invalid unicode escape");
                        std::string hex_str = s.substr(idx, 4);
                        idx += 4;
                        unsigned int codepoint = std::stoul(hex_str, nullptr, 16);
                        if (codepoint <= 0x7F) {
                            res += static_cast<char>(codepoint);
                        } else if (codepoint <= 0x7FF) {
                            res += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                            res += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            res += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                            res += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            res += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default: res += esc; break;
                }
            } else {
                res += c;
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    static Json parse_array(const std::string& s, size_t& idx) {
        idx++; // skip '['
        Json arr = Json::array();
        skip_whitespace(s, idx);
        if (idx < s.size() && s[idx] == ']') {
            idx++;
            return arr;
        }
        while (idx < s.size()) {
            arr.push_back(parse_value(s, idx));
            skip_whitespace(s, idx);
            if (idx >= s.size()) throw std::runtime_error("Unterminated array");
            if (s[idx] == ']') {
                idx++;
                return arr;
            }
            if (s[idx] == ',') {
                idx++;
                skip_whitespace(s, idx);
            } else {
                throw std::runtime_error("Expected ',' or ']' in array");
            }
        }
        throw std::runtime_error("Unterminated array");
    }

    static Json parse_object(const std::string& s, size_t& idx) {
        idx++; // skip '{'
        Json obj = Json::object();
        skip_whitespace(s, idx);
        if (idx < s.size() && s[idx] == '}') {
            idx++;
            return obj;
        }
        while (idx < s.size()) {
            skip_whitespace(s, idx);
            if (idx >= s.size() || s[idx] != '"') {
                throw std::runtime_error("Expected string key in object");
            }
            Json key_json = parse_string(s, idx);
            std::string key = key_json.str_val;
            skip_whitespace(s, idx);
            if (idx >= s.size() || s[idx] != ':') {
                throw std::runtime_error("Expected ':' after key in object");
            }
            idx++; // skip ':'
            skip_whitespace(s, idx);
            Json val = parse_value(s, idx);
            obj.obj_val[key] = std::move(val);
            skip_whitespace(s, idx);
            if (idx >= s.size()) throw std::runtime_error("Unterminated object");
            if (s[idx] == '}') {
                idx++;
                return obj;
            }
            if (s[idx] == ',') {
                idx++;
                skip_whitespace(s, idx);
            } else {
                throw std::runtime_error("Expected ',' or '}' in object");
            }
        }
        throw std::runtime_error("Unterminated object");
    }
};

template<>
inline int Json::get<int>() const {
    if (type != JsonType::Number) throw std::runtime_error("Json value is not a number");
    return static_cast<int>(num_val);
}

template<>
inline unsigned int Json::get<unsigned int>() const {
    if (type != JsonType::Number) throw std::runtime_error("Json value is not a number");
    return static_cast<unsigned int>(num_val);
}

template<>
inline size_t Json::get<size_t>() const {
    if (type != JsonType::Number) throw std::runtime_error("Json value is not a number");
    return static_cast<size_t>(num_val);
}

template<>
inline double Json::get<double>() const {
    if (type != JsonType::Number) throw std::runtime_error("Json value is not a number");
    return num_val;
}

template<>
inline bool Json::get<bool>() const {
    if (type != JsonType::Boolean) throw std::runtime_error("Json value is not a boolean");
    return bool_val;
}

template<>
inline std::string Json::get<std::string>() const {
    if (type != JsonType::String) throw std::runtime_error("Json value is not a string");
    return str_val;
}

} // namespace qubitos

#endif // QUBITOS_JSON_HPP

