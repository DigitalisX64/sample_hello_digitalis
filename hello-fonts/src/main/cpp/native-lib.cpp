/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/font.h>
#include <android/font_matcher.h>
#include <android/log.h>
#include <android/system_fonts.h>
#include <fcntl.h>
#include <jni.h>
#include <strings.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#define LOG_TAG "hellofonts"

namespace {

// SFNT container magics: TrueType 0x00010000, CFF 'OTTO', collection 'ttcf',
// and the legacy Apple 'true' tag (accepted defensively; AOSP does not ship it).
constexpr uint32_t kSfntTtf = 0x00010000U;
constexpr uint32_t kSfntOtto = 0x4F54544FU;
constexpr uint32_t kSfntTtcf = 0x74746366U;
constexpr uint32_t kSfntTrue = 0x74727565U;

int g_failures = 0;

void Failf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Failf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "hellofonts FAIL at %s", buf);
  ++g_failures;
}

bool HasSuffix(const char* s, const char* suffix) {
  size_t ls = strlen(s), lf = strlen(suffix);
  return ls >= lf && strcasecmp(s + ls - lf, suffix) == 0;
}

// font.h documents *.otf, *.ttf, *.otc or *.ttc.
bool HasFontExtension(const char* path) {
  return HasSuffix(path, ".ttf") || HasSuffix(path, ".otf") ||
         HasSuffix(path, ".ttc") || HasSuffix(path, ".otc");
}

// Path invariants shared by iterator fonts and matched fonts: non-null,
// absolute, readable from guest code, and a documented font extension.
bool CheckFontPath(const char* path, const char* where) {
  if (path == nullptr) {
    Failf("%s: null font file path", where);
    return false;
  }
  if (path[0] != '/') {
    Failf("%s: path not absolute: %s", where, path);
    return false;
  }
  if (access(path, R_OK) != 0) {
    Failf("%s: access(%s) failed, errno=%d", where, path, errno);
    return false;
  }
  if (!HasFontExtension(path)) {
    Failf("%s: unexpected extension: %s", where, path);
    return false;
  }
  return true;
}

// Open the file behind a matched font and verify the SFNT magic — proves the
// returned path is a real font file readable byte-for-byte from guest code.
void CheckSfntMagic(const char* path) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    Failf("sfnt: open(%s) failed, errno=%d", path, errno);
    return;
  }
  uint8_t b[4] = {0, 0, 0, 0};
  ssize_t n = read(fd, b, sizeof(b));
  close(fd);
  if (n != 4) {
    Failf("sfnt: short read (%zd bytes) from %s", n, path);
    return;
  }
  uint32_t magic = static_cast<uint32_t>(b[0]) << 24 | static_cast<uint32_t>(b[1]) << 16 |
                   static_cast<uint32_t>(b[2]) << 8 | static_cast<uint32_t>(b[3]);
  if (magic != kSfntTtf && magic != kSfntOtto && magic != kSfntTtcf && magic != kSfntTrue) {
    Failf("sfnt: bad magic 0x%08x in %s", magic, path);
  }
}

