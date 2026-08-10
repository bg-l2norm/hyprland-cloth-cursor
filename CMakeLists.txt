cmake_minimum_required(VERSION 3.27)
project(hyprland-cloth-cursor VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOTH_CURSOR_BUILD_PLUGIN "Build the Hyprland plugin" ON)
option(CLOTH_CURSOR_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(clothcursor_physics STATIC src/physics.cpp)
target_include_directories(clothcursor_physics PUBLIC src)
target_compile_options(clothcursor_physics PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(clothcursor_physics PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CLOTH_CURSOR_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

enable_testing()
add_executable(physics_test tests/physics_test.cpp)
target_link_libraries(physics_test PRIVATE clothcursor_physics)
target_compile_options(physics_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME physics COMMAND physics_test)

if(CLOTH_CURSOR_BUILD_PLUGIN)
    include(cmake/VerifyHyprland.cmake)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::HYPRLAND)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    unset(CLOTH_CURSOR_CURSOR_RENDER_HAS_SCREENCOPY CACHE)
    check_cxx_source_compiles([=[
        #include <hyprland/src/pointer/PointerManager.hpp>
        #include <optional>
        #include <type_traits>

        using CursorRenderWithScreencopy = void (Pointer::CPointerManager::*)(
            PHLMONITOR, const Time::steady_tp&, CRegion&, std::optional<Vector2D>, bool, bool);

        static_assert(std::is_same_v<
            decltype(static_cast<CursorRenderWithScreencopy>(&Pointer::CPointerManager::renderSoftwareCursorsFor)),
            CursorRenderWithScreencopy>);

        int main() { return 0; }
    ]=] CLOTH_CURSOR_CURSOR_RENDER_HAS_SCREENCOPY)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)

    add_library(clothcursor SHARED src/plugin.cpp src/runtime.cpp src/cursor_pass.cpp)
    target_link_libraries(clothcursor PRIVATE clothcursor_physics PkgConfig::HYPRLAND)
    target_compile_options(clothcursor PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-gnu-unique)
    if(CLOTH_CURSOR_CURSOR_RENDER_HAS_SCREENCOPY)
        target_compile_definitions(clothcursor PRIVATE CLOTH_CURSOR_CURSOR_RENDER_HAS_SCREENCOPY=1)
    endif()
    set_target_properties(clothcursor PROPERTIES
        OUTPUT_NAME clothcursor
    )
endif()
