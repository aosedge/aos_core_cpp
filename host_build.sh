#!/bin/bash

set +x
set -e

print_next_step() {
  echo
  echo "====================================="
  echo "  $1"
  echo "====================================="
  echo
}

#=======================================================================================================================

if [ "$1" == "clean" ]; then
  print_next_step "Clean artifacts"

  rm -rf ./build/
  conan remove 'poco*' -c
  conan remove 'gtest*' -c
  conan remove 'grpc*' -c
  conan remove 'protobuf*' -c
fi

#=======================================================================================================================

print_next_step "Setting up conan default profile"

conan profile detect --force

#=======================================================================================================================

print_next_step "Generate conan toolchain"

conan install ./conan/ --output-folder build --settings=build_type=Debug --build=missing

#=======================================================================================================================

print_next_step "Run cmake"

cd ./build

cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DWITH_COVERAGE=ON \
  -DWITH_TEST=ON -DWITH_CORELIB=ON

#=======================================================================================================================

print_next_step "Run make"

make -j4

#=======================================================================================================================

echo
echo "Build succeeded!"
