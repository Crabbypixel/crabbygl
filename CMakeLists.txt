cmake_minimum_required(VERSION 3.20)
project(CrabbyGL LANGUAGES C CXX)

# Match the .vcxproj (stdcpp23). Code only needs C++20 today; staying on 23 keeps
# both builds in the same mode so a C++23 feature can't compile on one and not the other.
# NOTE: <print>/std::println needs GCC >= 14.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # -std=c++23, not gnu++23: catch GNU-only code MSVC would reject

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(EXT ${CMAKE_CURRENT_SOURCE_DIR}/externals)

# ---------------------------------------------------------------------------
# GLFW
# Repo ships prebuilt Windows binaries only (libraries/). They can't serve Linux,
# and MinGW ABI vs. the bundled .a is a gamble, so build GLFW from source unless
# the system already has it. Emscripten will replace this whole block with
# -sUSE_GLFW=3 later.
# ---------------------------------------------------------------------------
option(CRABBY_SYSTEM_GLFW "Use an installed glfw3 instead of fetching it" OFF)
if(CRABBY_SYSTEM_GLFW)
    find_package(glfw3 3.3 REQUIRED)
else()
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)
endif()

# ---------------------------------------------------------------------------
# Vendored third-party code. Separate static libs so our warning flags don't
# drown in their noise and they aren't recompiled when engine flags change.
# ---------------------------------------------------------------------------
add_library(glad STATIC ${EXT}/glad/glad.c)
target_include_directories(glad PUBLIC ${EXT})
# glad's built-in loader references dlopen even when we use glfwGetProcAddress.
target_link_libraries(glad PRIVATE ${CMAKE_DL_LIBS})

add_library(imgui STATIC
    ${EXT}/imgui/imgui.cpp
    ${EXT}/imgui/imgui_demo.cpp
    ${EXT}/imgui/imgui_draw.cpp
    ${EXT}/imgui/imgui_tables.cpp
    ${EXT}/imgui/imgui_widgets.cpp
    ${EXT}/imgui/imgui_impl_glfw.cpp
    ${EXT}/imgui/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${EXT}/imgui ${EXT})
target_link_libraries(imgui PUBLIC glfw)

# GLM, stb: header-only. externals/glm/glm.cppm is a C++20 *module* the .vcxproj
# compiles for no reason; it is deliberately not built here.

# ---------------------------------------------------------------------------
# Engine
# GLOB + CONFIGURE_DEPENDS: one-author repo where files are added in the .vcxproj
# too; re-globbing at build time means a new .cpp can't silently go missing here.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CRABBY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(CrabbyGL ${CRABBY_SOURCES})
target_include_directories(CrabbyGL PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
# SYSTEM: silence warnings coming from glm/stb headers.
target_include_directories(CrabbyGL SYSTEM PRIVATE ${EXT} ${EXT}/imgui)

find_package(Threads REQUIRED)
target_link_libraries(CrabbyGL PRIVATE glad imgui glfw Threads::Threads)

if(MSVC)
    target_compile_options(CrabbyGL PRIVATE /W3 /permissive-)
else()
    target_compile_options(CrabbyGL PRIVATE -Wall -Wextra)
endif()

# Asset and save paths are cwd-relative ("assets/...", "saves/..."), so the game
# must run with the repo root as cwd. Don't copy assets into the build dir: saves/
# would then fork into two worlds depending on how it was launched.
set_target_properties(CrabbyGL PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_target(run
    COMMAND $<TARGET_FILE:CrabbyGL>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS CrabbyGL
    USES_TERMINAL)
