# Tyne Compiler and Runtime

Tyne is a compiled programming language with its own binary format and cross-platform runtime.

## Features

- **TOML Configuration**: Configure compilation settings via TOML files
- **Custom Binary Format**: Platform-specific binary format with memory mapping
- **Cross-Platform**: Supports x86_64, ARM 32/64-bit, and RISC-V 64-bit
- **Memory-Mapped Loading**: Efficient loading using mmap/VirtualAlloc
- **Standard Library**: Comprehensive stdlib with IO, math, collections, etc.

## Quick Start

### Automated Build and Install

The easiest way to build and install Tyne is using the automated build toolchain:

#### Windows
```cmd
build.bat
```

#### Linux/macOS
```bash
./build.sh
```

This will:
- ✅ Check for required dependencies (CMake, C++ compiler)
- ✅ Download and setup toml++ library
- ✅ Configure and build the compiler
- ✅ Install to a local directory
- ✅ Update your PATH automatically
- ✅ Test the installation

### Manual Build

Use one of these paths if you want explicit control:

1. CMake workflow:

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

2. Direct g++ (Windows MinGW/MSYS2):

```bash
# Open a shell where g++ is available (MinGW or MSYS2)
cd c:\Users\noll\Repos\Tyne-1\build
"C:\mingw64\bin\g++.exe" -std=c++17 -O2 -Wall -Wextra ..\src\tyne.cpp ..\src\lexer.cpp ..\src\parser.cpp ..\src\son_ir.cpp ..\src\ir_builder.cpp -o tyne.exe
```

3. Automated Windows helper:

```cmd
build.bat
```

4. Automated Unix helper:

```bash
./build.sh
```

### 2. Compile a Program

```bash
# Create a config file (see config.toml)
# Create your source file (see sample.tyne)

# Compile
./tyne config.toml
```

### 3. Run the Binary

```bash
# Load and execute
./tyne program.tynebin
```

## Configuration

The `config.toml` file controls compilation:

```toml
[output]
file = "program.tynebin"

[target]
arch = "x86_64"  # or arm32, arm64, riscv64

[optimization]
enabled = true
level = 2

[includes]
# paths = ["./lib"]

[debug]
symbols = false
```

## Binary Format

Tyne uses a custom binary format with:
- **Header**: Magic number, version, architecture, sizes
- **Code Section**: Compiled IR bytecode
- **Data Section**: Constants and static data
- **Symbol Table**: Function and variable symbols
- **String Table**: Interned strings
- **Relocation Table**: Position-independent code support

## Memory Mapping

The loader uses:
- **Linux**: `mmap()` for efficient loading
- **Windows**: `VirtualAlloc()` and `MapViewOfFile()`
- **Benefits**: Fast loading, shared memory, demand paging

## Architecture Support

### Building for Different Architectures

```bash
# Install cross-compilers
sudo apt install gcc-arm-linux-gnueabihf gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu

# Build runtime for all architectures
make build_all_runtimes
```

### Testing Cross-Compiled Binaries

```bash
# ARM 64-bit with QEMU
qemu-aarch64-static ./runtime_arm64

# RISC-V 64-bit with QEMU
qemu-riscv64-static ./runtime_riscv64
```

## Standard Library

Located in `lib/` directory:

- `io.tyne` - Input/output operations
- `math.tyne` - Mathematical functions
- `string.tyne` - String manipulation
- `list.tyne` - List operations
- `hash.tyne` - Hashing functions
- `crypto.tyne` - Cryptography
- `filesystem.tyne` - File system operations
- `collections.tyne` - Data structures
- `networking.tyne` - Network communication
- `time.tyne` - Date/time operations
- `stdlib.tyne` - Main stdlib (imports all modules)

## Language Features

### Complex List Syntax
```tyne
# Mixed types
list mixed = [int32 42, string "hello", list [1, 2, 3]];

# Type repetition
list repeated = [int32:string 3:"abc"];  # ["abc", "abc", "abc"]
```

### Inline Constructor Syntax
```tyne
class MyClass (int32 value, float rate) {
    int32 x = value;
    float y = rate * 2.0;
}
```

### Entry Point
```tyne
entry {
    println("Hello, World!");
    return 0;
}
```

### Imports
```tyne
import "io";
import "math";
```

## Development

### Build System

Tyne uses a Python-based build toolchain that automatically handles:

- **Dependency Detection**: Checks for CMake, C++ compilers, and libraries
- **Cross-Platform**: Works on Windows, Linux, and macOS
- **Library Management**: Downloads and configures toml++ automatically
- **Installation**: Installs to appropriate system locations
- **PATH Management**: Updates environment variables automatically

#### Build Scripts

- **`build.bat`** - Windows batch script
- **`build.sh`** - Unix shell script
- **`build_toolchain.py`** - Main Python build script

#### Usage Examples

```bash
# Build and install to default location
./build.sh

# Build to custom install directory
./build.sh --install /opt/tyne

# Clean build directory
./build.sh --clean

# Build without updating PATH
./build.sh --no-path-update
```

### Dependencies

- **Python 3.6+**: Required for the build toolchain
- **CMake 3.10+**: Build system configuration
- **C++ Compiler**: GCC, Clang, or MSVC with C++17 support
- **toml++**: Automatically downloaded by the build script

### Building from Source

```bash
# Clone and setup
git clone <repository>
cd tyne
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make

# Test
make runtime_test
./runtime_test
```

## File Structure

```
tyne/
├── src/
│   ├── tyne.cpp          # Main compiler/loader
│   ├── lexer.hpp/cpp     # Lexical analysis
│   ├── parser.hpp/cpp    # AST parsing
│   ├── son_ir.hpp/cpp    # IR representation
│   ├── ir_builder.hpp/cpp # AST to IR conversion
│   └── runtime.cpp       # Runtime library
├── lib/                  # Standard library
│   ├── *.tyne           # Stdlib modules
│   └── README.md        # Stdlib documentation
├── config.toml          # Sample configuration
├── sample.tyne          # Sample program
├── CMakeLists.txt       # Build configuration
└── build_toolchain.py
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests
5. Submit a pull request

## License

Mozilla Public License 2.0
