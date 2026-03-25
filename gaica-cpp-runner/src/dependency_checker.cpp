// dependency_checker.cpp - обновлённая версия
#include "dependency_checker.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

DependencyChecker::DependencyChecker() {
    initialize_lists();
    import_regex = std::regex(
        R"(^\s*(?:from\s+([a-zA-Z_][a-zA-Z0-9_.]*)\s+import|import\s+([a-zA-Z_][a-zA-Z0-9_.]*)))"
    );
}

// dependency_checker.cpp - найти функцию initialize_lists() и добавить:

void DependencyChecker::initialize_lists() {
    allowed = {
        // Стандартная библиотека Python
        "os", "sys", "json", "socket", "threading", "asyncio",
        "math", "random", "time", "datetime", "collections", "itertools",
        "functools", "pathlib", "argparse", "logging", "queue", "struct",
        "pickle", "copy", "hashlib", "base64", "re", "string", "io",
        "abc", "contextlib", "enum", "types", "typing", "warnings",
        "signal", "subprocess", "tempfile", "traceback", "weakref",
        "dataclasses", "__future__", "__main__", "builtins",
        
        // Научные библиотеки (есть на сервере)
        "numpy", "np",
        "scipy",
        "sklearn", "scikit-learn",
        "xgboost",
        "catboost",
        "torch",
    };
    
    // Запрещённые библиотеки (нет на сервере)
    disallowed = {
        "tensorflow", "tf",
        "keras",
        "transformers",
        "cv2", "opencv", "opencv-python",
        "PIL", "Pillow",
        "pandas", "pd",
        "matplotlib", "plt",
        "seaborn",
        "requests",
        "aiohttp",
        "flask", "fastapi",
        "django",
        "pytest",
        "numba",
        "joblib",
        "tqdm",
        "yaml", "pyyaml",
        "protobuf",
        "grpcio",
        "tensorboard",
        "torchvision",
        "torchaudio",
        "tensorflow_hub",
        "tensorflow_datasets",
        "h5py", "hdf5",
        "cupy", "cuda", "nvcuda", "pycuda", "cudnn",
        "gym", "pygame", "opengl", "tkinter", "wxpython",
        "pydot", "graphviz"
    };
    
    // Модули, которые мы игнорируем (локальные файлы)
    local_ignore = {
        "main", "bot", "agent", "strategy", "utils", "helpers",
        "config", "settings", "models", "network", "policy", "value",
        "my_bot", "my_agent", "my_strategy", "bot_aggressive",
        "bot_defensive", "smurf_bot", "terminator", "ml_bot"
    };
}

const std::set<std::string>& DependencyChecker::allowed_libraries() {
    static std::set<std::string> allowed_set = {
        "os", "sys", "json", "socket", "threading", "asyncio",
        "math", "random", "time", "datetime", "collections", "itertools",
        "functools", "pathlib", "argparse", "logging", "queue",
        "numpy", "scipy", "sklearn", "xgboost", "catboost", "torch"
    };
    return allowed_set;
}

const std::set<std::string>& DependencyChecker::disallowed_libraries() {
    static std::set<std::string> disallowed_set = {
        "tensorflow", "keras", "transformers", "cv2", "PIL", 
        "pandas", "matplotlib", "requests", "flask", "fastapi"
    };
    return disallowed_set;
}

std::string DependencyChecker::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string DependencyChecker::normalize_module_name(const std::string& import_str) {
    std::string name = import_str;
    
    // Убираем точки (берём корневой модуль)
    size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        name = name.substr(0, dot_pos);
    }
    
    // Приводим к нижнему регистру
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    
    // Нормализация альтернативных имён
    if (name == "np") name = "numpy";
    if (name == "pd") name = "pandas";
    if (name == "plt") name = "matplotlib";
    if (name == "sklearn") name = "scikit-learn";
    if (name == "tf") name = "tensorflow";
    if (name == "cv2") name = "opencv";
    
    return name;
}

