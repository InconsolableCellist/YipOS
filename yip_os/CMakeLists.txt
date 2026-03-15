cmake_minimum_required(VERSION 3.15)
project(yip_os VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Thirdparty options
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
if(UNIX)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
endif()

# GLFW via FetchContent
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# ImGui (copied from StayPutVR)
add_subdirectory(thirdparty/imgui)

# GLAD
add_library(glad STATIC thirdparty/glad/src/glad.c)
target_include_directories(glad PUBLIC thirdparty/glad/include)

# inih
add_library(inih STATIC
    thirdparty/inih/ini.c
    thirdparty/inih/INIReader.cpp
)
target_include_directories(inih PUBLIC thirdparty/inih)
# INIReader.cpp includes "../ini.h" — fix with this include path
target_include_directories(inih PRIVATE thirdparty/inih)

# Collect sources
set(YIPOS_SOURCES
    src/main.cpp
    src/core/Logger.cpp
    src/core/Config.cpp
    src/app/ScreenBuffer.cpp
    src/app/PDADisplay.cpp
    src/app/PDAController.cpp
    src/screens/Screen.cpp
    src/screens/HomeScreen.cpp
    src/screens/StatsScreen.cpp
    src/screens/NetScreen.cpp
    src/screens/HeartScreen.cpp
    src/screens/StayScreen.cpp
    src/screens/CalibrateScreen.cpp
    src/net/OSCManager.cpp
    src/net/NetTracker.cpp
    src/ui/UIManager.cpp
)

# Platform-specific sources
if(UNIX)
    list(APPEND YIPOS_SOURCES src/platform/SystemStats_linux.cpp)
elseif(WIN32)
    list(APPEND YIPOS_SOURCES src/platform/SystemStats_windows.cpp)
endif()

add_executable(yip_os ${YIPOS_SOURCES})

target_include_directories(yip_os PRIVATE
    src
    thirdparty
    thirdparty/glad/include
)

find_package(OpenGL REQUIRED)

target_link_libraries(yip_os PRIVATE
    imgui
    glad
    glfw
    inih
    OpenGL::GL
)

if(UNIX)
    target_link_libraries(yip_os PRIVATE pthread)
elseif(WIN32)
    target_link_libraries(yip_os PRIVATE ws2_32)
endif()
