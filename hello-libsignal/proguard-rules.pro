# This module routes the build through R8 solely so the whole program is dexed
# together — that provides the "global synthetics consumer" D8 needs to desugar
# the Java record classes inside libsignal-client (AGP 9 removed the
# enableGlobalSynthetics flag that the per-dependency dexing path would need).
#
# We do NOT want any actual shrinking/obfuscation/optimization here: it would
# strip Kotlin stdlib and JNI-reached libsignal classes. Disabling all three
# turns R8 into a pass-through dexer that still emits the record global
# synthetic correctly.
-dontshrink
-dontobfuscate
-dontoptimize

# Compile-only annotation deps (errorprone / javax.lang.model) are not on the
# runtime classpath; nothing references them at runtime, so silence R8.
-dontwarn javax.lang.model.**
-dontwarn com.google.errorprone.**
