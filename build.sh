#!/bin/bash

set +x
set -euo pipefail

print_next_step()
{
    echo
    echo "====================================="
    echo "  $1"
    echo "====================================="
    echo
}

print_usage()
{
    echo
    echo "Usage: ./build.sh <command> [options]"
    echo
    echo "Commands:"
    echo
    echo "  build                      build target"
    echo "  test                       run tests only"
    echo "  coverage                   run tests with coverage"
    echo "  lint                       run static analysis (cppcheck)"
    echo "Options:"
    echo "  --clean                    clean build artifacts"
    echo "  --aos-service <services>   specify services (e.g., sm,mp,iam)"
    echo "  --ci                       use build-wrapper for CI analysis (SonarQube)"
    echo
}

error_with_usage()
{
    echo >&2 "ERROR: $1"
    echo

    print_usage

    exit 1
}

clean_build()
{
    print_next_step "Clean artifacts"

    rm -rf ./build/
    conan remove 'gtest*' -c
    conan remove 'grpc*' -c
    conan remove 'poco*' -c
    conan remove 'libcu*' -c
    conan remove 'opens*' -c
    conan remove 'pkcs11provider*' -c
}

conan_setup()
{
    print_next_step "Setting up conan default profile"
    conan profile detect --force

    print_next_step "Generate conan toolchain"
    conan install ./conan/ --output-folder build --settings=build_type=Debug --build=missing
}

build_project()
{
    local aos_services="$1"
    local ci_flag="$2"

    local with_iam="ON"
    local with_mp="ON"
    local with_sm="ON"

    if [[ -n "$aos_services" ]]; then
        with_iam="OFF"
        with_mp="OFF"
        with_sm="OFF"

        local services_lower
        services_lower=$(echo "$aos_services" | tr '[:upper:]' '[:lower:]')

        IFS=',' read -ra service_array <<< "$services_lower"
        for service in "${service_array[@]}"; do
            service=$(echo "$service" | xargs) # trim whitespace
            case "$service" in
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
        -DCMAKE_BUILD_TYPE=Debug \
        -DWITH_VCHAN=OFF \
        -DWITH_COVERAGE=ON \
        -DWITH_TEST=ON \
        -DWITH_IAM="$with_iam" \
        -DWITH_MP="$with_mp" \
        -DWITH_SM="$with_sm" \
        -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -G "Unix Makefiles"

    if [ "$ci_flag" == "true" ]; then
        print_next_step "Run build-wrapper and build (CI mode)"
        build-wrapper-linux-x86-64 --out-dir "$BUILD_WRAPPER_OUT_DIR" cmake --build ./build/ --config Debug --parallel
    else
        print_next_step "Run build"
        cmake --build ./build/ --config Debug --parallel
    fi

    echo
    echo "Build succeeded!"
}

parse_arguments()
{
    local clean_flag=false
    local aos_services=""
    local ci_flag=false

    while [[ $# -gt 0 ]]; do
        case $1 in
            --clean)
                clean_flag=true
                shift
                ;;

            --aos-service)
                aos_services="$2"
                shift 2
                ;;

            --ci)
                ci_flag=true
                shift
                ;;

            *)
                error_with_usage "Unknown option: $1"
                ;;
        esac
    done

    echo "$clean_flag:$aos_services:$ci_flag"
}

build_target()
{
    local clean_flag="$1"
    local aos_services="$2"
    local ci_flag="$3"

    if [ "$clean_flag" == "true" ]; then
        clean_build

        if [[ -z "$aos_services" ]]; then
            return
        fi
    fi

    conan_setup
    build_project "$aos_services" "$ci_flag"
}

run_tests()
{
    print_next_step "Run tests"
    cd ./build
    make test
    echo
    echo "Tests completed!"
}

run_coverage()
{
    print_next_step "Run tests with coverage"
    cd ./build
    make coverage
    echo
    echo "Coverage completed!"
}

run_lint()
{
    print_next_step "Run static analysis (cppcheck)"

    cppcheck --enable=all --inline-suppr --std=c++17 --error-exitcode=1 \
        --suppressions-list=./suppressions.txt --project=build/compile_commands.json --file-filter='src/*'

    echo
    echo "Static analysis completed!"
}

#=======================================================================================================================

if [ $# -lt 1 ]; then
    error_with_usage "Missing command"
fi

command="$1"
shift

case "$command" in
    build)
        args_result=$(parse_arguments "$@")
        clean_flag=$(echo "$args_result" | cut -d: -f1)
        aos_services=$(echo "$args_result" | cut -d: -f2)
        ci_flag=$(echo "$args_result" | cut -d: -f3)
        build_target "$clean_flag" "$aos_services" "$ci_flag"
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

    *)
        error_with_usage "Unknown command: $command"
        ;;
esac
