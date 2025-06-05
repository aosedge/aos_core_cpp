# ######################################################################################################################
# Internal helper function that creates a target with common configuration.
# ######################################################################################################################
function(_add_target)
    set(options LOG_MODULE)
    set(one_value_args TARGET_NAME STACK_USAGE TARGET_TYPE)
    set(multi_value_args SOURCES DEFINES COMPILE_OPTIONS INCLUDES LIBRARIES)

    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_TARGET_NAME)
        message(FATAL_ERROR "TARGET_NAME parameter is required")
    endif()

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "SOURCES parameter is required")
    endif()

    if(NOT ARG_TARGET_TYPE)
        message(FATAL_ERROR "TARGET_TYPE parameter is required")
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

    if("${ARG_TARGET_TYPE}" STREQUAL "STATIC")
        add_library(${TARGET} STATIC ${ARG_SOURCES})
        add_library("${TARGET_NAMESPACE}::${ARG_TARGET_NAME}" ALIAS ${TARGET})
    elseif("${ARG_TARGET_TYPE}" STREQUAL "TEST")
        add_executable(${TARGET} ${ARG_SOURCES})
        add_executable("${TARGET_NAMESPACE}::${ARG_TARGET_NAME}" ALIAS ${TARGET})
        gtest_discover_tests(${TARGET})
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

    if(ARG_INCLUDES)
        target_include_directories(${TARGET} PUBLIC ${ARG_INCLUDES})
    endif()

    # link libraries
    if(ARG_LIBRARIES)
        target_link_libraries(${TARGET} PUBLIC ${ARG_LIBRARIES})
    endif()
endfunction()

# ######################################################################################################################
# This function creates a static library target.
#
# add_module(
#     TARGET_NAME <name>     - name of module;
#     LOG_MODULE             - if set, defines the LOG_MODULE for the target (optional);
#     STACK_USAGE <value>    - stack usage for the target (optional);
#     DEFINES <list>         - list of preprocessor definitions (optional);
#     COMPILE_OPTIONS <list> - list of compiler options (optional);
#     INCLUDES <list>        - list of include directories (optional);
#     SOURCES <list>         - list of source files;
#     LIBRARIES <list>       - list of libraries to link against (optional).
# )
#
# The following public variables are used:
#   - TARGET_PREFIX    - prefix for the target name, default is "aos";
#   - TARGET_NAMESPACE - namespace for the target alias, default is "aos".
#
# This function set TARGET and make it available in the parent scope.
# ######################################################################################################################
function(add_module)
    _add_target(TARGET_TYPE STATIC ${ARGN})

    set(TARGET
        ${TARGET}
        PARENT_SCOPE
    )
endfunction()

# ######################################################################################################################
# This function creates a test binary target.
#
# add_test(
#     TARGET_NAME <name>     - name of test;
#     LOG_MODULE             - if set, defines the LOG_MODULE for the target (optional);
#     STACK_USAGE <value>    - stack usage for the target (optional);
#     DEFINES <list>         - list of preprocessor definitions (optional);
#     COMPILE_OPTIONS <list> - list of compiler options (optional);
#     INCLUDES <list>        - list of include directories (optional);
#     SOURCES <list>         - list of source files;
#     LIBRARIES <list>       - list of libraries to link against (optional).
# )
#
# The following public variables are used:
#   - TARGET_PREFIX    - prefix for the target name, default is "aos";
#   - TARGET_NAMESPACE - namespace for the target alias, default is "aos".
#
# This function set TARGET and make it available in the parent scope.
# ######################################################################################################################
function(add_test)
    _add_target(TARGET_TYPE TEST ${ARGN})

    set(TARGET
        ${TARGET}
        PARENT_SCOPE
    )
endfunction()
