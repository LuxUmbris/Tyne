#include <iostream>
#include <vector>
#include <string>

// Simplified demonstration of Tyne runtime functionality
// This shows how the hello world program would work

// Simplified Value type (from runtime.cpp)
class Value {
public:
    enum Type { INT32, DOUBLE, STRING, LIST };

    Value(int32_t v) : type(INT32), int_val(v) {}
    Value(double v) : type(DOUBLE), double_val(v) {}
    Value(const std::string& v) : type(STRING), string_val(v) {}
    Value(const std::vector<Value>& v) : type(LIST), list_val(v) {}

    std::string toString() const {
        switch (type) {
            case INT32: return std::to_string(int_val);
            case DOUBLE: return std::to_string(double_val);
            case STRING: return string_val;
            case LIST: {
                std::string result = "[";
                for (size_t i = 0; i < list_val.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += list_val[i].toString();
                }
                result += "]";
                return result;
            }
        }
        return "";
    }

private:
    Type type;
    int32_t int_val;
    double double_val;
    std::string string_val;
    std::vector<Value> list_val;
};

// Simplified builtin functions
Value builtin_println(const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.toString() << std::endl;
    }
    return Value(0); // Return void-like value
}

Value builtin_print(const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.toString();
    }
    return Value(0);
}

// Simulate the hello world program execution
int main() {
    std::cout << "=== Tyne Hello World Demo ===" << std::endl;
    std::cout << "Simulating execution of hello_world.tyne" << std::endl;
    std::cout << std::endl;

    // Simulate: println("Hello, World!")
    builtin_println({Value("Hello, World!")});

    // Simulate: print("Welcome to Tyne programming language!")
    builtin_print({Value("Welcome to Tyne programming language!")});

    // Simulate: println("")
    builtin_println({Value("")});

    // Simulate: int32 answer = 42
    Value answer(42);

    // Simulate: print("The answer is: ")
    builtin_print({Value("The answer is: ")});

    // Simulate: println(answer)
    builtin_println({answer});

    std::cout << std::endl;
    std::cout << "=== Demo Complete ===" << std::endl;
    std::cout << "In a real Tyne environment, this would be compiled to:" << std::endl;
    std::cout << "- hello_world.tyne -> hello_world.tynebin (custom binary format)" << std::endl;
    std::cout << "- Memory-mapped loading with mmap/VirtualAlloc" << std::endl;
    std::cout << "- Cross-platform execution on x86_64, ARM, RISC-V" << std::endl;

    return 0;
}