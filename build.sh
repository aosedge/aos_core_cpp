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
    echo "  build                      builds target (options below apply here)"
    echo "  install                    installs built apps, their runtime deps into the prefix chosen"
    echo "                             at build time, then enables and configures AosCore"
    echo "  test                       runs tests only"
    echo "  coverage                   runs tests with coverage"
    echo "  lint                       runs static analysis (cppcheck)"
    echo "  doc                        generates documentation"
    echo
    echo "Options (for 'build'):"
    echo "  --clean                    cleans build artifacts before building"
    echo "  --aos-service <services>   specifies services (e.g., sm,mp,iam)"
    echo "  --sm-runtime <runtimes>    specifies sm runtimes (e.g., boot,rootfs,container)"
    echo "  --ci                       uses build-wrapper for CI analysis (SonarQube)"
    echo "  --core-dir <path>          specifies path to core libs directory"
    echo "  --parallel <N>             specifies number of parallel jobs for build (default: all available cores)"
    echo "  --build-type <type>        specifies build type (default: Debug, other options: Release, RelWithDebInfo, MinSizeRel)"
    echo "  --install-prefix <path>    specifies install prefix (default: /usr/local)"
    echo "  --no-test                  builds without tests (tests are built by default)"
    echo "  --no-coverage              builds without coverage instrumentation (coverage is built by default)"
    echo "  --aos-install              installs AosCore configs, systemd services and dependencies (disabled by default)"
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

cmake_configure() {
    conan_setup

    local with_cm="ON"
    local with_iam="ON"
    local with_mp="ON"
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

    local with_runtime_boot="ON"
    local with_runtime_rootfs="ON"
    local with_runtime_container="ON"

    if [[ -n "$ARG_SM_RUNTIMES" ]]; then
        with_runtime_boot="OFF"
        with_runtime_rootfs="OFF"
        with_runtime_container="OFF"

        local runtimes_lower
        runtimes_lower=$(echo "$ARG_SM_RUNTIMES" | tr '[:upper:]' '[:lower:]')

        IFS=',' read -ra runtime_array <<<"$runtimes_lower"
        for runtime in "${runtime_array[@]}"; do
            runtime=$(echo "$runtime" | xargs) # trim whitespace
            case "$runtime" in
            "boot")
                with_runtime_boot="ON"
                ;;

            "rootfs")
                with_runtime_rootfs="ON"
                ;;

            "container")
                with_runtime_container="ON"
                ;;

            *)
                error_with_usage "Unknown sm runtime: $runtime"
                ;;
            esac
        done
    fi

    print_next_step "Run cmake configure"

    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE="$ARG_BUILD_TYPE" \
        ${ARG_CORE_DIR+-DAOS_CORE_DIR="$ARG_CORE_DIR"} \
        ${ARG_INSTALL_PREFIX+-DCMAKE_INSTALL_PREFIX="$ARG_INSTALL_PREFIX"} \
        -DWITH_VCHAN=OFF \
        -DWITH_COVERAGE="$ARG_WITH_COVERAGE" \
        -DWITH_TEST="$ARG_WITH_TEST" \
        -DWITH_AOS_INSTALL="$ARG_WITH_AOS_INSTALL" \
        -DWITH_CM="$with_cm" \
        -DWITH_IAM="$with_iam" \
        -DWITH_MP="$with_mp" \
        -DWITH_SM="$with_sm" \
        -DWITH_RUNTIME_BOOT="$with_runtime_boot" \
        -DWITH_RUNTIME_ROOTFS="$with_runtime_rootfs" \
        -DWITH_RUNTIME_CONTAINER="$with_runtime_container" \
        -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -G "Unix Makefiles"

    print_next_step "Generate API targets"

    for api in iam sm; do
        var="with_$api"
        if [ "${!var}" == "ON" ]; then
            cmake --build build --target "aos_api_${api}"
        fi
    done
}

