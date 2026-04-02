#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>

// Cross-platform filesystem (C++17)
#ifdef __has_include
#  if __has_include(<filesystem>)
#    include <filesystem>
     namespace fs = std::filesystem;
#  elif __has_include(<experimental/filesystem>)
#    include <experimental/filesystem>
     namespace fs = std::experimental::filesystem;
#  else
     // Fallback implementation
     namespace fs {
         bool exists(const std::string&) { return false; }
         bool remove(const std::string&) { return false; }
         void create_directory(const std::string&) {}
         class directory_iterator {
         public:
             directory_iterator(const std::string&) {}
             bool operator!=(const directory_iterator&) { return false; }
             directory_iterator& operator++() { return *this; }
             struct path { std::string string() const { return ""; } };
             path path() const { return {}; }
         };
     }
#  endif
#else
// Conservative fallback
namespace fs {
    bool exists(const std::string&) { return false; }
    bool remove(const std::string&) { return false; }
    void create_directory(const std::string&) {}
    class directory_iterator {
    public:
        directory_iterator(const std::string&) {}
        bool operator!=(const directory_iterator&) { return false; }
        directory_iterator& operator++() { return *this; }
        struct path { std::string string() const { return ""; } };
        path path() const { return {}; }
    };
}
#endif

// Crypto functions (simplified for cross-compilation)
namespace Crypto {
    std::string sha256(const std::string& input) {
        // Simple hash for demo - replace with proper crypto library
        std::hash<std::string> hasher;
        auto hash = hasher(input);
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }

    std::string aesEncrypt(const std::string& plaintext, const std::string& key) {
        // Placeholder - implement with proper crypto library
        return "encrypted:" + plaintext;
    }

    std::string aesDecrypt(const std::string& ciphertext, const std::string& key) {
        // Placeholder - implement with proper crypto library
        if (ciphertext.substr(0, 10) == "encrypted:") {
            return ciphertext.substr(10);
        }
        return "";
    }
}

// Tyne Runtime Library
// Provides implementations for all builtin functions

namespace TyneRuntime {

// =========================
//  Type System
// =========================

enum class Type {
    INT32, INT64, UINT32, UINT64, FLOAT, DOUBLE, STRING, LIST, OBJECT
};

struct Value {
    Type type;
    union {
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        float f;
        double d;
        std::string* str;
        std::vector<Value>* list;
        std::unordered_map<std::string, Value>* obj;
    };

    Value() : type(Type::INT32), i32(0) {}
    Value(int32_t v) : type(Type::INT32), i32(v) {}
    Value(int64_t v) : type(Type::INT64), i64(v) {}
    Value(uint32_t v) : type(Type::UINT32), u32(v) {}
    Value(uint64_t v) : type(Type::UINT64), u64(v) {}
    Value(float v) : type(Type::FLOAT), f(v) {}
    Value(double v) : type(Type::DOUBLE), d(v) {}
    Value(const std::string& v) : type(Type::STRING), str(new std::string(v)) {}
    Value(std::vector<Value> v) : type(Type::LIST), list(new std::vector<Value>(std::move(v))) {}
    Value(std::unordered_map<std::string, Value> v) : type(Type::OBJECT), obj(new std::unordered_map<std::string, Value>(std::move(v))) {}

    ~Value() {
        if (type == Type::STRING) delete str;
        else if (type == Type::LIST) delete list;
        else if (type == Type::OBJECT) delete obj;
    }

    Value(const Value& other) {
        type = other.type;
        switch (type) {
            case Type::INT32: i32 = other.i32; break;
            case Type::INT64: i64 = other.i64; break;
            case Type::UINT32: u32 = other.u32; break;
            case Type::UINT64: u64 = other.u64; break;
            case Type::FLOAT: f = other.f; break;
            case Type::DOUBLE: d = other.d; break;
            case Type::STRING: str = new std::string(*other.str); break;
            case Type::LIST: list = new std::vector<Value>(*other.list); break;
            case Type::OBJECT: obj = new std::unordered_map<std::string, Value>(*other.obj); break;
        }
    }

