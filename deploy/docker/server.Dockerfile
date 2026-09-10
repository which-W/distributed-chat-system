FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git ninja-build pkg-config autoconf automake \
    libtool libtirpc-dev curl zip unzip tar && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg
WORKDIR /src
COPY . .
RUN cmake --preset linux-server-release && cmake --build --preset linux-server-release

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libstdc++6 python3 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/linux-server-release/bin/ /app/bin/
WORKDIR /app
