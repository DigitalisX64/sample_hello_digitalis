# spv_to_header.cmake — Convert a SPIR-V binary file to a C uint32_t array header.
# Usage: cmake -DSPV_FILE=<input.spv> -DHEADER_FILE=<output.h> -DVAR_NAME=<name> -P spv_to_header.cmake

file(READ ${SPV_FILE} SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_HEX_LEN)

# Each uint32_t is 8 hex chars (4 bytes). SPIR-V is little-endian on disk.
math(EXPR NUM_WORDS "${SPV_HEX_LEN} / 8")

set(ARRAY_BODY "")
set(COL 0)
math(EXPR LAST_WORD "${NUM_WORDS} - 1")
foreach(I RANGE 0 ${LAST_WORD})
    math(EXPR OFFSET "${I} * 8")
    # Read 4 bytes in file order (little-endian on disk).
    # Swap byte order to get the uint32_t value: AB CD EF GH -> GHEFCDAB
    string(SUBSTRING "${SPV_HEX}" ${OFFSET} 2 B0)
    math(EXPR OFF1 "${OFFSET} + 2")
    string(SUBSTRING "${SPV_HEX}" ${OFF1} 2 B1)
    math(EXPR OFF2 "${OFFSET} + 4")
    string(SUBSTRING "${SPV_HEX}" ${OFF2} 2 B2)
    math(EXPR OFF3 "${OFFSET} + 6")
    string(SUBSTRING "${SPV_HEX}" ${OFF3} 2 B3)
    set(WORD "0x${B3}${B2}${B1}${B0}")

    if(I LESS LAST_WORD)
        string(APPEND ARRAY_BODY "${WORD}, ")
    else()
        string(APPEND ARRAY_BODY "${WORD}")
    endif()

    math(EXPR COL "${COL} + 1")
    if(COL EQUAL 4)
        string(APPEND ARRAY_BODY "\n    ")
        set(COL 0)
    endif()
endforeach()

file(WRITE ${HEADER_FILE}
"// Auto-generated from ${VAR_NAME} shader. Do not edit.\n"
"#pragma once\n"
"#include <cstdint>\n"
"\n"
"static const uint32_t k_${VAR_NAME}_spv[] = {\n"
"    ${ARRAY_BODY}\n"
"};\n"
)
