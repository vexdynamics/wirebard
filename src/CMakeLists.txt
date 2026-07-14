# The application logic lives in a static library so that BOTH the `wirebard`
# executable and the test binary can link it — you can't link main.cpp into a
# test. (Node/Go equivalent: everything importable, main is a thin entry.)
#
# Zero third-party dependencies, by design (inherited from haladin).
add_library(wirebard_lib STATIC
    alloc.cpp
    cli.cpp
    directives.cpp
    error.cpp
    fs.cpp
    net.cpp
    partial.cpp
    peer.cpp
    project.cpp
    subprocess.cpp
    vars.cpp
    commands/stubs.cpp
)
target_include_directories(wirebard_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(wirebard main.cpp)
target_link_libraries(wirebard PRIVATE wirebard_lib)
