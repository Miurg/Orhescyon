cmake_minimum_required(VERSION 3.21)
project(Orhescyon VERSION 0.1.0 LANGUAGES CXX)

add_library(Orhescyon INTERFACE) 

target_include_directories(Orhescyon INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(Orhescyon INTERFACE cxx_std_20)

if(PROJECT_IS_TOP_LEVEL)
    option(ORHESCYON_BUILD_TESTS "Build tests" ON)
else()
    option(ORHESCYON_BUILD_TESTS "Build tests" OFF)
endif()

if(ORHESCYON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS Orhescyon
    EXPORT OrhescyonTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT OrhescyonTargets
    FILE OrhescyonTargets.cmake
    NAMESPACE Orhescyon::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Orhescyon
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OrhescyonConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OrhescyonConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Orhescyon
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OrhescyonConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OrhescyonConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OrhescyonConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Orhescyon
)