# BUILDING

This project: **H2O C Library**
Version: **0.0.1**

## Local build

```bash
# one-shot build + install
./build.sh install
````

Or run the steps manually:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc || sysctl -n hw.ncpu || echo 4)"
sudo cmake --install .
```


## Install dependencies (from `cmake.libraries`)


### System packages (required)

```bash
sudo apt-get update && sudo apt-get install -y zlib1g-dev libssl-dev
```



### Development tooling (optional)

```bash
sudo apt-get update && sudo apt-get install -y python3 python3-venv python3-pip valgrind gdb perl autoconf automake libtool
```



### 3rd-party/libuv


### libuv

Clone & build:

```bash
git clone --depth 1 --branch v1.48.0 --single-branch "https://github.com/libuv/libuv.git" "libuv"
mkdir -p build/libuv && cd build/libuv
cmake ../../libuv -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF
make -j"$(nproc)" && sudo make install
cd ../.. && rm -rf libuv
```


### OpenSSL

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libssl-dev
```


### ZLIB

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y zlib1g-dev
```


### h2o

Clone & build:

```bash
git clone --depth 1 --branch v2.2.6 --single-branch "https://github.com/h2o/h2o.git" "h2o"
mkdir -p build/h2o && cd build/h2o
cmake ../../h2o -DCMAKE_INSTALL_PREFIX=/usr/local -DWITH_MRUBY=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
make -j"$(nproc)" && sudo make install
cd ../.. && rm -rf h2o
```


## Docker (optional)

```dockerfile
# syntax=docker/dockerfile:1
ARG UBUNTU_TAG=22.04
FROM ubuntu:${UBUNTU_TAG}

# --- Configurable (can be overridden with --build-arg) ---
ARG CMAKE_VERSION=3.26.4
ARG CMAKE_BASE_URL=https://github.com/Kitware/CMake/releases/download
ARG GITHUB_TOKEN

ENV DEBIAN_FRONTEND=noninteractive

# --- Base system setup --------------------------------------------------------
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    curl \
    wget \
    tar \
    unzip \
    zip \
    pkg-config \
    sudo \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Development tooling (optional)
RUN apt-get update && apt-get install -y \
    python3 \
    python3-venv \
    python3-pip \
    valgrind \
    gdb \
    perl \
    autoconf \
    automake \
    libtool \
 && rm -rf /var/lib/apt/lists/*

# --- Install CMake from official binaries (arch-aware) ------------------------
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
      x86_64) CMAKE_ARCH=linux-x86_64 ;; \
      aarch64) CMAKE_ARCH=linux-aarch64 ;; \
      *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;; \
    esac; \
    apt-get update && apt-get install -y wget tar && rm -rf /var/lib/apt/lists/*; \
    wget -q "${CMAKE_BASE_URL}/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.tar.gz" -O /tmp/cmake.tgz; \
    tar --strip-components=1 -xzf /tmp/cmake.tgz -C /usr/local; \
    rm -f /tmp/cmake.tgz

# --- Create a non-root 'dev' user with passwordless sudo ----------------------
RUN useradd --create-home --shell /bin/bash dev && \
    echo "dev ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    mkdir -p /workspace && chown dev:dev /workspace

USER dev
WORKDIR /workspace

# --- Optional Python venv for tools ------------------------------------------
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --upgrade pip
ENV PATH="/opt/venv/bin:${PATH}"

# --- Build & install libuv ---
RUN set -eux; \
  git clone --depth 1 --branch v1.48.0 --single-branch "https://github.com/libuv/libuv.git" "libuv" && \
  mkdir -p build/libuv && cd build/libuv && \
  cmake ../../libuv -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF && \
  make -j"$(nproc)" && sudo make install && \
  cd ../.. && rm -rf libuv

# --- Build & install h2o ---
RUN set -eux; \
  git clone --depth 1 --branch v2.2.6 --single-branch "https://github.com/h2o/h2o.git" "h2o" && \
  mkdir -p build/h2o && cd build/h2o && \
  cmake ../../h2o -DCMAKE_INSTALL_PREFIX=/usr/local -DWITH_MRUBY=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
  make -j"$(nproc)" && sudo make install && \
  cd ../.. && rm -rf h2o


# --- Build & install this project --------------------------------------------
COPY --chown=dev:dev . /workspace/h2o-c-library
RUN mkdir -p /workspace/build/h2o-c-library && \
    cd /workspace/build/h2o-c-library && \
    cmake /workspace/h2o-c-library && \
    make -j"$(nproc)" && sudo make install

CMD ["/bin/bash"]
```
