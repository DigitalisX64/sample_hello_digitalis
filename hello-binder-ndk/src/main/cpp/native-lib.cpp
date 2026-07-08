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

#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstring>
#include <string>

#define LOG_TAG "hellobinderndk"

namespace {

// ---------------------------------------------------------------------------
// Typed-parcel round-trip probe.
//
// A local AIBinder is transacted end-to-end: the JNI side marshals a fixed,
// known payload of mixed scalar + string types into an AParcel, calls
// AIBinder_transact (which routes through the binder-ndk proxy trampolines and
// dispatches to OnTransact in-process), and OnTransact unmarshals every field,
// echoes it back, and appends a checksum it computes server-side. The JNI side
// then unmarshals the reply and asserts each field equals a HARDCODED golden.
//
// This exercises AParcel_write/readInt32/Int64/Float/Double/String and the
// AParcel_stringAllocator callback across the binder transact boundary — the
// path a real binder client hits — with a bit-exact golden so a marshalling or
// instruction-translation bug shows up as a value mismatch, not a crash.
//
// Golden derivation (all inputs exactly representable; checksum by hand):
//   kA = 1000, kB = 0x0000000100000002 (=4294967298), kC = 1.5f, kD = 3.25,
//   kS = "digitalis-binder"
//   checksum = kA ^ (int32)(kB & 0xFFFFFFFF) = 1000 ^ 2 = 1002
// ---------------------------------------------------------------------------
constexpr int32_t kA = 1000;
constexpr int64_t kB = 0x0000000100000002LL;
constexpr float kC = 1.5f;
constexpr double kD = 3.25;
constexpr char kS[] = "digitalis-binder";
constexpr int32_t kChecksum = 1002;
constexpr transaction_code_t kEchoCode = FIRST_CALL_TRANSACTION;

void* OnCreate(void* args) { return args; }
void OnDestroy(void* /*userData*/) {}

// AParcel_readString allocator: `length` includes the null terminator, so the
// usable content is length-1. std::string::resize(length-1) guarantees a
// writable null at index length-1, so &(*str)[0] exposes `length` bytes.
bool StringAllocator(void* stringData, int32_t length, char** buffer) {
  auto* str = static_cast<std::string*>(stringData);
  if (length < 0) {  // null string
    *buffer = nullptr;
    return true;
  }
  str->resize(length - 1);
  *buffer = length > 1 ? &(*str)[0] : nullptr;
  return true;
}

// Server side of the echo transaction: reads every field in the same order the
// client wrote them, echoes each back, and appends a computed checksum.
binder_status_t OnTransact(AIBinder* /*binder*/, transaction_code_t code, const AParcel* in,
                           AParcel* out) {
  if (code != kEchoCode) {
    return STATUS_UNKNOWN_TRANSACTION;
  }
  int32_t a = 0;
  int64_t b = 0;
  float c = 0.0f;
  double d = 0.0;
  std::string s;
  binder_status_t st;
  if ((st = AParcel_readInt32(in, &a)) != STATUS_OK) return st;
  if ((st = AParcel_readInt64(in, &b)) != STATUS_OK) return st;
  if ((st = AParcel_readFloat(in, &c)) != STATUS_OK) return st;
  if ((st = AParcel_readDouble(in, &d)) != STATUS_OK) return st;
  if ((st = AParcel_readString(in, &s, StringAllocator)) != STATUS_OK) return st;

  const int32_t checksum = a ^ static_cast<int32_t>(b & 0xFFFFFFFF);
  if ((st = AParcel_writeInt32(out, a)) != STATUS_OK) return st;
  if ((st = AParcel_writeInt64(out, b)) != STATUS_OK) return st;
  if ((st = AParcel_writeFloat(out, c)) != STATUS_OK) return st;
  if ((st = AParcel_writeDouble(out, d)) != STATUS_OK) return st;
  if ((st = AParcel_writeString(out, s.c_str(), static_cast<int32_t>(s.size()))) != STATUS_OK) {
    return st;
  }
  if ((st = AParcel_writeInt32(out, checksum)) != STATUS_OK) return st;
  return STATUS_OK;
}

// Runs the typed-parcel round-trip and asserts each returned field against its
// golden. Returns "" on success, or a "FAIL at <tag>: got=… want=…" string.
std::string ProbeParcelRoundTrip() {
  AIBinder_Class* clazz =
      AIBinder_Class_define("com.example.hellodigitalis.Echo", OnCreate, OnDestroy, OnTransact);
  if (clazz == nullptr) {
    return "FAIL at class-define: AIBinder_Class_define returned null";
  }
  AIBinder* binder = AIBinder_new(clazz, /*args=*/nullptr);
  if (binder == nullptr) {
    return "FAIL at binder-new: AIBinder_new returned null";
  }

  AParcel* in = nullptr;
  binder_status_t st = AIBinder_prepareTransaction(binder, &in);
  if (st != STATUS_OK) {
    AIBinder_decStrong(binder);
    return "FAIL at prepare: status=" + std::to_string(st);
  }
  AParcel_writeInt32(in, kA);
  AParcel_writeInt64(in, kB);
  AParcel_writeFloat(in, kC);
  AParcel_writeDouble(in, kD);
  AParcel_writeString(in, kS, static_cast<int32_t>(std::strlen(kS)));

  AParcel* out = nullptr;
  st = AIBinder_transact(binder, kEchoCode, &in, &out, /*flags=*/0);
  if (st != STATUS_OK) {
    AIBinder_decStrong(binder);
    return "FAIL at transact: status=" + std::to_string(st);
  }

  std::string err;
  int32_t a = 0;
  int64_t b = 0;
  float c = 0.0f;
  double d = 0.0;
  std::string s;
  int32_t checksum = 0;
  AParcel_readInt32(out, &a);
  AParcel_readInt64(out, &b);
  AParcel_readFloat(out, &c);
  AParcel_readDouble(out, &d);
  AParcel_readString(out, &s, StringAllocator);
  AParcel_readInt32(out, &checksum);

  if (a != kA) {
    err = "FAIL at int32: got=" + std::to_string(a) + " want=" + std::to_string(kA);
  } else if (b != kB) {
    err = "FAIL at int64: got=" + std::to_string(b) + " want=" + std::to_string(kB);
  } else if (c != kC) {
    err = "FAIL at float: got=" + std::to_string(c) + " want=" + std::to_string(kC);
  } else if (d != kD) {
    err = "FAIL at double: got=" + std::to_string(d) + " want=" + std::to_string(kD);
  } else if (s != kS) {
    err = "FAIL at string: got=\"" + s + "\" want=\"" + std::string(kS) + "\"";
  } else if (checksum != kChecksum) {
    err = "FAIL at checksum: got=" + std::to_string(checksum) + " want=" + std::to_string(kChecksum);
  }

  AParcel_delete(out);
  AIBinder_decStrong(binder);
  return err;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellobinderndk_MainActivity_probeBinderNdk(JNIEnv* env, jobject /*this*/) {
  std::string parcel_result = ProbeParcelRoundTrip();
  std::string msg;
  if (parcel_result.empty()) {
    msg = "hellobinderndk OK: typed-parcel round-trip verified";
  } else {
    // Contains "FAIL" — the StatusTest gate treats this as a failure marker.
    msg = "hellobinderndk " + parcel_result;
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
