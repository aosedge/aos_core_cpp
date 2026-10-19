# Rootfs runtime

The rootfs runtime updates the node root filesystem. It is a component runtime with a single instance: the instance
represents the rootfs version that is currently running on the node. Starting a new instance means updating the rootfs.

The update itself is done by the initramfs [aosupdate][aosupdate] module on boot. The runtime prepares the update,
requests reboots, confirms or rejects the updated rootfs with a health check and reports instance statuses.

The update is split between two stores:

- **SM storage** is the source of truth for **which instances exist** and their last reported state and error;
- **update state file** is shared with the initramfs and is the source of truth for **where the update is**.

On start, SM reconciles both stores and continues the update. The update must survive power loss and SM restarts at
any moment. The main rules are:

- a not applied update is never reported as installed;
- a failed update is never applied or promoted;
- SM starts even if the update state file is corrupted or the version file of the applied rootfs can't be read;
- every step is either atomic or repeatable, and is durable before the next step depends on it.

[aosupdate]: https://github.com/aosedge/meta-aos/blob/develop/recipes-core/initrdscripts/initramfs-framework/aosupdate

## Overview

[RootfsRuntime](rootfs.hpp) implements [aos::sm::launcher::RuntimeItf][runtime-itf] and uses:

- [aos::sm::launcher::runtimes::StorageItf](../itf/storage.hpp) - stores the current and the pending instances;
- [aos::sm::imagemanager::ItemInfoProviderItf][item-info-provider-itf] - provides paths of the installed image blobs;
- [aos::oci::OCISpecItf][oci-spec-itf] - loads the image manifest;
- [aos::fs::FileInfoProviderItf][file-info-provider-itf] - calculates the size and the sha256 hash of the copied image;
- [aos::sm::launcher::InstanceStatusReceiverItf][status-receiver-itf] - receives instance statuses and reboot requests;
- [aos::iamclient::CurrentNodeInfoProviderItf][node-info-provider-itf] - provides node info for the runtime info and the
  default instance ident;
- [SystemdUpdateChecker](../utils/systemdupdatechecker.hpp) - checks that the configured systemd units are active after
  the update;
- [SystemdRebooter](../utils/systemdrebooter.hpp) - reboots the node through systemd.