    Value& operator=(const Value& other) {
        if (this != &other) {
            this->~Value();
            new (this) Value(other);
        }
        return *this;
    }

    std::string toString() const {
        switch (type) {
            case Type::INT32: return std::to_string(i32);
            case Type::INT64: return std::to_string(i64);
            case Type::UINT32: return std::to_string(u32);
            case Type::UINT64: return std::to_string(u64);
            case Type::FLOAT: return std::to_string(f);
            case Type::DOUBLE: return std::to_string(d);
            case Type::STRING: return *str;
            case Type::LIST: {
                std::string s = "[";
                for (size_t i = 0; i < list->size(); ++i) {
                    if (i > 0) s += ", ";
                    s += (*list)[i].toString();
                }
                s += "]";
                return s;
            }
            case Type::OBJECT: {
                std::string s = "{";
                bool first = true;
                for (const auto& [k, v] : *obj) {
                    if (!first) s += ", ";
                    s += k + ": " + v.toString();
                    first = false;
                }
                s += "}";
                return s;
            }
        }
        return "unknown";
    }
};

// =========================
//  IO Operations
// =========================

Value print(const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.toString();
    }
    std::cout << std::endl;
    return Value(0);
}

Value println(const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.toString() << std::endl;
    }
    return Value(0);
}

Value readln(const std::vector<Value>& args) {
    (void)args;
    std::string line;
    std::getline(std::cin, line);
    return Value(line);
}

Value readFile(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("readFile requires string filename");
    }
    std::ifstream file(*args[0].str);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + *args[0].str);
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    return Value(content);
}

Value writeFile(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::STRING) {
        throw std::runtime_error("writeFile requires string filename and content");
    }
    std::ofstream file(*args[0].str);
    if (!file) {
        throw std::runtime_error("Cannot open file for writing: " + *args[0].str);
    }
    file << args[1].toString();
    return Value(0);
}

// =========================
//  Math Operations
// =========================

Value abs(const std::vector<Value>& args) {
    if (args.size() != 1) throw std::runtime_error("abs requires 1 argument");
    const auto& v = args[0];
    switch (v.type) {
        case Type::INT32: return Value(std::abs(v.i32));
        case Type::INT64: return Value(std::abs(v.i64));
        case Type::FLOAT: return Value(std::abs(v.f));
        case Type::DOUBLE: return Value(std::abs(v.d));
        default: throw std::runtime_error("abs requires numeric argument");
    }
}

Value sqrt(const std::vector<Value>& args) {
    if (args.size() != 1) throw std::runtime_error("sqrt requires 1 argument");
    const auto& v = args[0];
    if (v.type == Type::DOUBLE) return Value(std::sqrt(v.d));
    if (v.type == Type::FLOAT) return Value(std::sqrt(v.f));
    throw std::runtime_error("sqrt requires floating point argument");
}

Value pow(const std::vector<Value>& args) {
    if (args.size() != 2) throw std::runtime_error("pow requires 2 arguments");
    const auto& base = args[0];
    const auto& exp = args[1];
    if (base.type == Type::DOUBLE && exp.type == Type::DOUBLE) {
        return Value(std::pow(base.d, exp.d));
    }
    throw std::runtime_error("pow requires double arguments");
}

Value sin(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::DOUBLE) {
        throw std::runtime_error("sin requires double argument");
    }
    return Value(std::sin(args[0].d));
}

Value cos(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::DOUBLE) {
        throw std::runtime_error("cos requires double argument");
    }
    return Value(std::cos(args[0].d));
}

Value tan(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::DOUBLE) {
        throw std::runtime_error("tan requires double argument");
    }
    return Value(std::tan(args[0].d));
}

Value random(const std::vector<Value>& args) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    if (args.empty()) {
        std::uniform_real_distribution<> dis(0.0, 1.0);
        return Value(dis(gen));
    } else if (args.size() == 1) {
        if (args[0].type == Type::INT32) {
            std::uniform_int_distribution<> dis(0, args[0].i32 - 1);
            return Value(dis(gen));
        }
    }
    throw std::runtime_error("random: invalid arguments");
}

