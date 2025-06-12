# ######################################################################################################################
# Internal helper function that creates a target with common configuration.
#
# _add_target(
#     TARGET_NAME <name>     - name of test;
#     TARGET_TYPE <type>     - type of target, can be STATIC, TEST or EXECUTABLE;
#     LOG_MODULE             - if set, defines the LOG_MODULE for the target (optional);
#     STACK_USAGE <value>    - stack usage for the target (optional);
#     DEFINES <list>         - list of preprocessor definitions (optional);
#     COMPILE_OPTIONS <list> - list of compiler options (optional);
#     INCLUDES <list>        - list of include directories (optional);
#     SOURCES <list>         - list of source files;
#     LINK_OPTIONS <list>    - list of link options (optional);
#     LIBRARIES <list>       - list of libraries to link against (optional).
# )
#
# The following public variables are used:
#   - TARGET_PREFIX    - prefix for the target name, default is "aos";
#   - TARGET_NAMESPACE - namespace for the target alias, default is "aos";
#   - TARGET_INCLUDES  - common includes that will be added to public target scope.
#
# This function set TARGET variable and make it available in the parent scope.
# ######################################################################################################################
function(_add_target)
    set(options LOG_MODULE)
    set(one_value_args TARGET_NAME STACK_USAGE TARGET_TYPE)
    set(multi_value_args SOURCES DEFINES COMPILE_OPTIONS INCLUDES LINK_OPTIONS LIBRARIES)

    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_TARGET_NAME)
        message(FATAL_ERROR "TARGET_NAME parameter is required")
    endif()

    if(NOT ARG_TARGET_TYPE)
        message(FATAL_ERROR "TARGET_TYPE parameter is required")
    endif()

    if(NOT ARG_SOURCES AND NOT ("${ARG_TARGET_TYPE}" STREQUAL "STATIC"))
        message(FATAL_ERROR "SOURCE parameter is required")
    endif()

    # set default values for external variables

    if(NOT TARGET_PREFIX)
        set(TARGET_PREFIX "aos")
    endif()

    if(NOT TARGET_NAMESPACE)
        set(TARGET_NAMESPACE "aos")
    endif()

    # create target

    set(TARGET "${TARGET_PREFIX}_${ARG_TARGET_NAME}")

    set(TARGET
        "${TARGET}"
        PARENT_SCOPE
    )

    set(TARGET_SCOPE "PUBLIC")

    if("${ARG_TARGET_TYPE}" STREQUAL "STATIC")
        if(ARG_SOURCES)
            add_library(${TARGET} STATIC ${ARG_SOURCES})
        else()
            set(TARGET_SCOPE "INTERFACE")

            add_library(${TARGET} INTERFACE)
        endif()

        add_library("${TARGET_NAMESPACE}::${ARG_TARGET_NAME}" ALIAS ${TARGET})
    elseif("${ARG_TARGET_TYPE}" STREQUAL "TEST")
        add_executable(${TARGET} ${ARG_SOURCES})
        add_executable("${TARGET_NAMESPACE}::${ARG_TARGET_NAME}" ALIAS ${TARGET})
        gtest_discover_tests(${TARGET})
    elseif("${ARG_TARGET_TYPE}" STREQUAL "EXECUTABLE")
        add_executable(${TARGET} ${ARG_SOURCES})
        add_executable("${TARGET_NAMESPACE}::${ARG_TARGET_NAME}" ALIAS ${TARGET})
    else()
        message(FATAL_ERROR "Unsupported TARGET_TYPE: ${ARG_TARGET_TYPE}")
    endif()

    # set stack usage

    if(ARG_STACK_USAGE)
        target_compile_options(${TARGET} PRIVATE -Wstack-usage=${ARG_STACK_USAGE})
    endif()

    # set compile options

    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${TARGET} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()

    # set log module

    if(ARG_LOG_MODULE)
        target_compile_definitions(${TARGET} PRIVATE "LOG_MODULE=\"${ARG_TARGET_NAME}\"")
    endif()

    # add defines

    if(ARG_DEFINES)
        target_compile_definitions(${TARGET} PRIVATE ${ARG_DEFINES})
    endif()

    # add includes

    if(ARG_INCLUDES OR TARGET_INCLUDES)
        target_include_directories(${TARGET} ${TARGET_SCOPE} ${TARGET_INCLUDES} ${ARG_INCLUDES})
    endif()

    # link options
    if(ARG_LINK_OPTIONS)
        target_link_options(${TARGET} PRIVATE ${ARG_LINK_OPTIONS})
    endif()

    # link libraries
    if(ARG_LIBRARIES)
        target_link_libraries(${TARGET} ${TARGET_SCOPE} ${ARG_LIBRARIES})
    endif()
endfunction()

# ######################################################################################################################
# This function creates static library target.
#
# It calls _add_target with TARGET_TYPE set to STATIC.
# See parameters description in _add_target function.
# ######################################################################################################################
function(add_module)
    _add_target(TARGET_TYPE STATIC ${ARGN})

    set(TARGET
        ${TARGET}
        PARENT_SCOPE
    )
endfunction()

# ######################################################################################################################
# This function creates test binary target.
#
# It calls _add_target with TARGET_TYPE set to TEST.
# See parameters description in _add_target function.
# ######################################################################################################################
function(add_test)
    _add_target(TARGET_TYPE TEST ${ARGN})

    set(TARGET
        ${TARGET}
        PARENT_SCOPE
    )
endfunction()

# ######################################################################################################################
# This function creates executable binary target.
#
# It calls _add_target with TARGET_TYPE set to EXECUTABLE.
# See parameters description in _add_target function.
# ######################################################################################################################
function(add_exec)
    _add_target(TARGET_TYPE EXECUTABLE ${ARGN})

    set(TARGET
        ${TARGET}
        PARENT_SCOPE
    )
endfunction()
