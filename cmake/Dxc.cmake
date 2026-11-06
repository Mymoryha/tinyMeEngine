set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tools/dxc-lock.json")
file(READ "${PROJECT_SOURCE_DIR}/tools/dxc-lock.json" tinyme_dxc_lock)
string(JSON tinyme_dxc_version GET "${tinyme_dxc_lock}" version)
set(tinyme_dxc_root "${PROJECT_SOURCE_DIR}/.tools/dxc/${tinyme_dxc_version}")
foreach(pair IN ITEMS "build/native/bin/x64/dxcompiler.dll|compilerSha256"
        "build/native/bin/x64/dxil.dll|validatorSha256" "build/native/include/dxcapi.h|headerSha256")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 relative_path)
    list(GET parts 1 key)
    set(full_path "${tinyme_dxc_root}/${relative_path}")
    if(NOT EXISTS "${full_path}")
        message(FATAL_ERROR "DXC is missing. Run: powershell -NoProfile -File tools/install-dxc.ps1")
    endif()
    file(SHA256 "${full_path}" actual_hash)
    string(JSON expected_hash GET "${tinyme_dxc_lock}" "${key}")
    string(TOUPPER "${actual_hash}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "DXC integrity mismatch: ${relative_path}")
    endif()
endforeach()
message(STATUS "TinyMeEngine DXC: ${tinyme_dxc_version}")

function(tinyme_deploy_dxc target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${tinyme_dxc_root}/build/native/bin/x64/dxcompiler.dll"
            "${tinyme_dxc_root}/build/native/bin/x64/dxil.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    foreach(license IN ITEMS LICENSE-LLVM.txt LICENSE-MS.txt LICENCE-MIT.txt distributable_files.txt)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/dxc"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${tinyme_dxc_root}/${license}"
                "$<TARGET_FILE_DIR:${target}>/licenses/dxc/${license}" VERBATIM)
    endforeach()
endfunction()
