#include "json_utils.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace JsonUtils {

std::map<std::string, std::string> parse_json_line(const std::string& line) {
    std::map<std::string, std::string> result;
    try {
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return result;
        trimmed = trimmed.substr(start);
        if (trimmed.front() == '{') trimmed = trimmed.substr(1);
        if (trimmed.back() == '}') trimmed = trimmed.substr(0, trimmed.length() - 1);
        
        size_t pos = 0;
        while (pos < trimmed.length()) {
            pos = trimmed.find_first_not_of(" \t\r\n,", pos);
            if (pos == std::string::npos) break;
            if (trimmed[pos] != '"') { pos++; continue; }
            
            size_t key_start = pos;
            size_t key_end = trimmed.find('"', key_start + 1);
            if (key_end == std::string::npos) break;
            std::string key = trimmed.substr(key_start + 1, key_end - key_start - 1);
            
            size_t colon = trimmed.find(':', key_end);
            if (colon == std::string::npos) break;
            
            size_t val_start = trimmed.find_first_not_of(" \t", colon + 1);
            if (val_start == std::string::npos) break;
            
            std::string value;
            if (trimmed[val_start] == '"') {
                size_t val_end = trimmed.find('"', val_start + 1);
                if (val_end == std::string::npos) break;
                value = trimmed.substr(val_start + 1, val_end - val_start - 1);
                pos = val_end + 1;
            } else if (trimmed[val_start] == '[') {
                int bracket_count = 1;
                size_t val_end = val_start + 1;
                while (val_end < trimmed.length() && bracket_count > 0) {
                    if (trimmed[val_end] == '[') bracket_count++;
                    else if (trimmed[val_end] == ']') bracket_count--;
                    val_end++;
                }
                value = trimmed.substr(val_start, val_end - val_start);
                pos = val_end;
            } else {
                size_t val_end = trimmed.find_first_of(",}", val_start);
                if (val_end == std::string::npos) val_end = trimmed.length();
                value = trimmed.substr(val_start, val_end - val_start);
                value.erase(value.find_last_not_of(" \t") + 1);
                pos = val_end;
            }
            result[key] = value;
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
    }
    return result;
}

std::string to_json_line(const std::map<std::string, std::string>& data) {
    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& [key, value] : data) {
        if (!first) ss << ",";
        ss << "\"" << key << "\":";
        if (value == "true" || value == "false" || value == "null" ||
            (!value.empty() && (value[0] >= '0' && value[0] <= '9')) || value[0] == '-') {
            ss << value;
        } else {
            ss << "\"" << value << "\"";
        }
        first = false;
    }
    ss << "}\n";
    return ss.str();
}

std::string get_or_default(const std::map<std::string, std::string>& data,
                           const std::string& key,
                           const std::string& default_val) {
    auto it = data.find(key);
    return (it != data.end()) ? it->second : default_val;
}

} // namespace JsonUtils