cmake_minimum_required(VERSION 3.16)
project(rover_server VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")

# ---- Find packages -----------------------------------------
find_package(PkgConfig REQUIRED)

# libgpiod
pkg_check_modules(GPIOD REQUIRED libgpiod)

# libjpeg
find_package(JPEG REQUIRED)

# OpenSSL (for WebSocket SHA1/Base64)
find_package(OpenSSL REQUIRED)

# Threads
find_package(Threads REQUIRED)

# ---- Sources ------------------------------------------------
set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/serial/TeensyBridge.cpp
    src/wifi/WifiServer.cpp
    src/webui/WebUiServer.cpp
    src/camera/CameraStream.cpp
    src/gpio/GpioController.cpp
    src/ota/TeensyOta.cpp
)

add_executable(rover_server ${SOURCES})

target_include_directories(rover_server PRIVATE
    src
    ${GPIOD_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
)

target_link_libraries(rover_server PRIVATE
    ${GPIOD_LIBRARIES}
    ${JPEG_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    stdc++fs   # std::filesystem on older GCC
)

target_compile_options(rover_server PRIVATE
    -Wall -Wextra -O2
    ${GPIOD_CFLAGS_OTHER}
)

# ---- Install -----------------------------------------------
install(TARGETS rover_server DESTINATION /usr/local/bin)
install(FILES scripts/rover.service
        DESTINATION /etc/systemd/system)
install(FILES scripts/rover.conf.example
        DESTINATION /etc/rover
        RENAME rover.conf
        OPTIONAL)
install(DIRECTORY sounds/
        DESTINATION /opt/rover/sounds
        OPTIONAL)

# ---- Summary -----------------------------------------------
message(STATUS "=== Rover Server Build Configuration ===")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install to : ${CMAKE_INSTALL_PREFIX}")
