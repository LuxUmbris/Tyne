cmake_minimum_required(VERSION 3.10)
project(Tyne)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find toml++ library (optional) and fallback to FetchContent if missing
find_package(tomlplusplus QUIET)
if(NOT tomlplusplus_FOUND)
    message(STATUS "tomlplusplus not found; using FetchContent fallback")
    include(FetchContent)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)

    # Ensure target is available even when find_package fails.
    if(NOT TARGET tomlplusplus::tomlplusplus)
        find_package(tomlplusplus REQUIRED PATHS ${tomlplusplus_SOURCE_DIR} NO_DEFAULT_PATH)
    endif()
endif()

# Main compiler executable
# Sources are currently in single compilation unit; adjust as needed
add_executable(tyne
    src/tyne.cpp
)

target_link_libraries(tyne PRIVATE tomlplusplus::tomlplusplus tyne_runtime)

# Runtime library
add_library(tyne_runtime STATIC src/runtime.cpp)
target_include_directories(tyne_runtime PUBLIC src)

# (no runtime test executable in this minimal project structure)

# Cross-compilation toolchains (same as before)
# ... (rest of the cross-compilation setup remains the same)