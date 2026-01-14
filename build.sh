#!/bin/bash

set +x
set -euo pipefail

print_next_step() {
    echo
    echo "====================================="
    echo "  $1"
    echo "====================================="
    echo
}

print_usage() {
    echo
    echo "Usage: ./build.sh <command> [options]"
    echo
    echo "Commands:"
    echo "  build                      builds target"
    echo "  test                       runs tests only"
    echo "  coverage                   runs tests with coverage"
    echo "  lint                       runs static analysis (cppcheck)"
    echo "  doc                        generates documentation"
    echo
    echo "Options:"
    echo "  --clean                    cleans build artifacts before building"
    echo "  --aos-service <services>   specifies services (e.g., sm,mp,iam)"
    echo "  --ci                       uses build-wrapper for CI analysis (SonarQube)"
    echo "  --core-dir <path>          specifies path to core libs directory"
    echo "  --parallel <N>             specifies number of parallel jobs for build (default: all available cores)"
    echo "  --build-type <type>        specifies build type (default: Debug, other options: Release, RelWithDebInfo, MinSizeRel)"
    echo
}

error_with_usage() {
    echo >&2 "ERROR: $1"
    echo

    print_usage

    exit 1
}

clean_build() {
    print_next_step "Clean artifacts"

    rm -rf ./build/
}

conan_setup() {
    print_next_step "Setting up conan default profile"

    conan profile detect --force

    print_next_step "Generate conan toolchain"

    conan install ./conan/ --output-folder build --settings=build_type="$ARG_BUILD_TYPE" --build=missing
}

build_project() {
    local with_cm="ON"
    local with_iam="ON"
    local with_mp="OFF"
    local with_sm="ON"

    if [[ -n "$ARG_AOS_SERVICES" ]]; then
        with_cm="OFF"
        with_iam="OFF"
        with_mp="OFF"
        with_sm="OFF"

        local services_lower
        services_lower=$(echo "$ARG_AOS_SERVICES" | tr '[:upper:]' '[:lower:]')

        IFS=',' read -ra service_array <<<"$services_lower"
        for service in "${service_array[@]}"; do
            service=$(echo "$service" | xargs) # trim whitespace
            case "$service" in
            "cm")
                with_cm="ON"
                ;;

            "iam")
                with_iam="ON"
                ;;

            "mp")
                with_mp="ON"
                ;;

            "sm")
                with_sm="ON"
                ;;

            *)
                error_with_usage "Unknown service: $service"
                ;;
            esac
        done
    fi

    print_next_step "Run cmake configure"

    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE="$ARG_BUILD_TYPE" \
        ${ARG_CORE_DIR+-DAOS_CORE_DIR="$ARG_CORE_DIR"} \
        -DWITH_VCHAN=OFF \
        -DWITH_COVERAGE=ON \
        -DWITH_TEST=ON \
        -DWITH_CM="$with_cm" \
        -DWITH_IAM="$with_iam" \
        -DWITH_MP="$with_mp" \
        -DWITH_SM="$with_sm" \
        -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -G "Unix Makefiles"

    if [ "$ARG_CI_FLAG" == "true" ]; then
        print_next_step "Run build-wrapper and build (CI mode)"

        build-wrapper-linux-x86-64 --out-dir "$BUILD_WRAPPER_OUT_DIR" cmake --build ./build/ --config "$ARG_BUILD_TYPE" --parallel "$ARG_PARALLEL_JOBS"
    else
        print_next_step "Run build"

        cmake --build ./build/ --config "$ARG_BUILD_TYPE" --parallel "$ARG_PARALLEL_JOBS"
    fi

    echo
    echo "Build succeeded!"
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
        --clean)
            ARG_CLEAN_FLAG=true
            shift
            ;;

        --aos-service)
            if [[ -n "$ARG_AOS_SERVICES" ]]; then
                ARG_AOS_SERVICES="$ARG_AOS_SERVICES,$2"
            else
                ARG_AOS_SERVICES="$2"
            fi
            shift 2
            ;;

        --ci)
            ARG_CI_FLAG=true
            shift
            ;;

        --core-dir)
            ARG_CORE_DIR="$2"
            shift 2
            ;;

        --parallel)
            ARG_PARALLEL_JOBS="$2"
            shift 2
            ;;

        --build-type)
            ARG_BUILD_TYPE="$2"
            shift 2
            ;;

        *)
            error_with_usage "Unknown option: $1"
            ;;
        esac
    done
}

build_target() {
    if [ "$ARG_CLEAN_FLAG" == "true" ]; then
        clean_build
    fi

    conan_setup
    build_project
}

run_tests() {
    print_next_step "Run tests"

    cd ./build
    make test
    echo
    echo "Tests completed!"
}

run_coverage() {
    print_next_step "Run tests with coverage"

    cd ./build
    ctest --output-on-failure
    echo
    echo "Coverage completed!"
}

run_lint() {
    print_next_step "Run static analysis (cppcheck)"

    cppcheck --enable=all --inline-suppr --std=c++17 --error-exitcode=1 \
        --suppressions-list=./suppressions.txt --project=build/compile_commands.json --file-filter='src/*'

    echo
    echo "Static analysis completed!"
}

build_doc() {
    print_next_step "Build documentation"

    cd ./build

    cmake -DWITH_DOC=ON ../
    make doc

    echo
    echo "Documentation generated!"
}

#=======================================================================================================================

if [ $# -lt 1 ]; then
    error_with_usage "Missing command"
fi

command="$1"
shift

ARG_CLEAN_FLAG=false
ARG_AOS_SERVICES=""
ARG_CI_FLAG=false
ARG_PARALLEL_JOBS=$(nproc)
ARG_BUILD_TYPE="Debug"

case "$command" in
build)
    parse_arguments "$@"
    build_target
    ;;

test)
    run_tests
    ;;

coverage)
    run_coverage
    ;;

lint)
    run_lint
    ;;

doc)
    build_doc
    ;;

*)
    error_with_usage "Unknown command: $command"
    ;;
esac
