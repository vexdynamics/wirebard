include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_executable(wirebard_tests
    alloc_test.cpp
    check_test.cpp
    cli_test.cpp
    contract_test.cpp
    fs_test.cpp
    json_test.cpp
    net_test.cpp
    partial_test.cpp
    peer_test.cpp
    project_test.cpp
    render_test.cpp
    subprocess_test.cpp
    vars_test.cpp
)
target_link_libraries(wirebard_tests PRIVATE wirebard_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(wirebard_tests)
