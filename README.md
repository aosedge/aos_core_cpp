[![ci](https://github.com/aosedge/aos_core_iam_cpp/actions/workflows/build_test.yaml/badge.svg)](https://github.com/aosedge/aos_core_iam_cpp/actions/workflows/build_test.yaml)
[![codecov](https://codecov.io/gh/aosedge/aos_core_iam_cpp/graph/badge.svg?token=MknkthRkpf)](https://codecov.io/gh/aosedge/aos_core_iam_cpp)

# Identity and Access Manager(IAM)

## Prepare build environment

```sh
sudo apt install lcov libsofthsm2 libsystemd-dev
pip install conan
```

## Build for host

To make a build for host please run:

```sh
./host_build.sh
```

It installs all external dependencies to conan, creates `./build` directory and builds the manager with unit tests and coverage calculation target.

It is also possible to customize the build using different cmake options:

```sh
cd ${BUILD_DIR}
conan install ../conan/ --output-folder . --settings=build_type=Debug --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DWITH_TEST=ON -DCMAKE_BUILD_TYPE=Debug
```

Cmake options:

| Option | Description |
| --- | --- |
| `WITH_TEST` | creates unit tests target |
| `WITH_COVERAGE` | creates coverage calculation target |
| `WITH_DOC` | creates documentation target |

Options should be set to `ON` or `OFF` value.

Cmake variables:

| Variable | Description |
| --- | --- |
| `CMAKE_BUILD_TYPE` | `Release`, `Debug`, `RelWithDebInfo`, `MinSizeRel` |
| `CMAKE_INSTALL_PREFIX` | overrides default install path |

## Run unit tests

Build and run:

```sh
./host_build.sh
cd ${BUILD_DIR}
make test
```

## Check coverage

`lcov` utility shall be installed on your host to run this target:

```sh
sudo apt install lcov
```

Build and run:

```sh
./host_build.sh
cd ${BUILD_DIR}
make coverage
```

The overall coverage rate will be displayed at the end of the coverage target output:

```sh
...
Overall coverage rate:
  lines......: 94.7% (72 of 76 lines)
  functions..: 100.0% (39 of 39 functions)
```

Detailed coverage information can be find by viewing `./coverage/index.html` file in your browser.

## Generate documentation

`doxygen` package should be installed before generation the documentations:

```sh
sudo apt install doxygen
```

`host_build.sh` tool doesn't generate documentation. User should run the following commands to do that:

```sh
cd ${BUILD_DIR}
conan install ../conan/ --output-folder . --settings=build_type=Debug --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DWITH_DOC=ON
make doc
```

The result documentation is located in `${BUILD_DIR}/doc folder`. And it can be viewed by opening
`./doc/html/index.html` file in your browser.

## Install libraries

The default install path can be overridden by setting `CMAKE_INSTALL_PREFIX` variable.

Configure example with user defined install prefix:

```sh
cd ${BUILD_DIR}
conan install ../conan/ --output-folder . --settings=build_type=Release --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DCMAKE_INSTALL_PREFIX=/my/location
```

Install:

```sh
cd ${BUILD_DIR}
make  install
```

## Development tools

The following tools are used for code formatting and analyzing:

| Tool | Description | Configuration | Link
| --- | --- | --- | --- |
| `clang-format` | used for source code formatting | .clang-format | <https://clang.llvm.org/docs/ClangFormat.html> |
| `cmake-format` | used for formatting cmake files | .cmake-format | <https://github.com/cheshirekow/cmake_format> |
| `cppcheck` | used for static code analyzing | | <https://cppcheck.sourceforge.io/> |
