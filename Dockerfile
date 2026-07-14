# Build and test wirebard in a clean room — no toolchain on the host needed.
# Hand-written infrastructure, not generated; wirebard knows nothing about it.
#
#   docker build -t wirebard .            # compiles + runs the ctest suite
#   docker run --rm wirebard --help       # run the built binary

FROM ubuntu:24.04 AS build
# gcc-14 too: googletest's CMake probes for a C compiler.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-14 g++-14 cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-14 CXX=g++-14

WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY src/ src/
COPY tests/ tests/

# Release preset: the debug preset's ASan/LSan needs ptrace, which default
# docker seccomp blocks — sanitizers stay a host-side tool.
RUN cmake --preset release && cmake --build --preset release
RUN ctest --preset release

# Slim final stage: the binary plus wireguard-tools, so validation works here.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends wireguard-tools \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/release/src/wirebard /usr/local/bin/wirebard
ENTRYPOINT ["wirebard"]