// Validate a matched font and its out-param run length. Returns the font file
// path ("" on failure). Does NOT close the font — the caller does.
std::string ValidateMatch(const AFont* font, uint32_t run, uint32_t textLen, const char* where) {
  if (font == nullptr) {
    // font_matcher.h: match returns non-null even when no font can render
    // the text (Tofu fallback), so null is always a failure.
    Failf("%s: AFontMatcher_match returned null", where);
    return "";
  }
  std::string result;
  const char* path = AFont_getFontFilePath(font);
  if (CheckFontPath(path, where)) result = path;
  uint16_t w = AFont_getWeight(font);
  if (w > AFONT_WEIGHT_MAX) Failf("%s: weight %u exceeds %d", where, w, AFONT_WEIGHT_MAX);
  if (run < 1 || run > textLen) Failf("%s: run length %u outside [1,%u]", where, run, textLen);
  return result;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofonts_MainActivity_probeFonts(JNIEnv* env, jobject /*this*/) {
  g_failures = 0;

  // --- 1. System font iterator: enumerate every font, call every getter ------
  size_t total = 0;
  size_t robotoCount = 0;
  size_t axisTotal = 0;
  std::set<std::string> distinctFiles;

  ASystemFontIterator* iterator = ASystemFontIterator_open();
  if (iterator == nullptr) {
    Failf("iterator-open: ASystemFontIterator_open returned null");
  } else {
    AFont* font = nullptr;
    while ((font = ASystemFontIterator_next(iterator)) != nullptr) {
      ++total;
      const char* path = AFont_getFontFilePath(font);
      bool pathOk = CheckFontPath(path, "iterator");
      if (pathOk) {
        distinctFiles.insert(path);
        if (strstr(path, "Roboto") != nullptr) ++robotoCount;
      }

      uint16_t weight = AFont_getWeight(font);
      if (weight > AFONT_WEIGHT_MAX) {
        Failf("iterator: weight %u exceeds %d (%s)", weight, AFONT_WEIGHT_MAX,
              pathOk ? path : "?");
      }

      (void)AFont_isItalic(font);        // bool: any value is valid
      (void)AFont_getLocale(font);       // nullable per header: null is allowed

      // size_t is unsigned so ">= 0" is vacuous; assert the header contract
      // instead: a regular (non-collection) file always reports index 0.
      size_t collectionIndex = AFont_getCollectionIndex(font);
      if (pathOk && !HasSuffix(path, ".ttc") && !HasSuffix(path, ".otc") &&
          collectionIndex != 0) {
        Failf("iterator: collection index %zu for non-collection file %s", collectionIndex,
              path);
      }

      size_t axisCount = AFont_getAxisCount(font);
      axisTotal += axisCount;
      for (size_t i = 0; i < axisCount; ++i) {
        uint32_t tag = AFont_getAxisTag(font, static_cast<uint32_t>(i));
        // OpenType axis tags are 4 printable-ASCII bytes (0x20..0x7E).
        for (int b = 0; b < 4; ++b) {
          uint8_t c = (tag >> (24 - 8 * b)) & 0xFF;
          if (c < 0x20 || c > 0x7E) {
            Failf("iterator: axis tag 0x%08x has non-printable byte (%s)", tag,
                  pathOk ? path : "?");
            break;
          }
        }
        float value = AFont_getAxisValue(font, static_cast<uint32_t>(i));
        if (value != value) {
          Failf("iterator: axis value is NaN, tag 0x%08x (%s)", tag, pathOk ? path : "?");
        }
      }

      AFont_close(font);
    }
    ASystemFontIterator_close(iterator);
  }

  if (total == 0) Failf("iterator: no system fonts enumerated");
  if (total > 0 && robotoCount == 0) {
    Failf("iterator: no Roboto font among %zu fonts (AOSP default expected)", total);
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "iterator tally: %zu fonts, %zu distinct files, %zu Roboto, %zu axes",
                      total, distinctFiles.size(), robotoCount, axisTotal);

  // --- 2. AFontMatcher ---------------------------------------------------------
  // The family name goes to AFontMatcher_match, not _create (the header's own
  // doc example is stale; the declaration takes no arguments).
  const uint16_t kAb[] = {0x0041, 0x0062};     // "Ab"
  const uint16_t kNihon[] = {0x65E5, 0x672C};  // "日本" (U+65E5 U+672C)

  AFontMatcher* matcher = AFontMatcher_create();
  if (matcher == nullptr) {
    Failf("matcher-create: AFontMatcher_create returned null");
  } else {
    // Regular: weight 400, non-italic.
    AFontMatcher_setStyle(matcher, AFONT_WEIGHT_NORMAL, false);
    uint32_t run = 0;
    AFont* regular = AFontMatcher_match(matcher, "sans-serif", kAb, 2, &run);
    ValidateMatch(regular, run, 2, "match-regular");

    // Bold italic: weight 700 + italic; must still match, and should differ
    // in weight or slant metadata from the regular match.
    AFontMatcher_setStyle(matcher, AFONT_WEIGHT_BOLD, true);
    run = 0;
    AFont* bold = AFontMatcher_match(matcher, "sans-serif", kAb, 2, &run);
    ValidateMatch(bold, run, 2, "match-bold-italic");
    if (regular != nullptr && bold != nullptr) {
      uint16_t wr = AFont_getWeight(regular), wb = AFont_getWeight(bold);
      bool ir = AFont_isItalic(regular), ib = AFont_isItalic(bold);
      if (wr == wb && ir == ib) {
        // Host may collapse styles to one face; log it, but only a null
        // return is a failure.
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                            "matcher collapsed 400/regular and 700/italic to identical "
                            "metadata (weight=%u italic=%d) — host behavior",
                            wr, ir ? 1 : 0);
      }
    }
    AFont_close(regular);
    AFont_close(bold);

    // Family variants: exercise every AFAMILY_VARIANT_* value with a match.
    AFontMatcher_setStyle(matcher, AFONT_WEIGHT_NORMAL, false);
    const struct {
      uint32_t value;
      const char* name;
    } kVariants[] = {
        {AFAMILY_VARIANT_DEFAULT, "default"},
        {AFAMILY_VARIANT_COMPACT, "compact"},
        {AFAMILY_VARIANT_ELEGANT, "elegant"},
    };
    for (const auto& v : kVariants) {
      AFontMatcher_setFamilyVariant(matcher, v.value);
      run = 0;
      AFont* f = AFontMatcher_match(matcher, "sans-serif", kAb, 2, &run);
      char where[64];
      snprintf(where, sizeof(where), "variant-%s", v.name);
      ValidateMatch(f, run, 2, where);
      AFont_close(f);
    }
    AFontMatcher_setFamilyVariant(matcher, AFAMILY_VARIANT_DEFAULT);

    // Locales + CJK text: expect a CJK-capable font whose file exists; then
    // cross-check the SFNT magic of that matched file.
    AFontMatcher_setLocales(matcher, "ja-JP,en-US");
    run = 0;
    AFont* cjk = AFontMatcher_match(matcher, "sans-serif", kNihon, 2, &run);
    std::string cjkPath = ValidateMatch(cjk, run, 2, "match-cjk");
    if (!cjkPath.empty()) {
      CheckSfntMagic(cjkPath.c_str());
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "cjk match: %s", cjkPath.c_str());
    }
    AFont_close(cjk);

    // Unknown family must still return a fallback font per the header
    // contract. Passing null runLengthOut also exercises the _Nullable branch.
    AFont* fallback = AFontMatcher_match(matcher, "no-such-family-digitalis", kAb, 2, nullptr);
    if (fallback == nullptr) {
      Failf("unknown-family: header guarantees a non-null fallback font");
    } else {
      CheckFontPath(AFont_getFontFilePath(fallback), "unknown-family");
      AFont_close(fallback);
    }

    AFontMatcher_destroy(matcher);
  }

  // --- Summary ----------------------------------------------------------------
  char summary[256];
  if (g_failures == 0) {
    snprintf(summary, sizeof(summary),
             "hellofonts OK: %zu fonts, %zu distinct files, %zu Roboto, %zu axes; "
             "matcher + variants + cjk + fallback + sfnt verified",
             total, distinctFiles.size(), robotoCount, axisTotal);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", summary);
  } else {
    snprintf(summary, sizeof(summary), "hellofonts: %d check(s) FAILED — see logcat",
             g_failures);
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", summary);
  }
  return env->NewStringUTF(summary);
}