build_project() {
    cmake_configure

    print_next_step "Run build"

    cmake --build ./build/ --config "$ARG_BUILD_TYPE" --parallel "$ARG_PARALLEL_JOBS"

    if [ "$ARG_CI_FLAG" == "true" ]; then
        mkdir -p "$BUILD_WRAPPER_OUT_DIR"
        cp build/compile_commands.json "$BUILD_WRAPPER_OUT_DIR"/compile_commands.json
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

        --sm-runtime)
            if [[ -n "$ARG_SM_RUNTIMES" ]]; then
                ARG_SM_RUNTIMES="$ARG_SM_RUNTIMES,$2"
            else
                ARG_SM_RUNTIMES="$2"
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

        --install-prefix)
            ARG_INSTALL_PREFIX="$2"
            shift 2
            ;;

        --no-test)
            ARG_WITH_TEST=OFF
            shift
            ;;

        --no-coverage)
            ARG_WITH_COVERAGE=OFF
            shift
            ;;

        --aos-install)
            ARG_WITH_AOS_INSTALL=ON
            shift
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

    build_project
}

enable_units() {
    local install_prefix="$1"
    local unit_dir="$install_prefix/lib/systemd/system"

    shopt -s nullglob
    local units=("$unit_dir"/aos-*.service "$unit_dir"/aos.target)
    shopt -u nullglob

    if [ ${#units[@]} -eq 0 ]; then
        echo "Skipping: no aos units found in $unit_dir"

        return
    fi

    for unit in "${units[@]}"; do
        systemctl enable --now "$unit"
    done

    echo "Enabled and started: ${units[*]##*/}"
}

add_openssl_include() {
    local install_prefix="$1"
    local openssl_cnf="/etc/ssl/openssl.cnf"
    local include_line=".include ${install_prefix}/etc/aos/aos-openssl.cnf"

    if ! grep -qF "$include_line" "$openssl_cnf"; then
        echo "$include_line" >> "$openssl_cnf"
    fi
}

setup_dnsmasq() {
    local install_prefix="$1"

    mkdir -p /etc/dnsmasq.d
    ln -sf "$install_prefix/etc/aos/aos-dnsmasq.conf" /etc/dnsmasq.d/aos-dnsmasq.conf

    mkdir -p /var/aos/dns
    touch /var/aos/dns/addnhosts

    cat >/etc/systemd/network/10-dummy0.netdev <<EOF
[NetDev]
Name=dummy0
Kind=dummy
EOF

    cat >/etc/systemd/network/10-dummy0.network <<EOF
[Match]
Name=dummy0

[Network]
Address=10.0.0.100/24
EOF

    local dnsmasq_default="/etc/default/dnsmasq"

    if grep -q '^IGNORE_RESOLVCONF=yes' "$dnsmasq_default"; then
        :
    elif grep -q '^#\?IGNORE_RESOLVCONF=' "$dnsmasq_default"; then
        sed -i 's/^#\?IGNORE_RESOLVCONF=.*/IGNORE_RESOLVCONF=yes/' "$dnsmasq_default"
    else
        echo 'IGNORE_RESOLVCONF=yes' >>"$dnsmasq_default"
    fi

    systemctl restart systemd-networkd
    systemctl restart dnsmasq
}

run_install() {
    print_next_step "Install AosCore"

    # build type and install prefix are already baked into the build/ cache from the preceding `build` call.
    cmake --install ./build

    print_next_step "Enable AosCore"

    local install_prefix
    
    install_prefix=$(grep '^CMAKE_INSTALL_PREFIX:PATH=' ./build/CMakeCache.txt | cut -d= -f2)
    install_prefix="${install_prefix:-/usr/local}"

    # update shared library cache
    ldconfig

    # update CA certificates
    update-ca-certificates

    # add aos openssl config include
    add_openssl_include "$install_prefix"

    # set up dnsmasq (dummy interface, IGNORE_RESOLVCONF, aos-dnsmasq.conf)
    setup_dnsmasq "$install_prefix"

    # enable systemd units if any were installed
    enable_units "$install_prefix"

    echo
    echo "Install completed!"
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
    make coverage
    echo
    echo "Coverage completed!"
}

run_lint() {
    cmake_configure

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
ARG_SM_RUNTIMES=""
ARG_CI_FLAG=false
ARG_PARALLEL_JOBS=$(nproc)
ARG_BUILD_TYPE="Debug"
ARG_WITH_TEST=ON
ARG_WITH_COVERAGE=ON
ARG_WITH_AOS_INSTALL=OFF

case "$command" in
build)
    parse_arguments "$@"
    build_target
    ;;

install)
    run_install
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