std::vector<std::string> DependencyChecker::extract_imports(const std::string& content) {
    std::vector<std::string> imports;
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        std::smatch match;
        
        // Пропускаем комментарии
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Ищем import
        if (std::regex_search(line, match, import_regex)) {
            std::string module = match[1].matched ? match[1].str() : match[2].str();
            if (!module.empty()) {
                imports.push_back(normalize_module_name(module));
            }
        }
        
        // Пропускаем относительные импорты
        if (line.find("from .") != std::string::npos) {
            continue;
        }
    }
    
    return imports;
}

void DependencyChecker::collect_python_files(const std::string& dir, std::vector<std::string>& files) {
    try {
        if (!fs::exists(dir)) {
            return;
        }
        
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".py") {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << "\n";
    }
}

bool DependencyChecker::is_local_module(const std::string& module_name) {
    // Проверяем, не является ли модуль локальным файлом
    if (local_ignore.find(module_name) != local_ignore.end()) {
        return true;
    }
    
    // Если имя начинается с подчёркивания или содержит точки
    if (module_name.find('.') != std::string::npos) {
        return true;
    }
    
    return false;
}

DependencyChecker::DependencyResult DependencyChecker::check_bot(const std::string& bot_path) {
    DependencyResult result;
    result.valid = true;
    
    std::vector<std::string> py_files;
    collect_python_files(bot_path, py_files);
    
    if (py_files.empty()) {
        result.valid = false;
        result.message = "No Python files found in bot directory: " + bot_path;
        return result;
    }
    
    std::set<std::string> all_imports;
    
    for (const auto& file : py_files) {
        auto file_result = check_file(file);
        
        for (const auto& imp : file_result.allowed_imports) {
            all_imports.insert(imp);
            result.allowed_imports.push_back(imp);
        }
        for (const auto& imp : file_result.disallowed_imports) {
            all_imports.insert(imp);
            result.disallowed_imports.push_back(imp);
        }
        for (const auto& imp : file_result.unknown_imports) {
            // Проверяем, не локальный ли это модуль
            if (!is_local_module(imp)) {
                all_imports.insert(imp);
                result.unknown_imports.push_back(imp);
            }
        }
        
        if (!file_result.valid) {
            result.valid = false;
            result.message += file_result.message + "\n";
        }
    }
    
    // Убираем дубликаты
    auto remove_duplicates = [](std::vector<std::string>& vec) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    };
    
    remove_duplicates(result.allowed_imports);
    remove_duplicates(result.disallowed_imports);
    remove_duplicates(result.unknown_imports);
    
    if (!result.disallowed_imports.empty()) {
        result.valid = false;
        std::string msg = "❌ Disallowed libraries found: ";
        for (const auto& lib : result.disallowed_imports) {
            msg += lib + " ";
        }
        result.message = msg;
    }
    
    return result;
}

DependencyChecker::DependencyResult DependencyChecker::check_file(const std::string& file_path) {
    DependencyResult result;
    result.valid = true;
    
    std::string content = read_file(file_path);
    if (content.empty()) {
        result.valid = false;
        result.message = "Cannot read file: " + file_path;
        return result;
    }
    
    std::vector<std::string> imports = extract_imports(content);
    
    for (const auto& imp : imports) {
        if (allowed.find(imp) != allowed.end()) {
            result.allowed_imports.push_back(imp);
        } else if (disallowed.find(imp) != disallowed.end()) {
            result.disallowed_imports.push_back(imp);
            result.valid = false;
        } else {
            // Неизвестный импорт - может быть локальным файлом
            if (!is_local_module(imp)) {
                result.unknown_imports.push_back(imp);
            }
        }
    }
    
    if (!result.disallowed_imports.empty()) {
        result.message = "Disallowed imports in " + file_path + ": ";
        for (const auto& lib : result.disallowed_imports) {
            result.message += lib + " ";
        }
    }
    
    return result;
}