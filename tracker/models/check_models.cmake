# Checks the Tracker's models against their pinned SHA-256 (the build runs it: cmake -P).
# A changed upstream archive or a wrong file put here by hand (Windows) fails the build instead
# of shipping silently. After a deliberate model update, put the new hashes here.
set(models
    yolox_n_body_head_hand_192x320.onnx 3455b3aaafae74869d873ec5107142f704c8fedba7bf9b7154e1e4c6b9be5371
    hfhpe_vitt_ypr_kappa_1x3x128x128.onnx 6dc06f19d1cc89a2a72e8663bc246eeb5aaa4888dce914695794d79af00ceb66)

while(models)
    list(POP_FRONT models name expected)
    set(file "${CMAKE_CURRENT_LIST_DIR}/${name}")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing tracker model ${file} (tracker/models/MODELS.md)")
    endif()
    file(SHA256 "${file}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Tracker model ${name} has SHA-256 ${actual}, expected ${expected}")
    endif()
endwhile()
