FROM ubuntu:24.04 AS aos-core-build-base

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/.local/bin:${PATH}"

# Install basic build dependencies

RUN apt-get update \
    && apt-get install -y lcov cppcheck libsofthsm2 libsystemd-dev python3-pip\
       pipx cmake git autotools-dev autoconf libtool pkg-config python3-jinja2 \
    && apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN pipx install conan

# Build conan dependencies

COPY conan/* /opt/

WORKDIR /opt

RUN conan profile detect --force
RUN conan install . --output-folder build --settings=build_type=Debug --options=with_poco=True --build=missing \
    && conan install . --output-folder build --settings=build_type=Release --options=with_poco=True --build=missing \
    && conan cache clean

# Set default user
USER root
