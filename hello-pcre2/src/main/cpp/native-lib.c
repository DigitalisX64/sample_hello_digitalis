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

// Integration-level probe for PCRE2 (Perl-Compatible Regular Expressions) and
// its sljit-based JIT under Berberis ARM64->x86_64 translation.
//
// PCRE2's JIT (pcre2_jit_compile) makes sljit emit ARM64 machine code at
// runtime and announce that self-modified code to the I-cache via IC IVAU.
// Under translation, the translator must treat IC IVAU as a translation-cache
// invalidation of the regenerated line, not a NOP, or it runs a stale
// translation of the freshly written code. So this probe deliberately drives
// the JIT path (pcre2_jit_match) in addition to the interpreted bytecode path
// (pcre2_match) and cross-checks that both find the same captures.
//
// Pattern: a date "(\d{4})-(\d{2})-(\d{2})" with three capture groups.
// Subject: "today is 2026-06-21 ok" -> the match begins at offset 9 and
// captures group1="2026", group2="06", group3="21".

#include <android/log.h>
#include <jni.h>

#include <stdio.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define LOG_TAG "HelloPcre2"

// Compare ovector capture group `g` against the expected NUL-terminated text.
// Returns 1 on match, 0 otherwise. `ovector` is the PCRE2 output vector and
// `subject` is the matched subject string.
static int GroupEquals(const PCRE2_SIZE* ovector, const char* subject, int g,
                       const char* expected) {
  PCRE2_SIZE start = ovector[2 * g];
  PCRE2_SIZE end = ovector[2 * g + 1];
  if (start == PCRE2_UNSET || end == PCRE2_UNSET || end < start) {
    return 0;
  }
  size_t len = (size_t)(end - start);
  if (len != strlen(expected)) {
    return 0;
  }
  return memcmp(subject + start, expected, len) == 0;
}

JNIEXPORT jstring JNICALL
Java_com_example_hellopcre2_MainActivity_runProbe(JNIEnv* env, jobject thiz) {
  (void)thiz;

  char result[256];
  const char* pattern = "(\\d{4})-(\\d{2})-(\\d{2})";
  const char* subject = "today is 2026-06-21 ok";
  const PCRE2_SIZE subject_len = (PCRE2_SIZE)strlen(subject);
  // The match starts at byte offset 9 ("today is " is 9 characters).
  const PCRE2_SIZE kExpectedMatchStart = 9;

  pcre2_code* code = NULL;
  pcre2_match_data* jit_md = NULL;
  pcre2_match_data* interp_md = NULL;

  // 1. Compile the pattern.
  int errorcode = 0;
  PCRE2_SIZE erroroffset = 0;
  code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0,
                       &errorcode, &erroroffset, NULL);
  if (code == NULL) {
    PCRE2_UCHAR buf[128];
    pcre2_get_error_message(errorcode, buf, sizeof(buf));
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: compile error %d at offset %zu: %s", errorcode,
             (size_t)erroroffset, (const char*)buf);
    goto done;
  }

  // 2. JIT-compile the pattern. This is the key step: it emits ARM64 machine
  // code at runtime and flushes the I-cache with IC IVAU, exercising the
  // translator's self-modifying-code invalidation path. rc must be 0.
  int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
  if (jit_rc != 0) {
    snprintf(result, sizeof(result), "PCRE2 FAIL: jit_compile rc=%d", jit_rc);
    goto done;
  }

  // 3a. Match via the JIT-executed path.
  jit_md = pcre2_match_data_create_from_pattern(code, NULL);
  if (jit_md == NULL) {
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: jit match_data alloc failed");
    goto done;
  }
  int jit_match_rc = pcre2_jit_match(code, (PCRE2_SPTR)subject, subject_len, 0,
                                     0, jit_md, NULL);
  if (jit_match_rc < 0) {
    snprintf(result, sizeof(result), "PCRE2 FAIL: jit_match rc=%d",
             jit_match_rc);
    goto done;
  }
  // rc is 1 (whole match) + number of capturing groups => expect 4.
  if (jit_match_rc != 4) {
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: jit_match groups=%d (want 4)", jit_match_rc);
    goto done;
  }
  PCRE2_SIZE* jit_ov = pcre2_get_ovector_pointer(jit_md);
  if (jit_ov[0] != kExpectedMatchStart) {
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: jit_match offset=%zu (want %zu)", (size_t)jit_ov[0],
             (size_t)kExpectedMatchStart);
    goto done;
  }
  if (!GroupEquals(jit_ov, subject, 1, "2026") ||
      !GroupEquals(jit_ov, subject, 2, "06") ||
      !GroupEquals(jit_ov, subject, 3, "21")) {
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: jit_match captured wrong groups");
    goto done;
  }

  // 3b. Match via the interpreted bytecode path (independent of the JIT).
  interp_md = pcre2_match_data_create_from_pattern(code, NULL);
  if (interp_md == NULL) {
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: interp match_data alloc failed");
    goto done;
  }
  int interp_match_rc = pcre2_match(code, (PCRE2_SPTR)subject, subject_len, 0, 0,
                                    interp_md, NULL);
  if (interp_match_rc < 0) {
    snprintf(result, sizeof(result), "PCRE2 FAIL: match rc=%d",
             interp_match_rc);
    goto done;
  }
  if (interp_match_rc != 4) {
    snprintf(result, sizeof(result), "PCRE2 FAIL: match groups=%d (want 4)",
             interp_match_rc);
    goto done;
  }
  PCRE2_SIZE* interp_ov = pcre2_get_ovector_pointer(interp_md);
  if (interp_ov[0] != kExpectedMatchStart) {
    snprintf(result, sizeof(result), "PCRE2 FAIL: match offset=%zu (want %zu)",
             (size_t)interp_ov[0], (size_t)kExpectedMatchStart);
    goto done;
  }
  if (!GroupEquals(interp_ov, subject, 1, "2026") ||
      !GroupEquals(interp_ov, subject, 2, "06") ||
      !GroupEquals(interp_ov, subject, 3, "21")) {
    snprintf(result, sizeof(result),
             "PCRE2 FAIL: match captured wrong groups");
    goto done;
  }

  // 4. Both paths agreed on offset and all three captures.
  snprintf(result, sizeof(result),
           "PCRE2 OK (jit_compile=0, jit+interp match 2026-06-21, 3 groups)");

done:
  if (interp_md != NULL) {
    pcre2_match_data_free(interp_md);
  }
  if (jit_md != NULL) {
    pcre2_match_data_free(jit_md);
  }
  if (code != NULL) {
    pcre2_code_free(code);
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", result);
  return (*env)->NewStringUTF(env, result);
}
