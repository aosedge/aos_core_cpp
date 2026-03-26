# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
./build.sh build                              # Debug build (default)
./build.sh build --build-type Release         # Release build
./build.sh build --aos-service sm             # Build only SM
./build.sh build --aos-service sm,iam         # Build specific services
./build.sh build --clean                      # Clean before building
./build.sh test                               # Run all unit tests
./build.sh coverage                           # Run tests with lcov coverage
./build.sh lint                               # Static analysis (cppcheck)
./build.sh doc                                # Generate Doxygen docs
```

To run a single test after building:

```bash
cd build && ctest -R <test_name_regex>
```

Manual CMake workflow (for custom options):

```bash
cd build
conan install ../conan/ --output-folder . --settings=build_type=Debug --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DWITH_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build . --parallel
```

## Code Standards

- **C++17**, compiled with `-Wall -Werror -Wextra -Wpedantic`
- Format code with `clang-format` (WebKit-based style, 120-char line limit, config in `.clang-format`)
- Format CMake files with `cmake-format` (config in `.cmake-format`)
- Static analysis: `cppcheck` with `suppressions.txt`
- Testing framework: Google Test / Google Mock

## Architecture

AosCore is a service-oriented system with four service binaries
that communicate via **gRPC/Protobuf**:

- **CM** (Communication Manager, `aos_cm`) - cloud connectivity, unit
  configuration, network reconciliation, coordinates SM and IAM
  (modules: communication, smcontroller, unitconfig, networkmanager,
  iamclient, database)
- **IAM** (Identity & Access Manager, `aos_iam`) - certificate
  management, PKCS#11 HSM support, node identification
  (modules: iamserver, iamclient, identhandler, currentnode, database)
- **MP** (Message Proxy, `aos_mp`) - inter-service communication
  proxy; supports Xen vchan (`WITH_VCHAN`) or socket transport
  (modules: communication, cmclient, iamclient, filechunker,
  imageunpacker, logprovider)
- **SM** (Service Manager, `aos_sm`) - service lifecycle, image
  management, monitoring, alerts, on-node networking; runtimes:
  container, boot, rootfs (modules: launcher, imagemanager,
  resourcemanager, networkmanager, smclient, monitoring, alerts,
  logprovider, database)

Each service follows the same structure: `src/<service>/app/`
contains the entry point (Poco `ServerApplication` subclass),
with submodules for config, database (SQLite), and
service-specific logic.

### Networking (feature_release_9.1)

CM and SM split network responsibilities and synchronize via gRPC:

- `cm/networkmanager` reconciles desired network state from the cloud,
  pushes firewall/pending updates to SM, and persists pending
  connections in `cm/database` until SM acknowledges them.
- `sm/networkmanager` owns the on-node lifecycle (CNI, traffic
  monitor, namespace/exec helpers); `sm/launcher` drives instances
  through the Create/Start/Stop/Release API.
- `cm/smcontroller` exposes a `NetworkService` gRPC handler;
  `sm/smclient` is its client and notifies via `ConnectListener` so
  CM can run `SyncNetworkState` reconciliation on (re)connect.
- Shared types and conversions live in `common/network` and
  `common/pbconvert` (firewall rules, pending updates,
  `InstanceNetworkStateInfo`).

### Key source layout

- `src/common/` - Shared libraries used by all services:
  cloudprotocol, config, downloader, fileserver, iamclient,
  jsonprovider, logger, logging (journald/stdio), migration,
  network, ocispec, pbconvert, utils, version
- `src/common/tests/` - Shared test infrastructure: mocks, stubs, and test utilities
- `src/<service>/tests/` - Per-service unit tests (collocated with modules)

### External dependencies (fetched via CMake FetchContent)

- **aos_core_lib_cpp** - Core C++ library (error types, interfaces, common abstractions)
- **aos_core_api** - Protobuf/gRPC API definitions

### Conan-managed dependencies

gRPC, Protobuf, Poco (Foundation/JSON/Net/Crypto/DataSQLite), OpenSSL, libcurl, GTest, pkcs11provider
