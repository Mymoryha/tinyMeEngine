if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM is required.")
endif()
get_filename_component(app_name "${PROGRAM}" NAME_WE)
execute_process(
    COMMAND "${PROGRAM}" --windowed --frames 8 --gpu-validation
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    TIMEOUT 30
)
if(NOT "${actual_exit}" STREQUAL "0" OR NOT actual_error STREQUAL "")
    message(FATAL_ERROR "Windowed CLI failed: ${actual_exit}\n${actual_output}\n${actual_error}")
endif()
if(NOT actual_output MATCHES "selected_adapter=\"NVIDIA GeForce RTX 5080\""
        OR NOT actual_output MATCHES "gpu_validation=1"
        OR NOT actual_output MATCHES "gpu_idle=1 device_removed=0 validation_warnings=0 validation_errors=0 live_child_check=1"
        OR NOT actual_output MATCHES "${app_name} mode=windowed-gpu presented_frames=8 closed_by_user=0 validation_warnings=0")
    message(FATAL_ERROR "Unexpected GPU/CLI validation output:\n${actual_output}")
endif()
if(NOT actual_output MATCHES "experiment=triangle" OR NOT actual_output MATCHES "shader_reload success=1 generation=1")
    message(FATAL_ERROR "Default triangle was not compiled/drawn:\n${actual_output}")
endif()
execute_process(COMMAND "${PROGRAM}" --windowed --experiment clear --frames 8 --gpu-validation
    RESULT_VARIABLE clear_exit OUTPUT_VARIABLE clear_output ERROR_VARIABLE clear_error TIMEOUT 30)
if(NOT "${clear_exit}" STREQUAL "0" OR NOT clear_error STREQUAL ""
        OR NOT clear_output MATCHES "experiment=clear"
        OR clear_output MATCHES "shader_reload"
        OR NOT clear_output MATCHES "validation_warnings=0 validation_errors=0"
        OR NOT clear_output MATCHES "presented_frames=8 closed_by_user=0")
    message(FATAL_ERROR "Clear regression failed:\n${clear_output}\n${clear_error}")
endif()
execute_process(COMMAND "${PROGRAM}" --windowed --shader "${CMAKE_CURRENT_BINARY_DIR}/missing-m2-shader.hlsl" --frames 1
    RESULT_VARIABLE failed_exit OUTPUT_VARIABLE failed_output ERROR_VARIABLE failed_error TIMEOUT 30)
if(NOT "${failed_exit}" STREQUAL "1" OR NOT failed_error MATCHES "Initial triangle shader failed"
        OR NOT failed_output MATCHES "shader_reload success=0 generation=0")
    message(FATAL_ERROR "Initial missing shader must fail clearly:\n${failed_output}\n${failed_error}")
endif()
message(STATUS "${app_name}: RTX 5080, GBV, triangle/clear 8 presentations and initial shader failure passed.")
