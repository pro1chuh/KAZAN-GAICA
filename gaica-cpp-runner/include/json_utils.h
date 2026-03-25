#pragma once

#include <string>
#include <map>

namespace JsonUtils {
    std::map<std::string, std::string> parse_json_line(const std::string& line);
    std::string to_json_line(const std::map<std::string, std::string>& data);
    std::string get_or_default(const std::map<std::string, std::string>& data,
                               const std::string& key,
                               const std::string& default_val);
}