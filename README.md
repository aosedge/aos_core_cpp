[![ci](https://github.com/aosedge/aos_core_cpp/actions/workflows/build_test.yaml/badge.svg)](https://github.com/aosedge/aos_core_cpp/actions/workflows/build_test.yaml)
[![codecov](https://codecov.io/gh/aosedge/aos_core_cpp/graph/badge.svg?token=MknkthRkpf)](https://codecov.io/gh/aosedge/aos_core_cpp)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=aosedge_aos_core_cpp&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=aosedge_aos_core_cpp)

# AosCore C++ implementation

## Prepare build environment

```console
sudo apt install lcov
pip install conan
```

`lcov` version 2.0 or greater is required. If your Linux distributive doesn't contain the required version, download and
install the required version manually. For example in Ubuntu 22.04 it can be installed as following:

```console
wget https://launchpad.net/ubuntu/+source/lcov/2.0-4ubuntu2/+build/27959742/+files/lcov_2.0-4ubuntu2_all.deb
sudo dpkg -i lcov_2.0-4ubuntu2_all.deb
```

## Build for host

To make a build for host please run:

```console
./build.sh build
```

It installs all external dependencies to conan, creates `./build` directory, builds the AoCore components with unit
tests and coverage calculation target.

It is also possible to customize the build using different cmake options:

```console
cd build/
conan install ../conan/ --output-folder . --settings=build_type=Debug --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DWITH_TEST=ON -DCMAKE_BUILD_TYPE=Debug
```

Cmake options:

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `AOS_EXTERNAL_DIR` | `path/to/core to` | `build/core` | Aos core lib and API directory path |
| `WITH_CM` | `ON`, `OFF` | `ON` | build AosCore communication manager (CM) |
| `WITH_IAM` | `ON`, `OFF` | `ON` | build AosCore identity and access manager (IAM) |
| `WITH_MP` | `ON`, `OFF` | `ON` | build AosCore message proxy (MP) |
| `WITH_SM` | `ON`, `OFF` | `ON` | build AosCore service manager (SM) |
| `WITH_VCHAN` | `ON`, `OFF` | `ON` | use Xen vchan as communication transport for MP |
| `WITH_TEST` | `ON`, `OFF` | `OFF` | creates unit tests target |
| `WITH_COVERAGE` | `ON`, `OFF` | `OFF` | creates coverage calculation target |
| `WITH_DOC` | `ON`, `OFF` | `OFF` | creates documentation target |

Cmake variables:

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

`lcov` utility shall be installed on your host to run this target. See [this chapter](#prepare-build-environment).

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

Build container:

```console
docker build -t aos-core-build:latest -f docker/Dockerfile .
```

Run container:

```console
 docker run -v ${PWD}:/opt/aos_core_cpp -w /opt/aos_core_cpp -it --rm aos-core-build:latest
```

All above commands for build, test, coverage etc. can be used inside the docker container.

## Development tools

The following tools are used for code formatting and analyzing:

| Tool | Description | Configuration | Link |
| --- | --- | --- | --- |
| `clang-format` | used for source code formatting | .clang-format | <https://clang.llvm.org/docs/ClangFormat.html> |
| `cmake-format` | used for formatting cmake files | .cmake-format | <https://github.com/cheshirekow/cmake_format> |
| `cppcheck` | used for static code analyzing | | <https://cppcheck.sourceforge.io/> |
