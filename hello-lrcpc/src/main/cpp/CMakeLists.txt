cmake_minimum_required(VERSION 3.22.1)
project(hellolrcpc LANGUAGES CXX)

add_library(hellolrcpc SHARED native-lib.cpp)

# Armv8.3-a unlocks LDAPR/LDAPRB/LDAPRH (RCpc release-consistency loads).
# Armv8.3 implies Armv8.1+Armv8.2, so LDLAR/STLLR (Armv8.1-LOR) are also
# accepted by the assembler under this -march.  LLVM treats LRCPC and LOR
# as architecturally-required at Armv8.3 (no `+lrcpc` / `+lor` feature
# flag needed -- `+lrcpc` is also rejected by NDK r28+ clang; the feature
# flag is `+rcpc` if a backport to Armv8.2 is ever wanted, which we do not).
# On Digitalis these all route to plain x86 MOVs (and `MOV ; MFENCE` for
# the release-store variants) because x86-TSO is stronger than RCpc; if
# the decoder ever sent one to Undefined() the lib would SIGILL on the
# first probe.
target_compile_options(hellolrcpc PRIVATE
    -march=armv8.3-a)

target_link_libraries(hellolrcpc
    PRIVATE
    log
)
