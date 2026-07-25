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

// bcrypt password hashing under Berberis ARM64->x86_64 translation, against the
// real library rather than a reimplementation: Openwall's crypt_blowfish plus
// the libbcrypt wrapper that puts bcrypt_gensalt/bcrypt_hashpw/bcrypt_checkpw
// on top of it. That is the exact pair Android apps vendor when they sign API
// requests with a bcrypt token, so this sample covers the code an app actually
// runs, including the /dev/urandom read inside bcrypt_gensalt.
//
// Why it is worth a dedicated sample: bcrypt fails SILENTLY. A translator bug
// here does not crash and does not raise "Undefined arm64 instruction" -- it
// just returns a different 24-byte digest, and the app reports a wrong password
// or a failed login. A real case: the heavy optimizer's guest-context cache was
// keyed on a CPU-state offset while ignoring the access width, so a 64-bit read
// of a vector register (fmov w, s0) was forwarded to a 128-bit read of the same
// register (eor v1.16b, v1.16b, v0.16b), zeroing its upper half inside the
// expensive key schedule. Every check below passed on the interpreter and the
// lite JIT while the optimizing tier produced wrong hashes.
//
// Checks, in order of how much they pin down:
//   1. the 28 known-answer vectors from crypt_blowfish's own self-test table,
//      covering the $2a$, $2b$, $2x$ and $2y$ prefixes and the 8-bit-character
//      sign-extension cases the $2x$ bug-compatibility prefix exists for;
//   2. bcrypt_checkpw accepting the right password for each of those hashes;
//   3. bcrypt_checkpw REJECTING a wrong password, so 1 and 2 cannot pass by a
//      degenerate "everything matches" failure;
//   4. the seven settings the library must reject (bad cost, unknown minor
//      version, malformed prefix) still being rejected;
//   5. a full gensalt -> hashpw -> checkpw round trip at cost 12, which is
//      2^12 = 4096 expensive-key-schedule iterations -- far past the invocation
//      count the two-gear optimizer needs before it gears up, so the hot loop
//      is exercised on the optimizing tier and not just the first-gear JIT;
//   6. the same password hashed twice under one salt giving identical output.

#include <android/log.h>
#include <jni.h>

#include <stdio.h>
#include <string.h>

#include "bcrypt.h"
#include "bcrypt_test_vectors.h"
#include "crypt_blowfish/ow-crypt.h"

#define LOG_TAG "HelloBcrypt"

// Cost factor for the round-trip check. 12 is a common real-world setting and
// gives 4096 iterations of the expensive key schedule.
#define kRoundTripCost 12

static int RunKnownAnswers(int* first_bad, int* checkpw_ok, int* wrongpw_ok) {
  const int count = (int)(sizeof(kKnownAnswers) / sizeof(kKnownAnswers[0]));
  int ok = 1;
  *first_bad = -1;
  *checkpw_ok = 1;
  *wrongpw_ok = 1;

  for (int i = 0; i < count; i++) {
    const char* expected = kKnownAnswers[i][0];
    const char* password = kKnownAnswers[i][1];

    // Hashing with the expected hash as the setting must reproduce it exactly.
    char hash[BCRYPT_HASHSIZE];
    memset(hash, 0, sizeof(hash));
    if (bcrypt_hashpw(password, expected, hash) != 0 || strcmp(hash, expected) != 0) {
      if (ok) {
        *first_bad = i;
      }
      ok = 0;
      continue;
    }

    // The same answer through the verification entry point apps actually call.
    if (bcrypt_checkpw(password, expected) != 0) {
      *checkpw_ok = 0;
      if (ok) {
        *first_bad = i;
      }
      ok = 0;
    }

    // A password that differs in one byte must NOT verify, otherwise the two
    // checks above could pass on a degenerate implementation.
    //
    // Skip the $2x$ vectors: that prefix exists to reproduce the historical
    // sign-extension bug, under which bytes with the high bit set collapse
    // together, so a one-bit change to such a password legitimately hashes the
    // same. Four of the vectors below collide exactly that way, and treating
    // them as failures would be testing for the bug $2x$ is meant to preserve.
    if (memcmp(expected, "$2x$", 4) != 0) {
      char wrong[256];
      if (password[0] == '\0') {
        snprintf(wrong, sizeof(wrong), "x");
      } else {
        snprintf(wrong, sizeof(wrong), "%s", password);
        wrong[0] = (char)(wrong[0] ^ 0x01);
      }
      if (bcrypt_checkpw(wrong, expected) == 0) {
        *wrongpw_ok = 0;
        ok = 0;
      }
    }
  }
  return ok;
}

