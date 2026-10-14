cmake_minimum_required(VERSION 3.25)

project(Flowstate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)

qt_add_executable(flowstate
    src/Main.cpp
    resources.qrc
)

target_link_libraries(flowstate PRIVATE Qt6::Quick)

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)
pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

# wlroots 0.20 exposes five C99 [static N] parameter annotations that C++
# cannot parse. The equivalent [N] declarations have the same C ABI.
list(GET WLROOTS_INCLUDE_DIRS 0 WLROOTS_PUBLIC_INCLUDE)
foreach(header IN ITEMS render/color.h types/wlr_scene.h)
    file(READ "${WLROOTS_PUBLIC_INCLUDE}/wlr/${header}" header_text)
    string(REGEX REPLACE "\\[static ([0-9]+)\\]" "[\\1]" header_text "${header_text}")
    get_filename_component(header_directory "${CMAKE_CURRENT_BINARY_DIR}/wlr-cxx/wlr/${header}" DIRECTORY)
    file(MAKE_DIRECTORY "${header_directory}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/wlr-cxx/wlr/${header}" "${header_text}")
endforeach()

add_executable(flowstate-nested src/Nested.cpp)
target_compile_definitions(flowstate-nested PRIVATE WLR_USE_UNSTABLE)
target_include_directories(flowstate-nested BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wlr-cxx")
target_link_libraries(flowstate-nested PRIVATE
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
    Vulkan::Vulkan
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME vulkan-backend COMMAND flowstate --probe-vulkan)
    set_tests_properties(vulkan-backend PROPERTIES
        TIMEOUT 15
        LABELS "requires-wayland-vulkan"
    )

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME nested-vulkan-client
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_smoke.py
            $<TARGET_FILE:flowstate-nested>
            $<TARGET_FILE:flowstate>
    )
    set_tests_properties(nested-vulkan-client PROPERTIES
        TIMEOUT 20
        LABELS "requires-wayland-vulkan"
    )
endif()
