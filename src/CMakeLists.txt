# The application logic lives in a static library so that BOTH the `wirebard`
# executable and the test binary can link it — you can't link main.cpp into a
# test. (Node/Go equivalent: everything importable, main is a thin entry.)
#
# Zero third-party dependencies, by design.
add_library(wirebard_lib STATIC
    alloc.cpp
    apply.cpp
    check.cpp
    cli.cpp
    contract.cpp
    directives.cpp
    error.cpp
    fs.cpp
    json.cpp
    net.cpp
    partial.cpp
    peer.cpp
    project.cpp
    render.cpp
    subprocess.cpp
    vars.cpp
    commands/apply.cpp
    commands/build.cpp
    commands/check.cpp
    commands/list.cpp
    commands/network.cpp
    commands/peer.cpp
)
target_include_directories(wirebard_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(wirebard main.cpp)
target_link_libraries(wirebard PRIVATE wirebard_lib)

# Baked into `wirebard --version`. Local builds say "dev"; the release
# workflow passes the git tag: cmake --preset release-static -DWIREBARD_VERSION=v1.0.0
set(WIREBARD_VERSION "dev" CACHE STRING "version string for --version")
target_compile_definitions(wirebard PRIVATE WIREBARD_VERSION="${WIREBARD_VERSION}")