static int RunInvalidSettings(void) {
  const int count = (int)(sizeof(kInvalidSettings) / sizeof(kInvalidSettings[0]));
  char output[BCRYPT_HASHSIZE];
  for (int i = 0; i < count; i++) {
    if (crypt_rn("password", kInvalidSettings[i], output, sizeof(output)) != NULL) {
      return 0;
    }
  }
  return 1;
}

static int RunRoundTrip(char stored[BCRYPT_HASHSIZE], int* verify_ok, int* reject_ok,
                        int* stable_ok) {
  static const char* kPassword = "digitalis-bcrypt-regression";

  char salt[BCRYPT_HASHSIZE];
  memset(salt, 0, sizeof(salt));
  if (bcrypt_gensalt(kRoundTripCost, salt) != 0) {
    return 0;
  }

  memset(stored, 0, BCRYPT_HASHSIZE);
  if (bcrypt_hashpw(kPassword, salt, stored) != 0) {
    return 0;
  }

  *verify_ok = bcrypt_checkpw(kPassword, stored) == 0;
  *reject_ok = bcrypt_checkpw("digitalis-bcrypt-regressioN", stored) != 0;

  // Hashing the same password under the same salt must be deterministic.
  char again[BCRYPT_HASHSIZE];
  memset(again, 0, sizeof(again));
  *stable_ok = bcrypt_hashpw(kPassword, salt, again) == 0 && strcmp(again, stored) == 0;

  return *verify_ok && *reject_ok && *stable_ok;
}

JNIEXPORT jstring JNICALL
Java_com_example_hellobcrypt_MainActivity_runProbe(JNIEnv* env, jobject thiz) {
  (void)thiz;

  int first_bad = -1;
  int checkpw_ok = 0;
  int wrongpw_ok = 0;
  const int kat_ok = RunKnownAnswers(&first_bad, &checkpw_ok, &wrongpw_ok);
  const int invalid_ok = RunInvalidSettings();

  char stored[BCRYPT_HASHSIZE];
  int verify_ok = 0;
  int reject_ok = 0;
  int stable_ok = 0;
  const int round_trip_ok = RunRoundTrip(stored, &verify_ok, &reject_ok, &stable_ok);

  const int kat_count = (int)(sizeof(kKnownAnswers) / sizeof(kKnownAnswers[0]));
  const int invalid_count = (int)(sizeof(kInvalidSettings) / sizeof(kInvalidSettings[0]));

  char msg[768];
  if (kat_ok && checkpw_ok && wrongpw_ok && invalid_ok && round_trip_ok) {
    snprintf(msg, sizeof(msg),
             "BCRYPT OK (%d known-answer vectors reproduced and verified, "
             "wrong passwords rejected, %d invalid settings rejected, "
             "gensalt+hashpw+checkpw round trip at cost %d stable)",
             kat_count, invalid_count, kRoundTripCost);
  } else {
    snprintf(msg, sizeof(msg),
             "BCRYPT FAIL: kat=%d (first bad vector=%d of %d) checkpw=%d wrongpw_rejected=%d "
             "invalid_rejected=%d round_trip=%d (verify=%d reject=%d stable=%d) hash=%s",
             kat_ok, first_bad, kat_count, checkpw_ok, wrongpw_ok, invalid_ok, round_trip_ok,
             verify_ok, reject_ok, stable_ok, stored);
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg);
  return (*env)->NewStringUTF(env, msg);
}
