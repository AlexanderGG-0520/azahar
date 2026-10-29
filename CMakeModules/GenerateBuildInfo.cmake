macro(generate_build_info)
    find_package(Git QUIET)

    # Gets a UTC timstamp and sets the provided variable to it
    function(get_timestamp _var)
        string(TIMESTAMP timestamp UTC)
        set(${_var} "${timestamp}" PARENT_SCOPE)
    endfunction()
    get_timestamp(BUILD_DATE)

    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/cmake-modules")

    if (EXISTS "${CMAKE_SOURCE_DIR}/GIT-COMMIT" AND EXISTS "${CMAKE_SOURCE_DIR}/GIT-TAG")
        file(READ "${CMAKE_SOURCE_DIR}/GIT-COMMIT" GIT_REV_RAW LIMIT 64)
        string(STRIP "${GIT_REV_RAW}" GIT_REV)
        string(SUBSTRING "${GIT_REV_RAW}" 0 9 GIT_DESC)
        set(GIT_BRANCH "HEAD")
    elseif (EXISTS "${CMAKE_SOURCE_DIR}/.git")
        # Query Git directly. GetGitRevisionDescription.cmake assumes a non-directory .git
        # entry is a submodule pointer, but worktrees store a gitdir pointer there instead.
        if (GIT_FOUND)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE GIT_REV_RESULT
                OUTPUT_VARIABLE GIT_REV
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --long --dirty
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE GIT_DESC_RESULT
                OUTPUT_VARIABLE GIT_DESC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE GIT_BRANCH_RESULT
                OUTPUT_VARIABLE GIT_BRANCH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)

            if (NOT GIT_REV_RESULT EQUAL 0 OR NOT GIT_DESC_RESULT EQUAL 0 OR
                NOT GIT_BRANCH_RESULT EQUAL 0)
                set(GIT_REV "UNKNOWN")
                set(GIT_DESC "UNKNOWN")
                set(GIT_BRANCH "UNKNOWN")
            elseif (DEFINED ENV{CITRA_USE_TAG_AS_VERSION})
                execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    RESULT_VARIABLE GIT_TAG_RESULT
                    OUTPUT_VARIABLE GIT_TAG
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
                if (NOT GIT_TAG_RESULT EQUAL 0)
                    unset(GIT_TAG)
                endif()
            endif()
        else()
            set(GIT_REV "UNKNOWN")
            set(GIT_DESC "UNKNOWN")
            set(GIT_BRANCH "UNKNOWN")
        endif()
    else()
        # self-packed archive?
        set(GIT_REV "UNKNOWN")
        set(GIT_DESC "UNKNOWN")
        set(GIT_BRANCH "UNKNOWN")
    endif()
    string(SUBSTRING "${GIT_REV}" 0 7 GIT_SHORT_REV)

    # Set build version
    set(REPO_NAME "")
    set(BUILD_VERSION "0")
    set(BUILD_FULLNAME "${GIT_SHORT_REV}")
    if (NOT DEFINED GIT_TAG)
        if ((DEFINED ENV{CI} AND DEFINED ENV{GITHUB_ACTIONS}))
            if ($ENV{GITHUB_REF_TYPE} STREQUAL "tag")
                set(GIT_TAG $ENV{GITHUB_REF_NAME})
            endif()
        elseif (EXISTS "${CMAKE_SOURCE_DIR}/GIT-COMMIT" AND EXISTS "${CMAKE_SOURCE_DIR}/GIT-TAG")
            file(READ "${CMAKE_SOURCE_DIR}/GIT-TAG" GIT_TAG)
            string(STRIP ${GIT_TAG} GIT_TAG)
        endif()
    endif()

    if (DEFINED GIT_TAG AND NOT "${GIT_TAG}" STREQUAL "unknown")
        set(BUILD_VERSION "${GIT_TAG}")
        set(BUILD_FULLNAME "${BUILD_VERSION}")
    endif()
endmacro()
