cmake_minimum_required(VERSION 3.14)
project(xray VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xray
    src/main.cpp
    src/collector.cpp
    src/config_manager.cpp
    src/llm_client.cpp
    src/ui.cpp
    include/xray/version.hpp
    include/xray/types.hpp
    include/xray/collector.hpp
    include/xray/smart_analyzer.hpp
    include/xray/llm_client.hpp
    include/xray/ui.hpp
    include/xray/config_manager.hpp
)

target_include_directories(xray PRIVATE include)

target_compile_definitions(xray PRIVATE _UNICODE UNICODE)

# Windows subsystem (console app)
set_target_properties(xray PROPERTIES
    WIN32_EXECUTABLE FALSE
    OUTPUT_NAME "xray"
)
