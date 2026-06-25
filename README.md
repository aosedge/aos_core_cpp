<!-- markdownlint-disable-next-line MD041 -->
[![ci](https://github.com/aosedge/aos_core_cpp/actions/workflows/build_test.yaml/badge.svg)](https://github.com/aosedge/aos_core_cpp/actions/workflows/build_test.yaml)
[![codecov](https://codecov.io/gh/aosedge/aos_core_cpp/graph/badge.svg?token=MknkthRkpf)](https://codecov.io/gh/aosedge/aos_core_cpp)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=aosedge_aos_core_cpp&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=aosedge_aos_core_cpp)

# AosCore C++ implementation

This repository contains the AosCore services: communication manager (CM), identity and access manager (IAM), message
proxy (MP) and service manager (SM).

This document is a from-scratch build guide. Start with [Get the sources](#get-the-sources) and
[Prepare build environment](#prepare-build-environment).

For a quick build with the default options use `./build.sh build`. For a custom configuration (a subset of
services, a `Release` build, etc.) use the
[manual build with individual CMake options](#custom-build-with-individual-cmake-options).

## Get the sources

```console
git clone https://github.com/aosedge/aos_core_cpp.git
cd aos_core_cpp
```

The build depends on two more Aos repositories — `aos_core_lib_cpp` and `aos_core_api`. They are fetched automatically
at configure time (via CMake `FetchContent`) into the directory pointed to by `AOS_CORE_DIR` (`build/core` by default),
so `git` and network access must be available on the host. To build against local checkouts instead, see
`AOS_CORE_DIR` / `--core-dir` below.

## Prepare build environment

The instructions below target Ubuntu. Other distributions provide the same packages under similar names.

* Install the build tools and libraries:

```console
sudo apt install build-essential git pkg-config autoconf automake libtool \
    libseccomp-dev libyajl-dev libcap-dev libsystemd-dev \
    libnl-3-dev libnl-route-3-dev libnftables-dev \
    libblkid-dev libefivar-dev libefiboot-dev \
    softhsm2 lcov
```

| Package group | Why it is needed |
| --- | --- |
| `build-essential`, `git` | C/C++ compiler, `make`, and fetching the external Aos repositories |
| `pkg-config`, `autoconf`, `automake`, `libtool` | build the `crun` Conan dependency from source |
| `libseccomp-dev`, `libyajl-dev`, `libcap-dev`, `libsystemd-dev` | `crun` build dependencies |
| `libnl-3-dev`, `libnl-route-3-dev`, `libnftables-dev` | required by the SM / CM network code |
| `libblkid-dev`, `libefivar-dev`, `libefiboot-dev` | required by the SM boot runtime |
| `softhsm2` | provides `libsofthsm2`, required by the crypto unit tests (`WITH_TEST=ON`) |

* Install `conan` (package manager for the external dependencies, e.g. gRPC, OpenSSL, Poco, libcurl, crun):

```console
pip install conan
```

* Install `cmake` (version 3.23 or greater is required):

```console
pip install cmake
```

The `cmake` shipped with Ubuntu 22.04 (3.22) is too old. On Ubuntu 24.04 the distribution `cmake` (3.28) is new enough,
so you may use `sudo apt install cmake` instead. The [Kitware APT repository](https://apt.kitware.com/) is another
option for an up-to-date `cmake`.

* Install `lcov`:

```console
sudo apt install lcov
```

`lcov` is required to generate the code coverage report. It is also required to **configure** the project with
`./build.sh build`, because that script always enables the coverage target. Without `lcov` the configuration step
fails with `lcov not found`. Version 2.0 or greater is required. The version shipped with Ubuntu 22.04 (1.x) is
too old; in that case download and install it manually:

```console
wget https://launchpad.net/ubuntu/+source/lcov/2.0-4ubuntu2/+build/27959742/+files/lcov_2.0-4ubuntu2_all.deb
sudo dpkg -i lcov_2.0-4ubuntu2_all.deb
sudo apt-get install -f
```

The last `apt-get install -f` step pulls in the Perl dependencies that the `.deb` requires.

## Build with build.sh

`build.sh` is the recommended entry point. To make a build for host, run:

```console
./build.sh build
```

It fetches the external Aos repositories, installs all external dependencies via Conan (some of them, like `crun`, are
built from source), creates the `./build` directory and builds the AosCore components with the unit test and coverage
targets. The configuration used by `build.sh build` is fixed: all services are enabled, `WITH_TEST=ON`,
`WITH_COVERAGE=ON` and `WITH_VCHAN=OFF`.

`build.sh` accepts the following commands:

| Command | Description |
| --- | --- |
| `build` | configures and builds the project |
| `test` | runs the unit tests (see [Run unit tests](#run-unit-tests)) |
| `coverage` | runs the tests and collects coverage (see [Check coverage](#check-coverage)) |
| `lint` | runs static analysis with `cppcheck` |
| `doc` | generates the documentation (see [Generate documentation](#generate-documentation)) |

And the following options:

| Option | Description |
| --- | --- |
| `--clean` | removes the `./build` directory before building |
| `--aos-service <services>` | builds only the listed services, e.g. `--aos-service sm,mp,iam` (default: all) |
| `--core-dir <path>` | path to local `aos_core_lib_cpp` / `aos_core_api` checkouts (sets `AOS_CORE_DIR`) |
| `--ci` | builds through `build-wrapper` for CI / SonarQube analysis |
| `--parallel <N>` | number of parallel build jobs (default: all available cores) |
| `--build-type <type>` | `Debug` (default), `Release`, `RelWithDebInfo` or `MinSizeRel` |

Example — a clean `Release` build of the service manager and IAM only:

```console
./build.sh build --clean --aos-service sm,iam --build-type Release
```

## Custom build with individual CMake options

If you need options that `build.sh` does not expose, run Conan and CMake manually. From the repository root:

```console
mkdir -p build
cd build
conan install ../conan/ --output-folder . --settings=build_type=Debug --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DWITH_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build . --parallel
```

`conan install` generates `conan_toolchain.cmake` and provides the external dependencies; the `cmake` configure step
must therefore be run with that toolchain file.

CMake options:

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `AOS_CORE_DIR` | `path/to/core` | `core` | directory for the fetched `aos_core_lib_cpp` and `aos_core_api` |
| `WITH_CM` | `ON`, `OFF` | `ON` | build AosCore communication manager (CM) |
| `WITH_IAM` | `ON`, `OFF` | `ON` | build AosCore identity and access manager (IAM) |
| `WITH_MP` | `ON`, `OFF` | `ON` | build AosCore message proxy (MP) |
| `WITH_SM` | `ON`, `OFF` | `ON` | build AosCore service manager (SM) |
| `WITH_VCHAN` | `ON`, `OFF` | `ON` | use Xen vchan as communication transport for MP (needs Xen libs) |
| `WITH_TEST` | `ON`, `OFF` | `OFF` | creates unit tests target (requires `softhsm2`) |
| `WITH_COVERAGE` | `ON`, `OFF` | `OFF` | creates coverage calculation target (requires `lcov`) |
| `WITH_DOC` | `ON`, `OFF` | `OFF` | creates documentation target (requires `doxygen`) |

`AOS_CORE_DIR` is relative to the build directory unless an absolute path is given. `WITH_VCHAN=ON` requires the Xen
vchan development libraries; `build.sh` disables it (`WITH_VCHAN=OFF`) for a host build.

CMake variables:

| Variable | Description |
| --- | --- |
| `CMAKE_BUILD_TYPE` | `Release`, `Debug`, `RelWithDebInfo`, `MinSizeRel` |
| `CMAKE_INSTALL_PREFIX` | overrides default install path |

## Run unit tests

Build and run:

```console
./build.sh test
```

## Check coverage

`lcov` utility shall be installed on your host to run this target. See
[Prepare build environment](#prepare-build-environment).

Build and run:

```console
./build.sh coverage
```

The overall coverage rate will be displayed at the end of the coverage target output:

```console
...
Overall coverage rate:
  lines......: 94.7% (72 of 76 lines)
  functions..: 100.0% (39 of 39 functions)
```

Detailed coverage information can be find by viewing `./coverage/index.html` file in your browser.

## Generate documentation

`doxygen` package should be installed before generation the documentations:

```console
sudo apt install doxygen
```

Generate documentation:

```console
./build.sh doc
```

The result documentation is located in `build/doc` folder. And it can be viewed by opening `build/doc/html/index.html`
file in your browser.

## Use docker container

You can build and use container from [aos_core_lib_cpp](https://github.com/aosedge/aos_core_lib_cpp) repo. See
[this chapter](https://github.com/aosedge/aos_core_lib_cpp?tab=readme-ov-file#use-docker-container) for details.

## Development tools

The following tools are used for code formatting and analyzing:

| Tool | Description | Configuration | Link |
| --- | --- | --- | --- |
| `clang-format` | used for source code formatting | .clang-format | <https://clang.llvm.org/docs/ClangFormat.html> |
| `cmake-format` | used for formatting cmake files | .cmake-format | <https://github.com/cheshirekow/cmake_format> |
| `cppcheck` | used for static code analyzing | | <https://cppcheck.sourceforge.io/> |
