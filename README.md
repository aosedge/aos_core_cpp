[![Quality gate](https://sonarcloud.io/api/project_badges/quality_gate?project=aosedge_aos_core_mp_cpp)](https://sonarcloud.io/summary/new_code?id=aosedge_aos_core_mp_cpp)

# Aos core message proxy

## Cloning Submodules

To download the submodules, use the following command:

```sh
git submodule update --init --recursive
```

This command will recursively fetch all submodules located in the `external` directory. Additionally, the submodules themselves may include other submodules, which will also be placed in the `external` directory.

## Building

To build the message proxy, run the build script from the project's root directory:

```sh
./host_build.sh
```

This will initiate a recursive build of the main project and its corresponding submodules. If need to build a specific submodule separately, navigate to that submodule's directory and follow the instructions provided in its own `README`.

After the first run of the build script, also possible to build using the following commands:

```sh
cd build
make -j{count_thread}
```

Replace `{count_thread}` with the number of threads for parallel building.

## Running Tests

To run tests for the message proxy, execute:

```sh
cd build
make test
```

Please note that this will only run tests for the main project, not for the submodules. If need to run tests for a specific submodule, navigate to that submodule, perform a separate build, and run the tests as per the instructions in its `README`.