// =========================
//  String Operations
// =========================

Value length(const std::vector<Value>& args) {
    if (args.size() != 1) throw std::runtime_error("length requires 1 argument");
    const auto& v = args[0];
    if (v.type == Type::STRING) return Value((int32_t)v.str->length());
    if (v.type == Type::LIST) return Value((int32_t)v.list->size());
    throw std::runtime_error("length requires string or list argument");
}

Value substring(const std::vector<Value>& args) {
    if (args.size() < 2 || args.size() > 3 || args[0].type != Type::STRING) {
        throw std::runtime_error("substring requires string and integer arguments");
    }
    const std::string& str = *args[0].str;
    size_t start = (size_t)args[1].i32;
    size_t len = (args.size() == 3) ? (size_t)args[2].i32 : std::string::npos;
    return Value(str.substr(start, len));
}

Value concat(const std::vector<Value>& args) {
    std::string result;
    for (const auto& arg : args) {
        result += arg.toString();
    }
    return Value(result);
}

Value split(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::STRING || args[1].type != Type::STRING) {
        throw std::runtime_error("split requires string and delimiter");
    }
    std::string str = *args[0].str;
    const std::string& delim = *args[1].str;

    std::vector<Value> result;
    size_t pos = 0;
    std::string token;
    while ((pos = str.find(delim)) != std::string::npos) {
        token = str.substr(0, pos);
        result.emplace_back(token);
        str.erase(0, pos + delim.length());
    }
    result.emplace_back(str);
    return Value(result);
}

Value toString(const std::vector<Value>& args) {
    if (args.size() != 1) throw std::runtime_error("toString requires 1 argument");
    return Value(args[0].toString());
}

Value parseInt(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("parseInt requires string argument");
    }
    return Value(std::stoi(*args[0].str));
}

Value parseDouble(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("parseDouble requires string argument");
    }
    return Value(std::stod(*args[0].str));
}

// =========================
//  List Operations
// =========================

Value listGet(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::LIST || args[1].type != Type::INT32) {
        throw std::runtime_error("listGet requires list and integer index");
    }
    const auto& list = *args[0].list;
    int32_t index = args[1].i32;
    if (index < 0 || (size_t)index >= list.size()) {
        throw std::runtime_error("Index out of bounds");
    }
    return list[index];
}

Value listSet(const std::vector<Value>& args) {
    if (args.size() != 3 || args[0].type != Type::LIST || args[1].type != Type::INT32) {
        throw std::runtime_error("listSet requires list, integer index, and value");
    }
    auto& list = *args[0].list;
    int32_t index = args[1].i32;
    if (index < 0 || (size_t)index >= list.size()) {
        throw std::runtime_error("Index out of bounds");
    }
    list[index] = args[2];
    return Value(0);
}

Value listAppend(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::LIST) {
        throw std::runtime_error("listAppend requires list and value");
    }
    args[0].list->push_back(args[1]);
    return Value(0);
}

Value listInsert(const std::vector<Value>& args) {
    if (args.size() != 3 || args[0].type != Type::LIST || args[1].type != Type::INT32) {
        throw std::runtime_error("listInsert requires list, integer index, and value");
    }
    auto& list = *args[0].list;
    int32_t index = args[1].i32;
    if (index < 0 || (size_t)index > list.size()) {
        throw std::runtime_error("Index out of bounds");
    }
    list.insert(list.begin() + index, args[2]);
    return Value(0);
}

Value listRemove(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::LIST || args[1].type != Type::INT32) {
        throw std::runtime_error("listRemove requires list and integer index");
    }
    auto& list = *args[0].list;
    int32_t index = args[1].i32;
    if (index < 0 || (size_t)index >= list.size()) {
        throw std::runtime_error("Index out of bounds");
    }
    list.erase(list.begin() + index);
    return Value(0);
}

Value listFind(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::LIST) {
        throw std::runtime_error("listFind requires list and value");
    }
    const auto& list = *args[0].list;
    const auto& value = args[1];
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].toString() == value.toString()) { // Simple comparison
            return Value((int32_t)i);
        }
    }
    return Value((int32_t)-1);
}

