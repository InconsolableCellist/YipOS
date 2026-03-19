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

# whisper.cpp via FetchContent
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.7.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(whisper)

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

# SQLite3 (amalgamation)
add_library(sqlite3 STATIC thirdparty/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC thirdparty/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
)

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
    src/screens/ConfScreen.cpp
    src/net/OSCManager.cpp
    src/net/OSCQueryServer.cpp
    src/net/NetTracker.cpp
    src/net/VRCXData.cpp
    src/screens/VRCXScreen.cpp
    src/screens/VRCXWorldsScreen.cpp
    src/screens/VRCXWorldDetailScreen.cpp
    src/screens/VRCXFeedScreen.cpp
    src/screens/VRCXFeedDetailScreen.cpp
    src/screens/VRCXFriendDetailScreen.cpp
    src/screens/CCScreen.cpp
    src/screens/CCConfScreen.cpp
    src/screens/AVTRScreen.cpp
    src/screens/AVTRChangeScreen.cpp
    src/screens/AVTRDetailScreen.cpp
    src/screens/AVTRCtrlScreen.cpp
    src/screens/VRCXNotifScreen.cpp
    src/screens/LockScreen.cpp
    src/screens/BFIScreen.cpp
    src/screens/BFIParamScreen.cpp
    src/net/ChatClient.cpp
    src/net/VRCAvatarData.cpp
    src/screens/ChatScreen.cpp
    src/screens/ChatDetailScreen.cpp
    src/screens/IMGScreen.cpp
    src/screens/TEXTScreen.cpp
    src/screens/MediaScreen.cpp
    src/img/VQEncoder.cpp
    src/audio/AudioCapture.cpp
    src/audio/WhisperWorker.cpp
    src/ui/UIManager.cpp
)

# Platform-specific sources
if(UNIX)
    list(APPEND YIPOS_SOURCES
        src/platform/SystemStats_linux.cpp
        src/audio/AudioCapture_linux.cpp
        src/media/MediaController_linux.cpp
    )
elseif(WIN32)
    list(APPEND YIPOS_SOURCES
        src/platform/SystemStats_windows.cpp
        src/audio/AudioCapture_windows.cpp
        src/media/MediaController_windows.cpp
    )
endif()

add_executable(yip_os ${YIPOS_SOURCES})

target_include_directories(yip_os PRIVATE
    src
    thirdparty
    thirdparty/glad/include
)

find_package(OpenGL REQUIRED)

if(WIN32)
    # Build curl from source on Windows (no system package)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_7_1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(curl)
else()
    find_package(CURL REQUIRED)
endif()

target_link_libraries(yip_os PRIVATE
    imgui
    glad
    glfw
    inih
    sqlite3
    whisper
    OpenGL::GL
    CURL::libcurl
)

if(UNIX)
    target_link_libraries(yip_os PRIVATE pthread dl)
elseif(WIN32)
    target_link_libraries(yip_os PRIVATE ws2_32 iphlpapi windowsapp)
endif()