[runtime-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/sm/launcher/itf/runtime.hpp
[item-info-provider-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/sm/imagemanager/itf/iteminfoprovider.hpp
[oci-spec-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/common/ocispec/itf/ocispec.hpp
[file-info-provider-itf]: https://github.com/aosedge/aos_core_lib_cpp/blob/develop/src/core/common/tools/fs.hpp
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
    class StorageItf ["aos::sm::launcher::runtimes::StorageItf"] {
        <<interface>>
    }
    class ItemInfoProviderItf ["aos::sm::imagemanager::ItemInfoProviderItf"] {
        <<interface>>
    }
    class OCISpecItf ["aos::oci::OCISpecItf"] {
        <<interface>>
    }
    class FileInfoProviderItf ["aos::fs::FileInfoProviderItf"] {
        <<interface>>
    }
    class InstanceStatusReceiverItf ["aos::sm::launcher::InstanceStatusReceiverItf"] {
        <<interface>>
    }
    class SystemdUpdateChecker ["aos::sm::launcher::utils::SystemdUpdateChecker"]
    class SystemdRebooter ["aos::sm::launcher::utils::SystemdRebooter"]

    RuntimeItf <|.. RootfsRuntime
    RootfsRuntime ..> StorageItf
    RootfsRuntime ..> ItemInfoProviderItf
    RootfsRuntime ..> OCISpecItf
    RootfsRuntime ..> FileInfoProviderItf
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
        "healthCheckServices": ["aos-cm.service", "aos-sm.service", "aos-iam.service"]
    }
}
```

| Parameter             | Default                            | Description                                         |
| --------------------- | ---------------------------------- | --------------------------------------------------- |
| `workingDir`          | `<SM working dir>/runtimes/rootfs` | Update artifacts directory shared with initramfs.   |
| `versionFilePath`     | `/etc/aos/version`                 | Rootfs version file, format: `VERSION="<version>"`. |
| `healthCheckServices` | empty                              | systemd units that must be active after the update. |

`workingDir` must be the directory that the initramfs uses as the update directory (`aosupdate.disk` and
`aosupdate.path` kernel parameters).

## Data

| Data                                                                            | Where            | Written by    |
| ------------------------------------------------------------------------------- | ---------------- | ------------- |
| Current instance: ident, manifestDigest, version, preinstalled, state `eActive` | SM storage       | SM            |
| Pending instance: ident, manifestDigest, version, state, error                  | SM storage       | SM            |
| Update progress: `state`, `type`, `reason`                                      | `update_state`   | SM, initramfs |
| Update image                                                                    | `image.squashfs` | SM            |

### Instances

The runtime instances are stored in the SM storage `runtimeInstances` table, keyed by runtime ID, instance ident
(except the preinstalled flag) and version:

- **current instance** - the installed rootfs, state `eActive`. If it doesn't exist (first start), it is created from
  the version file with the default ident: item ID is the runtime type, subject ID is the node type, the instance is
  preinstalled;
- **pending instance** - the update, state `eActivating` while in progress. When the update fails, the pending
  instance is reported as `eFailed` with the error and removed from storage, so the update can be retried. A failed
  apply is the exception: the pending instance is kept in storage as `eFailed`, as initramfs retries the apply.

The rootfs version file of the update image must contain the version of the update item. The version is used to check
that the node really runs the updated rootfs.

### Working directory

| File             | Written by    | Description                                            |
| ---------------- | ------------- | ------------------------------------------------------ |
| `update_state`   | SM, initramfs | Update state, update type, failure reason.             |
| `image.squashfs` | SM            | Update image copied from the image manager blob.       |
| `*.tmp`          | SM, initramfs | Temporary files of atomic writes, never used as state. |

### Update state file

The file contains one `<key>=<value>` pair per line, so both sides can read and change it with `sed`. The value is the
rest of the line and can't contain line breaks. If a key is repeated, the first line wins.

```text
state=prepared
type=incremental
reason=
```

| Key      | Description                                                                    |
| -------- | ------------------------------------------------------------------------------ |
| `state`  | Update state, see [State machine](#state-machine).                             |
| `type`   | `full` or `incremental`. Missing or unknown type is an error, never defaulted. |
| `reason` | Failure reason, set by SM (health check) or initramfs (mount or apply error).  |

Each change atomically replaces the file: write `update_state.tmp`, sync, rename, sync the directory. A power loss
leaves either the previous or the new state, never a mix of them. Initramfs changes only `state` and `reason` and keeps
other lines as is.

## State machine

```mermaid
stateDiagram-v2
    direction LR

    [*] --> prepared: SM prepares update
    prepared --> trial: initramfs mounts image
    prepared --> failed: initramfs mount error
    trial --> confirmed: SM health check OK
    trial --> rejected: SM health check or version check failed
    trial --> reverted: initramfs, not confirmed
    rejected --> reverted: initramfs
    confirmed --> applying: initramfs starts apply
    confirmed --> failed: initramfs error before apply
    applying --> applied: initramfs applies image
    applying --> applying: initramfs apply error (reason set, retried)
    applied --> [*]: SM promotes
    reverted --> [*]: SM reports failed
    failed --> [*]: SM reports failed
```

| State       | Written by | Initramfs action on boot                                                            |
| ----------- | ---------- | ----------------------------------------------------------------------------------- |
| `prepared`  | SM         | mount image (full: as rootfs, incremental: overlay), write `trial`                  |
| `trial`     | initramfs  | SM didn't confirm the update: write `reverted` with `update not confirmed`          |
| `confirmed` | SM         | write `applying`, then apply as below; on error before `applying` write `failed`    |
| `applying`  | initramfs  | rsync image into rootfs, remount read-only, write `applied`; on error set reason    |
| `rejected`  | SM         | write `reverted`, the reason written by SM is kept                                  |
| `applied`   | initramfs  | none                                                                                |
| `reverted`  | initramfs  | none                                                                                |
| `failed`    | initramfs  | none                                                                                |

No state file or an unknown state means no update. Trial never modifies the installed rootfs, so a revert only
changes the state. Initramfs doesn't verify the image, it relies on the verification done by SM.

## Update flow

1. SM storage stores the current rootfs instance: itemId, subjectId, instance, manifestDigest, version, preinstalled,
   state `eActive`.
2. On update (`StartInstance` for a new instance, see [Start instance](#start-instance)):
   1. the manifest and the update type are validated. On error the previous update is kept;
   2. the previous update is cleared: `update_state` is removed (synced), then the previous image, temporary files and
      the previous pending instance are removed;
   3. the image is copied to `image.squashfs.tmp`, verified against the layer descriptor (size and sha256 calculated by
      the file info provider), synced and renamed to `image.squashfs`;
   4. SM storage adds the pending instance with state `eActivating`;
   5. `update_state` is created with `state=prepared` and the update type. This is the **commit point**: before it,
      initramfs does nothing;
   6. reboot is requested.
3. Initramfs `aosupdate` is triggered on boot:
   1. `state=prepared`:
      1. mount the update image;
      2. update the state file: `state=trial`;
      3. sync;
      4. continue boot.

      On error: `state=failed` with the reason, boot the installed rootfs.
   2. `state=trial` (SM didn't confirm before reboot): `state=reverted`, `reason=update not confirmed`, sync, boot the
      installed rootfs.
   3. `state=rejected`: `state=reverted` (the SM reason is kept), sync, boot the installed rootfs.
   4. `state=confirmed` or `state=applying`: mount the update image, `state=applying` (the reason is kept), rsync the
      image into the rootfs, remount it read-only (flushes the rootfs), `state=applied`, sync. On error before
      `applying`: `state=failed` with the reason, the rootfs is not modified. On error after it: keep `applying`, set
      the reason, sync. The apply is retried on next boot.
   5. Any other state or no file: no action.
4. SM starts in trial boot (`state=trial`):
   1. the version file must match the pending version, otherwise the update is rejected with `rootfs version mismatch`;
   2. health check OK: `state=confirmed`, request reboot. The pending instance stays `eActivating`;
   3. health check failed: `state=rejected` with the reason, pending instance `eFailed` with the error and removed from
      storage, request reboot.
5. SM starts after apply (`state=applied`):
   1. the version file must match the pending version, otherwise the update is failed, and the current instance is
      restored from the version file if it doesn't match it either. If the version file can't be read, the state is
      kept and processed again on next start, both instances are reported as `eFailed` with the error, and SM start
      doesn't fail;
   2. the old current instance is removed from storage, then the pending instance becomes current (`eActive`);
   3. remove `update_state` (synced), then the image;
   4. report the old instance as `eInactive` and the new one as `eActive`.
6. SM starts after failure (`state=reverted` or `state=failed`):
   1. pending instance `eFailed` with the reason, removed from storage;
   2. remove `update_state`, then the image;
   3. report the pending instance as `eFailed` and the current one as `eActive`.

```mermaid
sequenceDiagram
    autonumber
    participant L as Launcher
    participant R as Rootfs runtime
    participant S as SM storage
    participant W as Working dir
    participant I as Initramfs

    L->>R: StartInstance(new rootfs)
    R->>W: remove previous update_state, then images
    R->>S: remove previous pending instance
    R->>W: image.squashfs (copied, verified, synced)
    R->>S: add pending instance (eActivating)
    R->>W: state=prepared (commit point)
    R-->>L: Activating, reboot required
    Note over I: reboot
    I->>W: mount image, state=trial, sync
    Note over R: trial boot, SM starts
    R->>R: version check, health check
    R->>W: state=confirmed
    R-->>L: Activating, reboot required
    Note over I: reboot
    I->>W: state=applying, rsync image, remount ro, state=applied, sync
    Note over R: SM starts
    R->>R: version check
    R->>S: remove old current, pending -> current
    R->>W: remove update_state, then image
    R-->>L: old Inactive, new Active
```

If the health check or the version check fails, SM writes `rejected` with the reason, reports the pending instance as
`eFailed`, removes it from storage and requests reboot. Initramfs reverts the update on the next boot, and SM clears
the update artifacts.

### Start instance

`StartInstance` returns the known status without any action if the instance is the current or the pending one:

- if the pending instance failed, but its update state is not finished yet (rejected and waiting for reboot, apply
  failed, or the applied rootfs can't be verified), the update error is returned as well: the launcher treats a
  successful start as an active instance. A failed apply is retried by initramfs on next boot, not by
  `StartInstance`. After a failed apply or if the applied rootfs can't be verified, the current instance is reported
  as `eFailed` with the update error as well: the rootfs may be partially updated or is not the current one;
- if a reboot is still required, the reboot request is sent again. The launcher skips duplicated requests.

Otherwise, including the failed pending instance, the update is prepared. An instance with the ident and the version of
the current instance but a different manifest digest is rejected with `eInvalidArgument`: the storage key doesn't
include the manifest digest, and the trial boot is verified by the version only. If the current instance is restored
from the version file, it has no manifest digest: such an instance is the installed rootfs, `eAlreadyExist` is returned
and the launcher reports the instance as active. The media type of the first image layer defines the update type:
`[application/]vnd.aos.image.component.<full|inc>.v<N>[+<suffix>]`. After a failed apply or if the applied rootfs
can't be verified, any update is rejected with `eWrongState` before the previous update is changed: initramfs needs
the `applying` state and its image to recover the partially updated rootfs. Removing them before the new update is
committed leaves the rootfs without recovery if the new update fails, and clears the unverified rootfs condition.

Only one update is stored, and the update in trial boot is finalized before the next one:

- `trial`, `confirmed`, `rejected`: the trial rootfs is running, and the verdict or the reboot is on its way. The new
  update is deferred: nothing is changed, the instance is reported as `eActivating` and no error is returned. The
  launcher starts its stored instances on every SM start, so the new update is prepared on the start after the
  previous one is finished (promoted, reverted or failed);
- `applying` or the applied rootfs that can't be verified: the new update is rejected, see above;
- `prepared` or no update: the new update replaces the previous one. A prepared update isn't started yet.

The manifest and the update type are validated before the previous update is changed: an invalid update keeps the
previous one. Then the previous update is cleared and the new one is written, see [Update flow](#update-flow). If a
later step fails, artifacts and the pending instance are removed and the instance is reported as failed. If the state
file can't be removed, the pending instance is kept: the state may survive power loss, and the update stays tracked. If
it is the state of the previous prepared update, its reboot request is kept as well. If the pending instance can't be
removed, it is kept and removed by the next update or on next start. If only the reboot request fails, the prepared
update is kept, the error is returned and the request is sent again on the next `StartInstance` for this instance.

### Health check verdict

The health check waits until all configured systemd units are active or any of them failed, with retries (5 attempts,
10 seconds to 1 minute delay). It is started after `Start` sends its statuses, so the health check result is always
received after them.

The verdict is a single state change from `trial` to `confirmed` or `rejected`. An SM restart before the verdict is
stored runs the health check again. If `confirmed` can't be stored, `rejected` is stored instead. In any case the reboot
is requested: the state is either `trial` or `rejected`, which initramfs reverts, or `confirmed` that was renamed but
whose store failed later, which initramfs applies and SM promotes on the next start.

A new update is deferred in trial boot, so the verdict is never replaced by it. The verdict is discarded if the pending
instance or the state is changed anyway.

## Start processing

On start, SM loads the current and pending instances from storage and the update state file, and reconciles them.
Storage and state file lookup errors are returned as errors and are never treated as missing data: a wrong decision
may cancel the update that is being applied.

```mermaid
flowchart TD
    start([SM start]) --> file{valid update_state?}

    file -- no --> pendNoFile{pending instance?}
    pendNoFile -- no --> idle[current Active,<br/>remove stale artifacts]
    pendNoFile -- eActivating --> notStarted[pending Failed: update is not started,<br/>remove artifacts]
    pendNoFile -- eFailed --> reportFailed[report Failed,<br/>remove pending]

    file -- yes --> pend{pending instance?}
    pend -- no --> untracked[untracked state]
    pend -- yes --> st{state}

    st -- prepared, rejected,<br/>confirmed --> wait[report storage state,<br/>request reboot again]
    st -- trial --> trial[version check,<br/>health check]
    st -- applying --> applyFailed[pending Failed with reason,<br/>keep state, no reboot]
    st -- applied --> promote[promote]
    st -- reverted, failed --> failed[pending Failed with reason,<br/>remove pending, clear artifacts]
```

| `update_state`       | Pending       | Action                                                                  |
| -------------------- | ------------- | ----------------------------------------------------------------------- |
| none or invalid      | none          | Current `eActive`, remove stale image and temporary files.              |
| none or invalid      | `eActivating` | Not started: pending `eFailed` with `update is not started`, removed.   |
| none or invalid      | `eFailed`     | Not removed before: report `eFailed`, remove the pending instance.      |
| `prepared`           | present       | SM restarted before reboot: report storage state, request reboot again. |
| `rejected`           | present       | Pending not removed before: report `eFailed`, remove it, reboot again.  |
| `confirmed`          | present       | SM restarted before reboot: report storage state, request reboot again. |
| `trial`              | present       | Version check and health check.                                         |
| `applying`           | present       | Apply failed: pending `eFailed` with the reason, state kept, no reboot. |
| `applied`            | present       | Version check, then promote, see [Update flow](#update-flow).           |
| `reverted`, `failed` | present       | Pending `eFailed`, remove it, clear artifacts.                          |
| any                  | none          | See [Untracked state](#untracked-state).                                |

Initramfs always changes `prepared`, `confirmed` and `rejected` on boot. These states on SM start mean that SM was
restarted before the reboot. `applying` on SM start means that initramfs failed to apply the update, and the rootfs may
be partially updated. In this case SM doesn't request reboot to avoid a reboot loop. If SM was interrupted after
`rejected` is stored but before the pending instance is removed, it is reported as failed and removed on start.

The state file is written atomically, so it is invalid (unknown state) only if the filesystem is damaged. An invalid
state is treated as no update.

If a promotion storage call fails, `Start` returns the error and the promotion is repeated on the next start. If the
old current instance is already removed, there is no current instance in storage. On start, a missing current instance
with the pending instance, `state=applied` and the version file matching the pending version means an interrupted
promotion: the default current instance is not created, the pending instance is promoted. In `trial`, `rejected`,
`confirmed` and `applying` states the version file doesn't belong to the installed rootfs (it is the update image or a
partially updated rootfs): the default current instance is not created and is not reported until the installed rootfs
is booted. In `applied` state with the version file that can't be read, the default current instance is not created
either: the state is processed again on next start. Otherwise, the default current instance is created from the version
file.

Several pending instances in storage are left if a failed pending instance can't be removed and SM is restarted. The
tracked one can't be distinguished: all pending instances are removed, and the update state is processed as untracked.

### Untracked state

The state file exists, but there is no pending instance: the promotion was interrupted after the storage update, or
storage was lost:

- `confirmed`, `applying`: the rootfs may be partially updated, cancellation is not safe. The state is kept, initramfs
  applies the update. In `confirmed` state the trial rootfs is running: a reboot is requested to apply the update. After
  a failed apply a reboot is not requested to avoid a reboot loop;
- `applied`: if the version file doesn't match the current instance, the current instance is restored from the version
  file with the default ident, and the cloud sends the desired instance again. The state is removed after the current
  instance is restored: if restoring fails or is interrupted, it is repeated on next start. If the version file can't
  be read, the state is kept and the current instance is reported as `eFailed` with the error;
- `trial`, `rejected`: the trial rootfs may be running: `trial` is replaced with `rejected`, the state is kept and a
  reboot is requested, so initramfs reverts the update and the installed rootfs is booted. An SM restart before the
  reboot requests it again. If `rejected` can't be stored, initramfs reverts `trial` as well;
- `prepared`, `reverted`, `failed`: the state is removed.

## Power loss scenarios

| Interrupted at                | State after                       | Result                                         |
| ----------------------------- | --------------------------------- | ---------------------------------------------- |
| previous update cleared       | no state                          | Previous update dropped, see limitations.      |
| image copy                    | temporary image only              | Temporary image removed, no update.            |
| pending stored, no `prepared` | pending `eActivating`, no state   | Pending `eFailed`, image removed.              |
| `prepared`, before reboot     | `prepared`                        | SM restart: reboot requested again.            |
| mount, before `trial`         | `prepared`                        | Next boot repeats the mount.                   |
| after `trial`, before verdict | `trial`                           | Next boot: initramfs reverts, update failed.   |
| verdict write                 | `trial` or the verdict (atomic)   | Revert or the verdict, both consistent.        |
| before pending removal        | `rejected`, pending `eActivating` | Pending removed on SM restart or after revert. |
| initramfs revert              | `trial` or `rejected`             | Revert repeated on next boot.                  |
| apply, before `applying`      | `confirmed`                       | Next boot repeats the apply.                   |
| during rsync                  | `applying`                        | Next boot repeats the apply.                   |
| rsync, before `applied`       | `applying`                        | Apply repeated, rsync is idempotent.           |
| between promotion calls       | pending only, `applied`           | Promotion repeated, old instance not reported. |
| promoted, state not removed   | `applied`, no pending             | Untracked `applied`: state removed.            |
| state → image removal         | stale image                       | Image removed on next start.                   |
| failure handling              | `reverted` or `failed`            | Failure handling repeated, it is idempotent.   |
| new update over failed apply  | `applying`                        | Rejected, `applying` and its image are kept.   |

## Durability

1. **Each storage call is atomic and durable on return**, no transactions are used. Storage is durable before the state
   file is written. The SM database sets `synchronous=FULL` in the default rollback journal mode. In this mode the
   journal removal that commits a transaction is not followed by a directory sync (only `synchronous=EXTRA` does it): a
   power loss right after the commit may roll back the last transaction on a filesystem that doesn't order the removal
   before later syncs of the working directory.
2. **Storage calls are ordered, so every intermediate state is recoverable**:
   - promotion removes the old current instance first, then makes the pending one current. An interruption leaves no
     current instance, which is detected on start and the promotion is repeated. The reverse order may leave two
     current instances that can't be distinguished;
   - restoring the default current instance removes the old one first, then adds the default one. An interruption
     leaves no current instance, and the default one is created on start;
   - preparing an update removes the previous pending instance after the previous state file, then adds the new one.
     There is at most one tracked pending instance: a pending instance that can't be removed is removed by the next
     update or on next start.
3. **Storage and working directory are located on the persistent data partition**, not on the rootfs. During trial
   boot the rootfs is the update image or an overlay: storage on the rootfs is lost on revert or taken from the image.
4. **Ordering rule**: on update, *storage first, state file last* (the state file is the commit point); on cleanup,
   *state file before image* (initramfs stops acting first). Failure handling removes the failed pending instance
   before the state file: an interruption leaves `rejected`, `reverted` or `failed` without the pending instance, which
   is processed as untracked state, only the failure reason is lost.
5. **Initramfs syncs every state change** before it continues boot.
6. **The image is never removed while the state file exists**: initramfs fails the update without the image.

All files are written atomically: written to `<file>.tmp` (permissions `0600`), fsynced with write and close errors
checked, renamed, and the working directory is fsynced. Artifacts are cleared in two steps:

1. the state file is removed and the working directory is synced. Cleanup stops if this step fails;
2. images and temporary files are removed and the working directory is synced. Errors don't stop the step, the first
   error is returned.

## Reboot requests

Reboots are requested through `InstanceStatusReceiverItf::RebootRequired`. The launcher reboots the node when no launch
is in progress.

While processing the update state, the runtime only sets the reboot required flag. The request is sent by `Start`,
`StartInstance` and the health check thread after the runtime lock is released. If a reboot request fails:

- the error is reported in the instance status;
- the runtime keeps the reboot required flag, and the next `StartInstance` sends the request again.

A reboot is also requested if the trial rootfs is running without tracked update: on untracked `trial`, `rejected` and
`confirmed` states. A reboot is not requested when initramfs failed to apply the update (to avoid a reboot loop).

## Concurrency

- `mMutex` protects the runtime state. All runtime files and storage records are changed under it.
- The status receiver (statuses and reboot requests) is never called under `mMutex`: the launcher calls the runtime
  under its own lock, so calling the launcher under `mMutex` would create a lock order inversion.
- The health check runs in a separate thread, started by `Start` after its statuses are sent. The thread doesn't hold
  `mMutex` during the check, takes it to store the verdict and requests the reboot after releasing it.
- The health check runs only in `trial` state, and `StartInstance` doesn't change the state in trial boot, so it
  doesn't wait for the health check.
- `mHealthCheckMutex` protects the health check thread object. `JoinHealthCheck` is called without `mMutex`:
  - in `Start` to join the thread of a previous start;
  - in `Stop` and in the destructor: destroying a joinable thread terminates the process.

## Statuses

| Situation                         | Pending instance     | Current instance |
| --------------------------------- | -------------------- | ---------------- |
| no update                         | -                    | Active           |
| update prepared or waiting reboot | Activating           | -                |
| trial boot, health check running  | Activating           | -                |
| update rejected, waiting reboot   | Failed (reason)      | -                |
| initramfs apply failed            | Failed (reason)      | -                |
| update failed or reverted         | Failed (reason)      | Active           |
| update promoted                   | Active (new current) | Inactive (old)   |

A new update deferred in trial boot is reported by the runtime as `eActivating` until it is prepared on a later start,
the same as a prepared update. The launcher treats the successful `StartInstance` as an active instance in both cases.
After a failed apply, `StartInstance` for the current instance reports it as `eFailed` with the update error. If the
version file of the applied rootfs can't be read, both instances are reported as `eFailed` with the read error, on start
and on `StartInstance`, until the version file is readable on a later start.

## Design decisions

- The runtime uses its own storage table (`runtimeInstances`), separate from the launcher instances. The runtime
  removes the previous current instance itself on promotion.
- Storage transactions are not used: ordered atomic storage calls are enough to recover every interruption.
- A failed pending instance is removed from storage: the failure is reported once, and `StartInstance` for the same
  instance retries the update. Retry policy belongs to the cloud. A failed apply is kept and not retried by
  `StartInstance`: the rootfs may be partially updated, and initramfs retries the apply.
- Only one update image is stored, and only one update is in progress. The update in trial boot is finalized before
  the next one is prepared: changing it would require a rejected state without the image and an extra reboot path.
  Other updates are replaced: they don't run the trial rootfs. The working directory needs space for one image.
- Boot ID is not tracked: the update state alone defines the action on start.
- No update is accepted after a failed apply or if the applied rootfs can't be verified: the `applying` state and its
  image are the only recovery path of the partially updated rootfs. Replacing them requires staging and committing the
  new update without a window where neither recovery path exists.

## Limitations

- If initramfs doesn't process the update state (e.g. aosupdate module is disabled), every SM start requests a reboot.
- After a failed apply the node boots the partially updated rootfs until initramfs applies the update. If the apply
  always fails, the node can't be updated remotely. The same applies to the applied rootfs whose version file can't be
  read, until it is readable.
- A new update during trial boot is prepared only after the trial update is finished: a confirmed update is applied and
  promoted first, which takes the apply and an extra reboot.
- If the verdict can't be stored, the update is reported as failed, but it may still be applied and promoted after the
  reboot.
- If storage is lost during an update, the current instance is restored from the version file with the default ident.
- A rootfs image can't be updated without changing its version.
- The failure of an update isn't reported after SM restart.
- The launcher starts its stored instances on every SM start. A failed update stays in the launcher storage, so it is
  retried on every SM start: an update that always fails reboots the node on every start until the cloud changes the
  desired instance.
- If the promotion is interrupted after the old current instance is removed, the old instance is not reported as
  `eInactive` on the repeated promotion.
