#!/bin/bash

set +x

PrintNextStep () {
  echo
  echo "====================================="
  echo "  $1"
  echo "====================================="
  echo
}


#==============================================
if [ "$1" == "clean" ]; then
  PrintNextStep "Clean artifacts"

  rm -rf ./build/
  conan remove 'poco*' -c
  conan remove 'gtest*' -c
fi

#==============================================
PrintNextStep "Setting up conan default profile"

conan profile detect --force
if [ $? -ne 0 ]; then
  echo "!ERROR"; exit 1
fi

#==============================================
PrintNextStep "Generate conan toolchain"

conan install . --output-folder build --settings=build_type=Debug --options=with_poco=True --build=missing
if [ $? -ne 0 ]; then
  echo "!ERROR"; exit 1
fi


#==============================================
PrintNextStep "Run cmake"

cd ./build

cmake .. -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DWITH_COVERAGE=ON -DWITH_TEST=ON
if [ $? -ne 0 ]; then
  echo "!ERROR"; exit 1
fi

#==============================================
PrintNextStep "Run make"

make -j4
if [ $? -ne 0 ]; then
  echo "!ERROR"; exit 1
fi

#==============================================
echo
echo "Build succeeded!"
