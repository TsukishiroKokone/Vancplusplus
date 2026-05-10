# SPDX-FileCopyrightText: 2026 TsukishiroKokone
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(VanBot VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 依赖查找 ──────────────────────────────────────────────
find_package(ftxui REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(httplib REQUIRED)
find_package(ixwebsocket REQUIRED)
find_package(unofficial-sqlite3 REQUIRED)

# ── 主可执行文件 ──────────────────────────────────────────
add_executable(vanbot
    src/main.cpp
    src/vanbot/storage.cpp
    src/vanbot/lexicon_engine.cpp
    src/vanbot/variable_engine.cpp
    src/vanbot/http_client.cpp
    src/vanbot/onebot_adapter.cpp
    src/vanbot/bot.cpp
    src/tui/app.cpp
)

target_link_libraries(vanbot PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    httplib::httplib
    ixwebsocket::ixwebsocket
    unofficial::sqlite3::sqlite3
)

# Windows 特定配置
if(WIN32)
    target_compile_definitions(vanbot PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(vanbot PRIVATE ws2_32 crypt32 bcrypt)
endif()

# Linux 特定链接
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(vanbot PRIVATE Threads::Threads)
endif()

target_include_directories(vanbot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── 编译选项 ──────────────────────────────────────────────
if(MSVC)
    target_compile_options(vanbot PRIVATE /W4 /utf-8)
else()
    target_compile_options(vanbot PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── MSVC 运行库静态链接：生成的 exe 不依赖 VC++ 运行库 DLL ───────
if(MSVC)
    set_property(TARGET vanbot PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
