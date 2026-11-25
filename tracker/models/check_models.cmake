# Checks the Tracker's models against the SHA-256 pinned in models.json (the build runs it:
# cmake -P). A changed upstream archive or a wrong file put here by hand (Windows) fails the
# build instead of shipping silently. After a deliberate model update, put the new hash there.
file(READ "${CMAKE_CURRENT_LIST_DIR}/models.json" models)
string(JSON count LENGTH "${models}")
math(EXPR last "${count} - 1")

foreach(index RANGE ${last})
    string(JSON name GET "${models}" ${index} name)
    string(JSON expected GET "${models}" ${index} sha256)
    set(file "${CMAKE_CURRENT_LIST_DIR}/${name}")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing tracker model ${file} (tracker/models/MODELS.md)")
    endif()
    file(SHA256 "${file}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Tracker model ${name} has SHA-256 ${actual}, expected ${expected}")
    endif()
endforeach()
