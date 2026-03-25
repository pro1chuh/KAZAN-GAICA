// dependency_checker.h
#pragma once

#include <string>
#include <vector>
#include <set>
#include <regex>

class DependencyChecker {
public:
    struct DependencyResult {
        bool valid = true;
        std::vector<std::string> allowed_imports;
        std::vector<std::string> disallowed_imports;
        std::vector<std::string> unknown_imports;
        std::string message;
    };
    
    DependencyChecker();
    
    // Проверка всех файлов в директории бота
    DependencyResult check_bot(const std::string& bot_path);
    
    // Проверка отдельного файла
    DependencyResult check_file(const std::string& file_path);
    
    // Получить список разрешённых библиотек
    static const std::set<std::string>& allowed_libraries();
    
    // Получить список запрещённых библиотек
    static const std::set<std::string>& disallowed_libraries();
    
private:
    std::set<std::string> allowed;
    std::set<std::string> disallowed;
    std::set<std::string> local_ignore;  // имена локальных модулей для игнорирования
    std::regex import_regex;
    
    void initialize_lists();
    std::vector<std::string> extract_imports(const std::string& content);
    std::string read_file(const std::string& path);
    void collect_python_files(const std::string& dir, std::vector<std::string>& files);
    std::string normalize_module_name(const std::string& import_str);
    bool is_local_module(const std::string& module_name);
};