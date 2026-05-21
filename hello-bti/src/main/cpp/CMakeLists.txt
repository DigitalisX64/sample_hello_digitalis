cmake_minimum_required(VERSION 3.22.1)
project(hellobti LANGUAGES CXX)

add_library(hellobti SHARED native-lib.cpp)

# Armv8.5-a unlocks BTI (Branch Target Identification).
# `-mbranch-protection=bti` makes clang emit a BTI guard at the entry of
# every function that can be reached via an indirect branch or call:
#   BTI c     guards an indirect-call landing pad (BLR target).
#   BTI j     guards an indirect-branch landing pad (BR target).
#   BTI jc    guards both.
#   BTI       (no target) is reserved and decodes as plain NOP.
# On Digitalis (no real BTI enforcement) these must all NOP — if the
# decoder ever routes any of HINT #32-#39 to Undefined() the process
# SIGILLs the moment a guarded entry point is hit.  This sample
# additionally issues the four mnemonics as inline-asm probes to
# exercise the encodings even on toolchains where the auto-inserted
# guards might use only a subset.
target_compile_options(hellobti PRIVATE
    -march=armv8.5-a
    -mbranch-protection=bti)

# Tag the .so as BTI-capable so the loader/linker can verify the
# property (BIND_NOW + GNU_PROPERTY_AARCH64_FEATURE_1_BTI).
target_link_options(hellobti PRIVATE
    -Wl,-z,force-bti)

target_link_libraries(hellobti
    PRIVATE
    log
)
