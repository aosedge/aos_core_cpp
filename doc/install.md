# Install AosCore to a Linux distro

This document describes how to build AosCore (`aos_core_cpp`) and install it onto a Linux host as a set of
systemd-managed services: identity and access manager (IAM), communication manager (CM), service manager (SM), and
message proxy (MP).

Installing does more than drop these services in place — it also configures the host to run them, wiring up
`dnsmasq`, OpenSSL (PKCS#11 provider), adds Aos root CA certificates, and the base nftables ruleset.

> **Note:** this install procedure has been verified on Ubuntu 24.04 and may not work correctly on other Linux
> distros.

## Prepare the host

### Install build dependencies

Install build tools and libraries required to build AosCore and its dependencies:

```console
sudo apt-get update
sudo apt install -y git build-essential cmake pkg-config autoconf automake libtool patchelf python3-pip \
    python3-jinja2 zip curl \
    libyajl-dev libcap-dev libseccomp-dev libsystemd-dev \
    libnl-3-dev libnl-route-3-dev libnftables-dev \
    libblkid-dev libefivar-dev libefiboot-dev
```

Install `conan` as C++ package manager:

```console
pip install conan --break-system-packages
source ~/.profile
```

### Install runtime dependencies

`softhsm2` provides the PKCS#11 token used by AosCore to store cryptographic keys and is required both to build and to
run AosCore. `dnsmasq` is used as the local DNS resolver for the AosCore services.

```console
sudo apt install -y softhsm2 dnsmasq
```

Install quota to enable disk quotas on the Aos partition (recommended):

```console
sudo apt install -y quota
```

For AWS host instance, install the `linux-modules-extra-aws` package to have quota support in the kernel:

```console
sudo apt install -y linux-modules-extra-aws
```

### Use a separate partition for AosCore

> **Mandatory:** AosCore requires `/var/aos` to be on its own partition. SM uses the host rootfs as the base
> (lower) layer of the overlay filesystem it builds for each Aos service, and the kernel does not allow mounting an
> overlay on top of a directory that is already part of an overlay — so `/var/aos` cannot itself live on the
> (overlaid) rootfs. Create this partition when installing the Linux distro, or add a separate block device for it,
> before proceeding.

Here is an example of how to format an existing block device and add it as `/var/aos` to `/etc/fstab`. This example
assumes the block device is `/dev/sdaX`; adjust the device name for your system.

```console
sudo mkfs.ext4 -L "aos" /dev/sdaX
```

Add the following line to `/etc/fstab` to mount the partition at `/var/aos`:

```text
LABEL=aos       /var/aos        ext4 defaults 0 2
```

Restart the system or run `sudo mount -a` to mount the partition.

### Enable disk quotas on AosCore partition (recommended)

AosCore uses filesystem quotas to enforce per-service limits on state and storage directories under `/var/aos`;
without quotas enabled, those limits aren't enforced. Enabling quotas requires the partition to be unmounted, so do
this now, before it is used.

Unmount the `/var/aos` partition if it is already mounted:

```console
sudo umount /var/aos
```

Set the quota option on the partition:

```console
sudo tune2fs -O quota /dev/sdaX
```

Update `/etc/fstab` to enable quotas on `/var/aos` partition:

```text
LABEL=aos       /var/aos        ext4 defaults,usrquota,grpquota 0 2
```

Mount the `/var/aos` partition:

```console
sudo mount /var/aos
```

## Build

Clone the repository AosCore repository:

```console
git clone https://github.com/aosedge/aos_core_cpp.git
```

Go to clone directory:

```console
cd aos_core_cpp
```

Run build script to build AosCore:

```console
./build.sh build --build-type Release --no-test --no-coverage --sm-runtime container --aos-install
```

Run `./build.sh` with no arguments to see all available build options.

## Install

```console
sudo ./build.sh install
```

By default this installs to `/usr/local`; pass the same `--install-prefix` used at build time if you built with a
custom one.

## Verify

After successful installation, `aos-iam-prov.service` should be healthy and running, and the other AosCore services
(`aos-cm.service`, `aos-sm.service`, `aos-iam.service`) should be inactive - they stay inactive until provisioning
completes successfully.

```console
systemctl status aos-iam-prov.service
```

Now the host is ready to be provisioned by the AosCore services. See
[AosEdge documentation](https://docs.aosedge.tech/docs/v1/quick-start/) for details.

After successful provisioning, AosCore is ready to run services.