Value listSort(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::LIST) {
        throw std::runtime_error("listSort requires list argument");
    }
    auto& list = *args[0].list;
    std::sort(list.begin(), list.end(), [](const Value& a, const Value& b) {
        return a.toString() < b.toString();
    });
    return Value(0);
}

// =========================
//  Hash Operations
// =========================

Value hashString(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("hashString requires string argument");
    }
    const std::string& str = *args[0].str;
    std::hash<std::string> hasher;
    return Value((uint64_t)hasher(str));
}

Value sha256(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("sha256 requires string argument");
    }
    const std::string& str = *args[0].str;
    return Value(Crypto::sha256(str));
}

// =========================
//  Crypto Operations
// =========================

Value aesEncrypt(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::STRING || args[1].type != Type::STRING) {
        throw std::runtime_error("aesEncrypt requires plaintext and key strings");
    }
    const std::string& plaintext = *args[0].str;
    const std::string& key = *args[1].str;
    return Value(Crypto::aesEncrypt(plaintext, key));
}

Value aesDecrypt(const std::vector<Value>& args) {
    if (args.size() != 2 || args[0].type != Type::STRING || args[1].type != Type::STRING) {
        throw std::runtime_error("aesDecrypt requires ciphertext and key strings");
    }
    const std::string& ciphertext = *args[0].str;
    const std::string& key = *args[1].str;
    return Value(Crypto::aesDecrypt(ciphertext, key));
}

// =========================
//  Filesystem Operations
// =========================

Value fileExists(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("fileExists requires string filename");
    }
    return Value(fs::exists(*args[0].str) ? 1 : 0);
}

Value listDir(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("listDir requires string directory path");
    }
    std::vector<Value> result;
    try {
        for (const auto& entry : fs::directory_iterator(*args[0].str)) {
            result.emplace_back(entry.path().string());
        }
    } catch (...) {
        // Directory doesn't exist or can't be read
    }
    return Value(result);
}

Value createDir(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("createDir requires string directory path");
    }
    try {
        fs::create_directory(*args[0].str);
    } catch (...) {
        // Directory creation failed
    }
    return Value(0);
}

Value deleteFile(const std::vector<Value>& args) {
    if (args.size() != 1 || args[0].type != Type::STRING) {
        throw std::runtime_error("deleteFile requires string filename");
    }
    try {
        return Value(fs::remove(*args[0].str) ? 1 : 0);
    } catch (...) {
        return Value(0);
    }
}

// =========================
//  Function Registry
// =========================

using BuiltinFunction = Value(*)(const std::vector<Value>&);

std::unordered_map<std::string, BuiltinFunction> builtinFunctions = {
    // IO
    {"print", print},
    {"println", println},
    {"readln", readln},
    {"readFile", readFile},
    {"writeFile", writeFile},

    // Math
    {"abs", abs},
    {"sqrt", sqrt},
    {"pow", pow},
    {"sin", sin},
    {"cos", cos},
    {"tan", tan},
    {"random", random},

    // String
    {"length", length},
    {"substring", substring},
    {"concat", concat},
    {"split", split},
    {"toString", toString},
    {"parseInt", parseInt},
    {"parseDouble", parseDouble},

    // List
    {"listGet", listGet},
    {"listSet", listSet},
    {"listAppend", listAppend},
    {"listInsert", listInsert},
    {"listRemove", listRemove},
    {"listFind", listFind},
    {"listSort", listSort},

    // Hash/Crypto
    {"hashString", hashString},
    {"sha256", sha256},
    {"aesEncrypt", aesEncrypt},
    {"aesDecrypt", aesDecrypt},

    // Filesystem
    {"fileExists", fileExists},
    {"listDir", listDir},
    {"createDir", createDir},
    {"deleteFile", deleteFile},
};

Value callBuiltin(const std::string& name, const std::vector<Value>& args) {
    auto it = builtinFunctions.find(name);
    if (it == builtinFunctions.end()) {
        throw std::runtime_error("Unknown builtin function: " + name);
    }
    return it->second(args);
}

} // namespace TyneRuntime