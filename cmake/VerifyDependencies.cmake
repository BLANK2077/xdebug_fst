function(xdebug_require_git_revision dependency_name repository_dir expected_revision)
    if(NOT IS_DIRECTORY "${repository_dir}/.git")
        message(FATAL_ERROR
            "${dependency_name} repository is missing at ${repository_dir}")
    endif()

    execute_process(
        COMMAND git -C "${repository_dir}" rev-parse HEAD
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE actual_revision
        ERROR_VARIABLE git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot read ${dependency_name} revision at ${repository_dir}: ${git_error}")
    endif()
    if(NOT actual_revision STREQUAL expected_revision)
        message(FATAL_ERROR
            "${dependency_name} revision mismatch: expected ${expected_revision}, "
            "found ${actual_revision}. Update the dependency deliberately and refresh "
            "dependencies.lock.json plus cmake/DependenciesLock.cmake together.")
    endif()
endfunction()

function(xdebug_require_file_hash artifact_name artifact_path expected_hash)
    if(NOT EXISTS "${artifact_path}")
        message(FATAL_ERROR "${artifact_name} is missing at ${artifact_path}")
    endif()
    file(SHA256 "${artifact_path}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "${artifact_name} hash mismatch: expected ${expected_hash}, found ${actual_hash} "
            "at ${artifact_path}")
    endif()
endfunction()
