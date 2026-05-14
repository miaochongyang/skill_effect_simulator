cmake_minimum_required(VERSION 4.2)
project(simulator_battle)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一产物输出层级：
# - 可执行文件输出到 <build>/bin[/<Config>]
# - 库文件输出到 <build>/lib[/<Config>]
# 兼容单配置（Ninja/Makefiles）与多配置（Visual Studio）生成器。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_CONFIGURATION_TYPES)
    foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${CONFIG} CONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${CONFIG})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${CONFIG})
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${CONFIG})
    endforeach()
endif()

add_executable(simulator_battle
        src/main.cpp
        src/ecs/monster_pool.cpp
        src/spatial/uniform_grid.cpp
        src/systems/wave_system.cpp
        src/systems/movement_system.cpp
        src/systems/projectile_weapon_system.cpp
        src/systems/game_director.cpp
        src/io/metrics_logger.cpp
        src/io/json_value.cpp
        src/io/config_loader.cpp
)

target_include_directories(simulator_battle PRIVATE src)
