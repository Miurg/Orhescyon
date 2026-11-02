include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

if(ORHESCYON_DISABLE_EXCEPTIONS)
    foreach(orhescyon_test_target IN ITEMS gtest gtest_main gmock gmock_main)
        if(TARGET ${orhescyon_test_target})
            get_target_property(orhescyon_test_flags ${orhescyon_test_target} COMPILE_FLAGS)
            string(REPLACE "-EHsc" "" orhescyon_test_flags "${orhescyon_test_flags}")
            string(REPLACE "-D_HAS_EXCEPTIONS=1" "" orhescyon_test_flags "${orhescyon_test_flags}")
            string(REPLACE "-fexceptions" "" orhescyon_test_flags "${orhescyon_test_flags}")
            set_target_properties(${orhescyon_test_target} PROPERTIES COMPILE_FLAGS "${orhescyon_test_flags}")
            target_link_libraries(${orhescyon_test_target} PRIVATE Orhescyon)
        endif()
    endforeach()
endif()

add_executable(orhescyon-tests
        GMContextTests.cpp
        StablePoolTests.cpp
        SlotBitmapTests.cpp
        ComponentViewTests.cpp
        ComponentColumnTests.cpp
        SparseComponentStorageTests.cpp
        ComponentManagerTests.cpp
        GMEntitiesTests.cpp
        GMDeferredTests.cpp
        EntityManagerTests.cpp
        GMComponentTests.cpp
        GMSystemTests.cpp
        SystemSequenceTests.cpp
        GMMultiManagerTests.cpp
        GMJobSystemTests.cpp
        Jobs/JobPoolTests.cpp
        Jobs/DefaultJobSystemTests.cpp
)

target_link_libraries(orhescyon-tests
    PRIVATE
        Orhescyon
        GTest::gtest_main
)

# Capture dev-shell env at configure time; tests re-export it before spawning cmake.
file(WRITE "${CMAKE_BINARY_DIR}/orhescyon_test_env.txt"
"INCLUDE=$ENV{INCLUDE}
LIB=$ENV{LIB}
LIBPATH=$ENV{LIBPATH}
PATH=$ENV{PATH}
")

target_compile_definitions(orhescyon-tests PRIVATE
    ORHESCYON_CMAKE_COMMAND="${CMAKE_COMMAND}"
    ORHESCYON_BUILD_DIR="${CMAKE_BINARY_DIR}"
    "ORHESCYON_CMAKE_CONFIG=\"$<IF:$<BOOL:${CMAKE_CONFIGURATION_TYPES}>,$<CONFIG>,>\""
    ORHESCYON_TEST_ENV_FILE="${CMAKE_BINARY_DIR}/orhescyon_test_env.txt"
)

# Compile-check target: built on demand by its gtest; build success = test passes.
function(orhescyon_add_compile_check name source)
    add_executable(${name} EXCLUDE_FROM_ALL ${source})
    target_link_libraries(${name} PRIVATE Orhescyon)
endfunction()

orhescyon_add_compile_check(JobPoolCompile_LvalueCallable             Jobs/Compile/LvalueCallable.cpp)
orhescyon_add_compile_check(JobPoolCompile_MoveOnlyCallable           Jobs/Compile/MoveOnlyCallable.cpp)
orhescyon_add_compile_check(JobPoolCompile_RandomAccessIteratorRange  Jobs/Compile/RandomAccessIteratorRange.cpp)
orhescyon_add_compile_check(JobPoolCompile_ForwardIteratorRange       Jobs/Compile/ForwardIteratorRange.cpp)
orhescyon_add_compile_check(JobPoolCompile_SubmitForwardsArgs         Jobs/Compile/SubmitForwardsArgs.cpp)
orhescyon_add_compile_check(JobPoolCompile_SubmitMoveOnlyCallable     Jobs/Compile/SubmitMoveOnlyCallable.cpp)
orhescyon_add_compile_check(JobPoolCompile_SubmitMoveOnlyArg          Jobs/Compile/SubmitMoveOnlyArg.cpp)

include(GoogleTest)
gtest_discover_tests(orhescyon-tests)
