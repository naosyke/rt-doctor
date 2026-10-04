# Build and test environment. rtdoctor itself has no runtime dependencies.
FROM ubuntu:24.04 AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build libgtest-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace

FROM base AS build
COPY . /workspace
RUN cmake -S . -B build -G Ninja -DRTDOCTOR_STATIC=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure
