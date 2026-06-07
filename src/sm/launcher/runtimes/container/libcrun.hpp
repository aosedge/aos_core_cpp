/*
 * crun - OCI runtime written in C
 *
 * Copyright (C) 2017, 2018, 2019 Giuseppe Scrivano <giuseppe@scrivano.org>
 * crun is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * crun is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with crun.  If not, see <http://www.gnu.org/licenses/>.

 * This header copies declarations for libcrun container types and functions,
 * created from the crun 1.14.3 source code.
 * Struct layout mirrors crun 1.14.3
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_LIBCRUN_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_LIBCRUN_HPP_

#include <stdbool.h>
#include <stdio.h>

extern "C" {

#ifndef LIBCRUN_PUBLIC
#define LIBCRUN_PUBLIC __attribute__((visibility("default")))
#endif

struct libcrun_error_s {
    int   status;
    char* msg;
};
typedef struct libcrun_error_s* libcrun_error_t;

typedef void (*crun_output_handler)(int errno_, const char* msg, bool warning, void* arg);

LIBCRUN_PUBLIC int  libcrun_error_release(libcrun_error_t* err);
LIBCRUN_PUBLIC void libcrun_error_write_warning_and_release(FILE* out, libcrun_error_t** err);

/* Opaque – only used via pointer, no field access needed. */
struct custom_handler_manager_s;

struct libcrun_context_s {
    const char* state_root;
    const char* id;
    const char* bundle;
    const char* console_socket;
    const char* pid_file;
    const char* notify_socket;
    const char* handler;
    int         preserve_fds;
    // For some use-cases we need differentiation between preserve_fds and listen_fds.
    // Following context variable makes sure we get exact value of listen_fds irrespective of preserve_fds.
    int listen_fds;

    crun_output_handler output_handler;
    void*               output_handler_arg;

    int fifo_exec_wait_fd;

    bool systemd_cgroup;
    bool detach;
    bool no_new_keyring;
    bool force_no_cgroup;
    bool no_pivot;

    char** argv;
    int    argc;

    struct custom_handler_manager_s* handler_manager;
};
typedef struct libcrun_context_s libcrun_context_t;

/* Opaque container object – no fields accessed by callers. */
struct libcrun_container_s;
typedef struct libcrun_container_s libcrun_container_t;

/* Opaque OCI spec – passed as NULL in our use cases. */
struct runtime_spec_schema_config_schema_s;
typedef struct runtime_spec_schema_config_schema_s runtime_spec_schema_config_schema;

struct libcrun_container_status_s {
    pid_t              pid;
    unsigned long long process_start_time;
    char*              bundle;
    char*              rootfs;
    char*              cgroup_path;
    char*              scope;
    char*              intelrdt;
    int                systemd_cgroup;
    char*              created;
    int                detached;
    char*              external_descriptors;
    char*              owner;
};
typedef struct libcrun_container_status_s libcrun_container_status_t;

LIBCRUN_PUBLIC libcrun_container_t* libcrun_container_load_from_file(const char* path, libcrun_error_t* err);
LIBCRUN_PUBLIC void                 libcrun_container_free(libcrun_container_t* container);

LIBCRUN_PUBLIC int libcrun_container_run(
    libcrun_context_t* context, libcrun_container_t* container, unsigned int options, libcrun_error_t* error);

LIBCRUN_PUBLIC int libcrun_container_delete(libcrun_context_t* context, runtime_spec_schema_config_schema* def,
    const char* id, bool force, libcrun_error_t* err);

LIBCRUN_PUBLIC int libcrun_container_kill(
    libcrun_context_t* context, const char* id, const char* signal, libcrun_error_t* err);

LIBCRUN_PUBLIC void libcrun_free_container_status(libcrun_container_status_t* status);

LIBCRUN_PUBLIC int libcrun_read_container_status(
    libcrun_container_status_t* status, const char* state_root, const char* id, libcrun_error_t* err);

LIBCRUN_PUBLIC int libcrun_is_container_running(libcrun_container_status_t* status, libcrun_error_t* err);
}

#endif
