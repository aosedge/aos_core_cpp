# Rootfs runtime

The rootfs runtime updates the node root filesystem. It is a component runtime with a single instance: the instance
represents the rootfs version that is currently running on the node. Starting a new instance means updating the rootfs.

The update itself is done by the initramfs [aosupdate][aosupdate] module on boot. The runtime prepares the update,
requests reboots, confirms or rejects the updated rootfs with a health check and reports instance statuses. SM and the
initramfs communicate through files in the runtime working directory.

The update must survive power loss and SM restarts at any moment. The main rules are:

- a not applied update is never reported as installed;
- a failed update is never applied or promoted;
- SM always starts, even if runtime state files are corrupted;
- every state change is durable before the next step depends on it.

[aosupdate]: https://github.com/aosedge/meta-aos/blob/develop/recipes-core/initrdscripts/initramfs-framework/aosupdate

## Overview

[RootfsRuntime](rootfs.hpp) implements [aos::sm::launcher::RuntimeItf][runtime-itf] and uses:

- [aos::sm::imagemanager::ItemInfoProviderItf][item-info-provider-itf] - provides paths of the installed image blobs;
- [aos::oci::OCISpecItf][oci-spec-itf] - loads the image manifest;
- [aos::sm::launcher::InstanceStatusReceiverItf][status-receiver-itf] - receives instance statuses and reboot requests;
- [aos::iamclient::CurrentNodeInfoProviderItf][node-info-provider-itf] - provides node info for the runtime info and the
  default instance ident;
- [SystemdUpdateChecker](../utils/systemdupdatechecker.hpp) - checks that the configured systemd units are active after
  the update;
- [SystemdRebooter](../utils/systemdrebooter.hpp) - reboots the node through systemd.

