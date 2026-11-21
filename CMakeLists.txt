cmake_minimum_required(VERSION 3.21)
project(guikit_native VERSION 1.0.0 LANGUAGES CXX)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(GUIKIT_NATIVE_BUILD_EXAMPLES "Build GuiKit Native minimal example application" ${PROJECT_IS_TOP_LEVEL})
option(GUIKIT_NATIVE_BUILD_TESTS "Build GuiKit Native unit tests" ${PROJECT_IS_TOP_LEVEL})
option(GUIKIT_NATIVE_INSTALL "Enable CMake install targets for guikit_native" ${PROJECT_IS_TOP_LEVEL})

if (GUIKIT_NATIVE_BUILD_TESTS)
    enable_testing()
endif()

if (NOT TARGET guikit::core)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/guikit/CMakeLists.txt")
        add_subdirectory(third_party/guikit EXCLUDE_FROM_ALL)
    else()
        find_package(guikit REQUIRED)
    endif()
endif()

set(GUIKIT_NATIVE_HAS_TEXT OFF)
if (TARGET guikit::text)
    set(GUIKIT_NATIVE_HAS_TEXT ON)
else()
    find_package(guikit_text QUIET)
    if (TARGET guikit::text)
        set(GUIKIT_NATIVE_HAS_TEXT ON)
    endif()
endif()

add_library(guikit_native STATIC
    src/guikit/native/gl_es3_canvas.cpp
    src/guikit/native/win32_platform_window.cpp
    src/guikit/native/x11_platform_window.cpp
    src/guikit/native/native_backend.cpp
)
add_library(guikit::native ALIAS guikit_native)
set_target_properties(guikit_native PROPERTIES EXPORT_NAME native)

target_compile_features(guikit_native PUBLIC cxx_std_20)
target_include_directories(guikit_native PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(guikit_native PUBLIC GUIKIT_NATIVE_HAS_ES3=1)
if (WIN32)
    target_compile_definitions(guikit_native PUBLIC GUIKIT_NATIVE_HAS_WIN32=1)
    target_link_libraries(guikit_native PRIVATE user32 gdi32 opengl32 shell32)
elseif (UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_compile_definitions(guikit_native PUBLIC GUIKIT_NATIVE_HAS_X11=1)
endif()
target_link_libraries(guikit_native PUBLIC guikit::core)
if (GUIKIT_NATIVE_HAS_TEXT AND TARGET guikit::text)
    target_link_libraries(guikit_native PUBLIC guikit::text)
endif()

if (COMMAND keel_enforce_guidelines)
    keel_enforce_guidelines(guikit_native
        HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
        STANDALONE_HEADERS ${GUIKIT_NATIVE_BUILD_TESTS}
        TEST_NAME guikit_native_header_purity_scan
    )
endif()

if (GUIKIT_NATIVE_BUILD_EXAMPLES AND NOT EMSCRIPTEN)
    add_executable(guikit_native_minimal
        examples/minimal/main.cpp
    )
    target_link_libraries(guikit_native_minimal PRIVATE guikit::native)
    if (COMMAND guikit_enforce_guidelines)
        guikit_enforce_guidelines(guikit_native_minimal)
    endif()
endif()

if (GUIKIT_NATIVE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS guikit_native
        EXPORT guikitNativeTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/guikit"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(EXPORT guikitNativeTargets
        FILE guikitNativeTargets.cmake
        NAMESPACE guikit::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_native"
    )
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/guikit_nativeConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_nativeConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_native"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_nativeConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_nativeConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_nativeConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_native"
    )
endif()

if (GUIKIT_NATIVE_BUILD_TESTS)
    add_executable(test_native
        tests/test_native.cpp
    )
    target_link_libraries(test_native PRIVATE guikit::native)
    if (COMMAND guikit_enforce_guidelines)
        guikit_enforce_guidelines(test_native)
    endif()
    add_test(NAME native COMMAND test_native)
endif()
