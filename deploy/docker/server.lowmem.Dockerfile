# syntax=docker/dockerfile:1.7
# Build profile for resource-constrained Linux hosts. The vcpkg cache survives
# failed builds and source-only changes, while compilation stays single-threaded.
FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git ninja-build pkg-config autoconf automake \
    libtool libtirpc-dev curl zip unzip tar && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_MAX_CONCURRENCY=1 \
    CMAKE_BUILD_PARALLEL_LEVEL=1 \
    VCPKG_BINARY_SOURCES="clear;files,/root/.cache/vcpkg/archives,readwrite"
WORKDIR /src
COPY . .
# Cache binary packages outside this Dockerfile layer. A later CMake failure
# can therefore reuse vcpkg dependencies instead of compiling them again.
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    cmake --preset linux-server-release \
    && cmake --build --preset linux-server-release

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libstdc++6 python3 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/linux-server-release/bin/ /app/bin/
WORKDIR /app