[runtime-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/sm/launcher/itf/runtime.hpp
[item-info-provider-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/sm/imagemanager/itf/iteminfoprovider.hpp
[oci-spec-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/common/ocispec/itf/ocispec.hpp
[status-receiver-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/sm/launcher/itf/instancestatusreceiver.hpp
[node-info-provider-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/common/iamclient/itf/currentnodeinfoprovider.hpp

```mermaid
classDiagram
    direction LR

    class RuntimeItf ["aos::sm::launcher::RuntimeItf"] {
        <<interface>>
    }
    class RootfsRuntime ["aos::sm::launcher::RootfsRuntime"] {
        +Start()
        +Stop()
        +StartInstance()
        +StopInstance()
        +Reboot()
    }
    class ItemInfoProviderItf ["aos::sm::imagemanager::ItemInfoProviderItf"] {
        <<interface>>
    }
    class OCISpecItf ["aos::oci::OCISpecItf"] {
        <<interface>>
    }
    class InstanceStatusReceiverItf ["aos::sm::launcher::InstanceStatusReceiverItf"] {
        <<interface>>
    }
    class SystemdUpdateChecker ["aos::sm::launcher::utils::SystemdUpdateChecker"]
    class SystemdRebooter ["aos::sm::launcher::utils::SystemdRebooter"]

    RuntimeItf <|.. RootfsRuntime
    RootfsRuntime ..> ItemInfoProviderItf
    RootfsRuntime ..> OCISpecItf
    RootfsRuntime ..> InstanceStatusReceiverItf
    RootfsRuntime *-- SystemdUpdateChecker
    RootfsRuntime *-- SystemdRebooter
```

`StopInstance` only reports the instance as inactive: the rootfs can't be stopped. `InitInstances` does nothing and
`GetInstanceMonitoringData` is not supported.

## Configuration

The runtime is configured in the SM `runtimes` section:

```json
{
    "plugin": "rootfs",
    "type": "rootfs",
    "isComponent": true,
    "config": {
        "workingDir": "/var/aos/workdirs/sm/runtimes/rootfs",
        "versionFilePath": "/etc/aos/version",
        "healthCheckServices": ["aos-servicemanager.service", "aos-updatemanager.service"]
    }
}
```

| Parameter             | Default                                | Description                                         |
| --------------------- | -------------------------------------- | --------------------------------------------------- |
| `workingDir`          | `<SM working dir>/runtimes/rootfs`     | Update artifacts directory shared with initramfs.   |
| `versionFilePath`     | `/etc/aos/version`                     | Rootfs version file, format: `VERSION="<version>"`. |
| `healthCheckServices` | empty                                  | systemd units that must be active after the update. |

`workingDir` must be the directory that the initramfs uses as the update directory (`aosupdate.disk` and
`aosupdate.path` kernel parameters).

## Instance identity and version

The runtime has a single instance:

- **current instance** - the installed rootfs, stored in `installed_instance.json`. If the file doesn't exist (first
  start) or is corrupted, it is created from the version file with the default ident: item ID is the runtime type,
  subject ID is the node type, the instance is preinstalled.
- **pending instance** - the update in progress, stored in `pending_instance.json`.

The rootfs version file of the update image must contain the version of the update item. The version is used to check
that the node really runs the updated rootfs.

## Working directory

| File                      | Written by    | Description                                                             |
| ------------------------- | ------------- | ----------------------------------------------------------------------- |
| `installed_instance.json` | SM            | Current instance info.                                                  |
| `pending_instance.json`   | SM            | Pending instance info, boot ID and confirmed flag.                      |
| `image.squashfs`          | SM            | Update image copied from the image manager blob.                        |
| `do_update`               | SM            | Requests the update, contains the update type: `full` or `incremental`. |
| `updated`                 | initramfs     | Update image is mounted, the node runs the updated rootfs (trial boot). |
| `do_apply`                | SM            | Trial boot is confirmed, the update should be applied.                  |
| `failed`                  | SM, initramfs | Update failed, contains the failure reason.                             |
| `*.tmp`                   | SM            | Temporary files of atomic writes, never used by initramfs.              |

`pending_instance.json` format:

```json
{
    "itemId": "rootfs",
    "subjectId": "nodeType",
    "instance": 0,
    "manifestDigest": "sha256:...",
    "type": "component",
    "version": "1.0.1",
    "preinstalled": false,
    "bootId": "5b0f1c1a-...",
    "confirmed": false
}
```

- `bootId` - the boot ID (`/proc/sys/kernel/random/boot_id`) at the moment SM made the last update step. It is used to
  distinguish an SM restart within the same boot from a reboot.
- `confirmed` - set when the health check confirmed the update and `do_apply` is durable. A confirmed pending instance
  without action files means that initramfs applied the update.

`installed_instance.json` has the same format, `bootId` and `confirmed` are not used.

## Initramfs contract

The initramfs [aosupdate][aosupdate] module selects the action by action files in the following priority:

| Priority | Condition   | Action                                                                                     |
| -------- | ----------- | ------------------------------------------------------------------------------------------ |
| 1        | `do_apply`  | **apply**: rsync the image into the rootfs, remount it read-only, remove all action files. |
| 2        | `updated`   | **revert**: remove all action files and write `failed` with `update not confirmed`.        |
| 3        | `do_update` | **update**: mount the image as rootfs (full) or overlay (incremental), write `updated`.    |

Other details that the runtime relies on:

- `do_update` is not removed after the update, it is removed only by apply or revert;
- the image is found with `find -name "*.squashfs" | head -n1`, so only one image may exist in the working directory and
  temporary files must not match this pattern;
- on any error initramfs writes `failed` with the error message but keeps the other action files. If apply fails,
  `do_apply` remains and apply is retried on the next boot;
- empty `do_update` means a full update. Apply fails if `do_update` or the image is missing: applying an incremental
  image as a full one (`rsync --delete`) removes rootfs files that are missing in the image;
- action files are removed in synced steps, so a power loss never leaves a set of flags that selects a wrong action.
  Apply removes `updated` and `failed`, then `do_apply`, then `do_update`: an interruption repeats the apply, or
  repeats the trial boot of the applied update, which is confirmed and applied again. Revert removes `do_update`, then
  `updated` and `failed`: an interruption repeats the revert.

## Update flow

```mermaid
sequenceDiagram
    autonumber
    participant L as Launcher
    participant R as RootfsRuntime
    participant W as Working dir
    participant I as Initramfs

    L->>R: StartInstance(new rootfs)
    R->>W: clear artifacts
    R->>W: image.squashfs (copied, verified)
    R->>W: pending_instance.json (bootId, confirmed=false)
    R->>W: do_update (commit point)
    R-->>L: Activating, RebootRequired
    Note over I: reboot
    I->>W: mount image, write updated
    Note over R: trial boot, SM starts
    R->>R: verify rootfs version
    R->>R: health check (systemd units)
    R->>W: pending_instance.json (new bootId)
    R->>W: do_apply
    R->>W: pending_instance.json (confirmed=true)
    R-->>L: Activating, RebootRequired
    Note over I: reboot
    I->>W: rsync image to rootfs, remove action files
    Note over R: SM starts
    R->>R: confirmed and version matches
    R->>W: installed_instance.json = pending
    R->>W: clear artifacts
    R-->>L: old Inactive, new Active
```

If the health check fails or the rootfs version doesn't match, SM removes `do_apply`, writes `failed` with the reason
and requests a reboot. The initramfs reverts the update on the next boot (no `do_apply`, `updated` exists), and SM
reports the update as failed and the current instance as active.

### Preparing the update

`StartInstance` returns the known status without any action if the instance is the current or the pending one:

- if the pending instance is failed, the update error is returned as well: the launcher treats a successful start as an
  active instance;
- if a reboot is still required (the pending update or a trial boot revert is waiting for it), the reboot request is
  sent again. The launcher skips duplicated requests.

Otherwise, the update is prepared:

1. Wait for the running health check: its verdict must be stored before update artifacts are replaced.
2. Load the image manifest and get the update type from the first layer media type. Supported media types:
   `[application/]vnd.aos.image.component.<full|inc>.v<N>[+<suffix>]`.
3. If the rootfs may be partially updated (initramfs apply failed, or `do_apply` exists without a valid pending
   instance), refuse an incremental update: the incremental image is mounted over and applied to the current rootfs.
   Update artifacts are not changed, so initramfs retries the apply on the next boot. Only a full update replaces them.
4. Clear all update artifacts. Preparation is aborted if a previous `do_apply`, pending instance or `do_update` can't
   be durably removed.
5. Copy the layer blob to `image.squashfs.tmp`, verify its size and sha256 digest against the manifest layer
   descriptor, fsync and rename it to `image.squashfs`.
6. Store `pending_instance.json` with the current boot ID and `confirmed=false`.
7. Store `do_update` with the update type. This is the commit point: initramfs starts the update only if `do_update`
   exists, and at this moment all other artifacts are durable.
8. Request a reboot.

If any preparation step except the refusal fails, artifacts are cleared and the instance is reported as failed.
Preparation also fails if the boot ID can't be read. If only the reboot request fails, the prepared update is kept, the
error is returned and the request is sent again on the next `StartInstance` for this instance.

### Health check verdict

The verdict is stored in steps, each step is durable before the next one:

| Verdict  | Steps                                                                                   |
| -------- | --------------------------------------------------------------------------------------- |
| confirm  | `pending_instance.json` (new boot ID) -> `do_apply` -> `pending_instance.json` (confirmed) |
| reject   | `pending_instance.json` (new boot ID) -> remove `do_apply` -> `failed` (reason)            |

The confirmed flag is written only after `do_apply` is durable. Any interruption between the steps results in a failed
update or in an applied update that is still detected by the version change, but never in promotion of a not applied
update.

If storing the confirm verdict fails, the reject verdict is stored: without `do_apply` initramfs reverts the update
anyway. If the verdict can't be stored, `do_apply` removal is retried, and the reboot is requested only if the removal
is durable (the file is removed or absent, and the working directory is synced): `do_apply` absence alone doesn't mean
that its removal survives a power loss. Otherwise the update is reported as failed but the reboot is not requested: it
would apply the update. The update state is processed again on the next SM start.

Both verdicts start with storing the trial boot ID. If SM is restarted in the same boot after this step but before
`do_apply` or `failed` is stored, the verdict is unknown: the update is rejected with `update verdict is interrupted`
instead of running the health check again, which could confirm the update it has just rejected.

The health check is started after `Start` sends its statuses, so the health check result is always received after them.
If the pending update is replaced before the verdict is stored, the verdict is discarded.

The health check waits until all configured systemd units are active or any of them failed, with retries (5 attempts,
10 seconds to 1 minute delay).

## Start processing

On `Start`, the runtime loads the current and pending instances, reads all action files and decides what to do. Lookup
errors of the state files are returned as errors and are never treated as missing files: a wrong decision may overwrite
the installed instance or promote an unconfirmed update.

```mermaid
flowchart TD
    start([Start]) --> pending{valid pending instance?}
    pending -- no --> noPendingApply{do_apply?}
    noPendingApply -- yes --> keep[keep artifacts,<br/>recovery required]
    noPendingApply -- no --> trial{updated?}
    trial -- yes --> revert[write failed,<br/>reboot to revert]
    trial -- no --> clear[clear stale artifacts,<br/>verify installed]
    pending -- yes --> applyOrTrial{do_apply or updated?}
    applyOrTrial -- yes --> failed1{failed?}
    failed1 -- yes --> waitFailed[wait reboot: Failed]
    failed1 -- no --> doApply{do_apply?}
    doApply -- yes --> waitActivating[wait reboot: Activating]
    doApply -- no --> updated[trial boot:<br/>reject if same boot,<br/>else version check + health check]
    applyOrTrial -- no --> failed2{failed?}
    failed2 -- yes --> processFailed[update failed:<br/>clear artifacts,<br/>verify installed]
    failed2 -- no --> doUpdate{do_update?}
    doUpdate -- yes --> waitActivating
    doUpdate -- no --> noAction[promote or fail]
```

### No valid pending instance

The update can't be tracked, so it is canceled if it is safe:

- `do_apply` exists: the update is confirmed, or initramfs failed to apply it and the rootfs may be partially updated.
  Initramfs revert only clears action files and doesn't restore the rootfs, so cancellation is not safe. All
  artifacts are kept, initramfs retries the apply on the next boot, no reboot is requested, and the current instance is
  reported as failed with `rootfs update state is unknown, recovery is required`;
- `updated` exists without `do_apply` (trial boot): initramfs didn't modify the rootfs. Write `failed` and request a
  reboot, so initramfs reverts the update. The current instance is reported as active;
- otherwise: clear stale artifacts (images, temporary files, action files) and verify the installed instance (see
  [Installed instance verification](#installed-instance-verification)). The current instance is reported as active.

### Waiting for reboot

`do_update`, `do_apply`, or `updated` with `failed` mean that the update is waiting for a reboot. The pending boot ID
is compared with the current one:

- **same boot** - SM was restarted before the reboot. The pending instance is reported as activating (or failed for
  `updated` + `failed`) and the reboot is requested again. Additionally:
  - `do_apply` with `failed` (rejected update, `do_apply` removal failed): `do_apply` removal is retried. The reboot is
    postponed until `do_apply` is durably removed, otherwise initramfs applies the rejected update;
  - `do_apply` without the confirmed flag (SM was interrupted between storing `do_apply` and confirming): the working
    directory is synced and the confirmed flag is stored;
- **another boot with `do_apply`** - initramfs failed to apply the update, the rootfs may be partially updated. The
  pending instance is reported as failed with the `failed` reason, artifacts are kept and initramfs retries the apply on
  the next boot. No reboot is requested to avoid a reboot loop;
- **another boot with `updated` and `failed`, the node runs the pending version** - revert removes `updated`, so
  initramfs started the trial boot again and `failed` is stale. It happens if only part of the revert flags removal is
  durable. The update is rejected as in [Trial boot](#trial-boot) and the reboot is requested, so initramfs reverts
  it. If the node doesn't run the pending version (or the version is the same as the installed one), the case below
  applies: rejecting would cause a reboot loop if initramfs doesn't process the flags;
- **another boot without `do_apply`** - the reboot was done but initramfs didn't process the update. The update is
  failed (`update is not processed after reboot` or the `failed` reason) and artifacts are cleared.

### Trial boot

`updated` without `failed` and `do_apply` means that the node runs the update image. If the pending boot ID is the
current one, the verdict was interrupted and the update is rejected (see [Health check verdict](#health-check-verdict)).
Otherwise the runtime checks that the version file matches the pending version and starts the health check in a
separate thread. The verdict is stored as
described in [Health check verdict](#health-check-verdict), the result is reported and a reboot is requested.

### Failed update

`failed` without `do_apply` and `updated` means that initramfs reverted the update or failed to start it. The pending
instance is reported as failed with the `failed` reason, artifacts are cleared, the installed instance is verified (see
[Installed instance verification](#installed-instance-verification)) and the current instance is reported as active.

### Installed instance verification

When no update is in progress (no `updated` and `do_apply`), the installed rootfs is running and the version file must
match the installed instance version. A mismatch means that the rootfs was changed without SM tracking, e.g. initramfs
applied an update whose pending instance was lost. The installed instance is restored from the version file with the
default ident (as for a corrupted `installed_instance.json`), the previous instance is reported as inactive and the
restored one as active. The cloud then sees that the desired instance isn't installed and requests it again.

Verification errors (unreadable version file, store error) don't fail the start: the check is repeated on the next
start.

### No action files

A pending instance without action files means one of:

- initramfs applied the update and removed all action files;
- the update was interrupted before `do_update` was stored.

The update is promoted only if the rootfs version matches the pending version and one of:

- the update is confirmed;
- the pending version differs from the current version: the version change proves that the update is applied.

An update with the same version as the current one requires the confirmed flag, as the version can't distinguish the
two cases.

On promotion, `installed_instance.json` is overwritten with the pending instance, artifacts are cleared, the previous
instance is reported as inactive and the new one as active. If promotion is interrupted after
`installed_instance.json` is written, it is repeated on the next start and only the active status is reported.

Otherwise, the update is reported as failed (`update is not confirmed` or `rootfs version mismatch`).

## Durability

### Atomic writes

All state files are written atomically:

1. write to `<file>.tmp` (permissions `0600`);
2. fsync the file and check write and close errors;
3. rename to `<file>`;
4. fsync the working directory.

A power loss leaves either the old or the new file, never an empty or partially written one. Temporary files are removed
on errors and on cleanup.

### Cleanup

Artifacts are cleared in steps. Each step is synced, and cleanup stops if the step fails:

1. `do_apply`: initramfs applies the update if `do_apply` exists. If the pending instance is removed first, an update
   applied on the next boot is not tracked and the installed instance doesn't match the rootfs. Without `do_apply`,
   initramfs reverts the trial boot and the remaining pending instance is reported as failed. `do_apply` must also
   never remain without `do_update` and the image: applying an incremental image as a full one (`rsync --delete`)
   removes rootfs files that are missing in the image;
2. `pending_instance.json`: removing other action files (e.g. `failed`) while the pending instance remains may lead to
   promotion of a failed update;
3. `updated`, `do_update` and `failed`. Removal stops on the first error. Removing the image while `do_update` remains
   makes initramfs fail without the image;
4. images and temporary files.

Without a pending instance, any remaining action files are treated as stale and removed on the next start.

### Corrupted state

- corrupted or incomplete `installed_instance.json` is recreated from the version file with the default ident. If
  `updated` or `do_apply` exists, the running rootfs is the trial one or may be partially updated, so the version file
  doesn't identify the installed rootfs: the instance is restored in memory only, and the file is recreated on a start
  without these action files (e.g. after the update is reverted);
- corrupted or incomplete `pending_instance.json` cancels the update if it is safe, otherwise keeps the apply
  artifacts (see [No valid pending instance](#no-valid-pending-instance)).

An instance info is incomplete if the item ID or version is empty.

## Power loss scenarios

| Interrupted at                                  | Result on next start                              |
| ----------------------------------------------- | ------------------------------------------------- |
| image copy                                      | temporary image removed as stale artifact         |
| after pending instance, before `do_update`      | update failed (not confirmed or version mismatch) |
| after `do_update`, before reboot (SM restart)   | reboot requested again                            |
| trial boot, before verdict                      | initramfs reverts (no `do_apply`), update failed  |
| during verdict (SM restart)                     | update rejected, reboot to revert                 |
| after `do_apply`, before confirmed (SM restart) | confirmed flag repaired, reboot requested again   |
| after `do_apply`, before confirmed (power off)  | applied, promoted if version changed, else failed |
| during initramfs apply                          | initramfs retries apply on next boot              |
| during initramfs apply flags removal            | apply or trial boot of applied update repeated    |
| during initramfs revert flags removal           | revert repeated, or trial rejected and reverted   |
| cleanup of confirmed update for a new one       | do_apply removed first: reverted, update failed   |
| after `installed_instance.json`, before cleanup | promotion repeated, artifacts cleared             |
| during cleanup                                  | cleanup repeated, never promotes failed update    |

## Reboot requests

Reboots are requested through `InstanceStatusReceiverItf::RebootRequired`. The launcher reboots the node when no launch
is in progress.

While processing the update state, the runtime only sets the reboot required flag. The request is sent by `Start`,
`StartInstance` and the health check thread after the runtime lock is released. If a reboot request fails:

- the error is reported in the instance status;
- the runtime keeps the reboot required flag, and the next `StartInstance` for the current or pending instance sends
  the request again.

A reboot is not requested when it would apply an untracked or rejected update (`do_apply` can't be removed or the
verdict can't be stored) and when initramfs failed to apply the update (to avoid a reboot loop).

## Concurrency

- `mMutex` protects the runtime state. All state files are changed under it.
- The status receiver (statuses and reboot requests) is never called under `mMutex`: the launcher calls the runtime
  under its own lock, so calling the launcher under `mMutex` would create a lock order inversion.
- The health check runs in a separate thread, started by `Start` after its statuses are sent. The thread doesn't hold
  `mMutex` during the check, takes it to store the verdict and requests the reboot after releasing it.
- `mHealthCheckMutex` protects the health check thread object. `JoinHealthCheck` is called without `mMutex`:
  - in `StartInstance` before preparing a new update, so the verdict can't be written after new artifacts;
  - in `Start` to join the thread of a previous start;
  - in `Stop` and in the destructor: destroying a joinable thread terminates the process.

## Statuses

| Situation                           | Pending instance     | Current instance                  |
| ----------------------------------- | -------------------- | --------------------------------- |
| no update                           | -                    | Active                            |
| update prepared or waiting reboot   | Activating           | -                                 |
| trial boot, health check running    | Activating           | -                                 |
| update rejected, waiting reboot     | Failed (reason)      | -                                 |
| initramfs apply failed              | Failed (reason)      | -                                 |
| update failed or reverted           | Failed (reason)      | Active                            |
| update promoted                     | Active (new current) | Inactive (old)                    |
| `do_apply` without pending instance | -                    | Failed (reason)                   |
| installed instance mismatch         | -                    | Inactive (old), Active (restored) |

## Limitations

- The initramfs doesn't sync its action file changes. This is safe, as all initramfs actions are idempotent, but a
  power loss may repeat an action.
- On revert, initramfs replaces the `failed` reason written by SM with `update not confirmed`, so the original health
  check failure reason is reported only until the reboot.
- The initramfs doesn't verify the image, it relies on the verification done by SM.
- If the rootfs may be partially updated (initramfs apply failed, or `do_apply` without a valid pending instance), the
  apply artifacts are kept until the initramfs applies the update or a new full update is started. The full update
  clears them, so until it is applied the node boots the partially updated rootfs. The restriction is kept in memory
  only: after SM restart in this window an incremental update is accepted again.
- If cleanup removes `do_apply` of a confirmed same version update and the initramfs revert loses `failed` on power
  loss, the update is promoted: the same version can't distinguish the applied and the reverted rootfs.
- A failed update stays in memory until SM restarts: `StartInstance` for it returns the update error without retrying.
- If `installed_instance.json` is corrupted during an update, the current instance is reported with the version of
  the running (trial) rootfs until the update is reverted or promoted.
- If the health check verdict can't be stored and `do_apply` isn't durably removed, the reboot is postponed: the node
  keeps running the trial rootfs and the update is reported as failed until SM restarts. Any reboot in the meantime
  (e.g. initiated outside of SM) applies the update. The applied update is promoted on the next start if its version
  changed, otherwise it is reported as failed.
