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
    set(cxx_header "${CMAKE_CURRENT_BINARY_DIR}/wlr-cxx/wlr/${header}")
    get_filename_component(header_directory "${cxx_header}" DIRECTORY)
    file(MAKE_DIRECTORY "${header_directory}")
    if(EXISTS "${cxx_header}")
        file(READ "${cxx_header}" existing_header_text)
    endif()
    if(NOT "${existing_header_text}" STREQUAL "${header_text}")
        file(WRITE "${cxx_header}" "${header_text}")
    endif()
    unset(existing_header_text)
endforeach()

add_library(flowstate-layout src/Layout.cpp)
target_include_directories(flowstate-layout PUBLIC src)

add_executable(flowstate-nested src/Nested.cpp)
target_compile_definitions(flowstate-nested PRIVATE WLR_USE_UNSTABLE)
target_include_directories(flowstate-nested BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wlr-cxx")
target_link_libraries(flowstate-nested PRIVATE
    flowstate-layout
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
    Vulkan::Vulkan
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(layout-test tests/layout_test.cpp)
    set_target_properties(layout-test PROPERTIES AUTOMOC ON)
    target_link_libraries(layout-test PRIVATE flowstate-layout Qt6::Test)
    add_test(NAME layout COMMAND layout-test)
    set_tests_properties(layout PROPERTIES LABELS "unit")

    add_test(NAME vulkan-backend COMMAND flowstate --probe-vulkan)
    set_tests_properties(vulkan-backend PROPERTIES
        TIMEOUT 15
        LABELS "graphical;requires-wayland-vulkan"
    )

    add_test(NAME qml-vulkan-pixels COMMAND flowstate --probe-visual)
    set_tests_properties(qml-vulkan-pixels PROPERTIES
        TIMEOUT 15
        LABELS "graphical;requires-wayland-vulkan"
    )

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME invalid-compositor-options
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_arguments.py
            $<TARGET_FILE:flowstate-nested>
    )
    set_tests_properties(invalid-compositor-options PROPERTIES
        TIMEOUT 5 LABELS "unit"
    )
    add_test(NAME nested-vulkan-client
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_smoke.py
            $<TARGET_FILE:flowstate-nested>
            $<TARGET_FILE:flowstate>
    )
    set_tests_properties(nested-vulkan-client PROPERTIES
        TIMEOUT 20
        LABELS "integration;requires-wayland-vulkan"
    )

    find_program(FOOT_EXECUTABLE foot REQUIRED)
    add_test(NAME three-wayland-windows
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/three_windows.py
            $<TARGET_FILE:flowstate-nested>
            ${FOOT_EXECUTABLE}
    )
    set_tests_properties(three-wayland-windows PROPERTIES
        TIMEOUT 35
        LABELS "integration;requires-wayland-vulkan"
    )

    add_test(NAME rapid-resize-focus
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/three_windows.py
            $<TARGET_FILE:flowstate-nested>
            ${FOOT_EXECUTABLE} --stress
    )
    set_tests_properties(rapid-resize-focus PROPERTIES
        TIMEOUT 60 LABELS "stress;requires-wayland-vulkan"
    )

    add_test(NAME qml-and-wayland-windows
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/qml_composed.py
            $<TARGET_FILE:flowstate-nested>
            $<TARGET_FILE:flowstate>
            ${FOOT_EXECUTABLE}
    )
    set_tests_properties(qml-and-wayland-windows PROPERTIES
        TIMEOUT 35
        LABELS "integration;graphical;requires-wayland-vulkan"
    )

    add_test(NAME repeated-compositor-starts
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/repeated_starts.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_smoke.py
            $<TARGET_FILE:flowstate-nested>
            $<TARGET_FILE:flowstate>
    )
    set_tests_properties(repeated-compositor-starts PROPERTIES
        TIMEOUT 75
        LABELS "stress;requires-wayland-vulkan"
    )

    find_file(VULKAN_VALIDATION_LAYER VK_LAYER_KHRONOS_validation.json
        PATHS /usr/share/vulkan/explicit_layer.d /usr/local/share/vulkan/explicit_layer.d
        NO_DEFAULT_PATH)
    if(VULKAN_VALIDATION_LAYER)
        add_test(NAME vulkan-validation
            COMMAND ${CMAKE_COMMAND} -E env VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_smoke.py
                $<TARGET_FILE:flowstate-nested> $<TARGET_FILE:flowstate>
        )
        set_tests_properties(vulkan-validation PROPERTIES
            TIMEOUT 35 LABELS "stress;requires-wayland-vulkan"
        )
    endif()
endif()
