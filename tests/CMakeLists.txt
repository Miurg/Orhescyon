include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

add_executable(orhescyon-tests
        GMContextTests.cpp
        StablePoolTests.cpp
        ActiveEntitySetTests.cpp
        ComponentArrayTests.cpp
        ComponentManagerTests.cpp
        GMEntitiesTests.cpp
        GMComponentTests.cpp
        GMSystemTests.cpp
 "ActiveEntitySetTests.cpp" "ComponentArrayTests.cpp" "ComponentManagerTests.cpp" "GMEntitiesTests.cpp" "GMComponentTests.cpp" "GMSystemTests.cpp" "GMContextTests.cpp" )

target_link_libraries(orhescyon-tests
    PRIVATE
        Orhescyon
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(orhescyon-tests)