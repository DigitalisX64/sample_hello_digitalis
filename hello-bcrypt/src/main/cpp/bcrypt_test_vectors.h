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

// Known-answer vectors for bcrypt, extracted verbatim from the self-test table
// in crypt_blowfish 1.3's wrapper.c (public domain, Solar Designer). They are
// the upstream project's own answers, so they are ground truth independent of
// anything in this sample.

#ifndef HELLO_BCRYPT_TEST_VECTORS_H_
#define HELLO_BCRYPT_TEST_VECTORS_H_

// Each entry is {expected hash, password}. Hashing the password with the
// expected hash as the setting must reproduce the expected hash exactly.
static const char* const kKnownAnswers[][2] = {
    {"$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW",
     "U*U"},
    {"$2a$05$CCCCCCCCCCCCCCCCCCCCC.VGOzA784oUp/Z0DY336zx7pLYAy0lwK",
     "U*U*"},
    {"$2a$05$XXXXXXXXXXXXXXXXXXXXXOAcXxm9kjPGEMsLznoKqmqw7tc8WCx4a",
     "U*U*U"},
    {"$2a$05$abcdefghijklmnopqrstuu5s2v8.iXieOjg/.AySBTTZIIVFJeBui",
     "0123456789abcdefghijklmnopqrstuvwxyz" "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" "chars after 72 are ignored"},
    {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e",
     "\xa3"},
    {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e",
     "\xff\xff\xa3"},
    {"$2y$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e",
     "\xff\xff\xa3"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.nqd1wy.pTMdcvrRWxyiGL2eMz.2a85.",
     "\xff\xff\xa3"},
    {"$2b$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e",
     "\xff\xff\xa3"},
    {"$2y$05$/OK.fbVrR/bpIqNJ5ianF.Sa7shbm4.OzKpvFnX1pQLmQW96oUlCq",
     "\xa3"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.Sa7shbm4.OzKpvFnX1pQLmQW96oUlCq",
     "\xa3"},
    {"$2b$05$/OK.fbVrR/bpIqNJ5ianF.Sa7shbm4.OzKpvFnX1pQLmQW96oUlCq",
     "\xa3"},
    {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.o./n25XVfn6oAPaUvHe.Csk4zRfsYPi",
     "1\xa3" "345"},
    {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.o./n25XVfn6oAPaUvHe.Csk4zRfsYPi",
     "\xff\xa3" "345"},
    {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.o./n25XVfn6oAPaUvHe.Csk4zRfsYPi",
     "\xff\xa3" "34" "\xff\xff\xff\xa3" "345"},
    {"$2y$05$/OK.fbVrR/bpIqNJ5ianF.o./n25XVfn6oAPaUvHe.Csk4zRfsYPi",
     "\xff\xa3" "34" "\xff\xff\xff\xa3" "345"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.ZC1JEJ8Z4gPfpe1JOr/oyPXTWl9EFd.",
     "\xff\xa3" "34" "\xff\xff\xff\xa3" "345"},
    {"$2y$05$/OK.fbVrR/bpIqNJ5ianF.nRht2l/HRhr6zmCp9vYUvvsqynflf9e",
     "\xff\xa3" "345"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.nRht2l/HRhr6zmCp9vYUvvsqynflf9e",
     "\xff\xa3" "345"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.6IflQkJytoRVc1yuaNtHfiuq.FRlSIS",
     "\xa3" "ab"},
    {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.6IflQkJytoRVc1yuaNtHfiuq.FRlSIS",
     "\xa3" "ab"},
    {"$2y$05$/OK.fbVrR/bpIqNJ5ianF.6IflQkJytoRVc1yuaNtHfiuq.FRlSIS",
     "\xa3" "ab"},
    {"$2x$05$6bNw2HLQYeqHYyBfLMsv/OiwqTymGIGzFsA4hOTWebfehXHNprcAS",
     "\xd1\x91"},
    {"$2x$05$6bNw2HLQYeqHYyBfLMsv/O9LIGgn8OMzuDoHfof8AQimSGfcSWxnS",
     "\xd0\xc1\xd2\xcf\xcc\xd8"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.swQOIzjOiJ9GHEPuhEkvqrUyvWhEMx6",
     "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa" "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa" "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa" "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa" "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa" "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa" "chars after 72 are ignored as usual"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.R9xrDjiycxMbQE2bp.vgqlYpW5wx2yy",
     "\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55" "\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55" "\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55" "\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55" "\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55" "\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55\xaa\x55"},
    {"$2a$05$/OK.fbVrR/bpIqNJ5ianF.9tQZzcJfm3uj2NvJ/n5xkhpqLrMpWCe",
     "\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff" "\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff" "\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff" "\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff" "\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff" "\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff\x55\xaa\xff"},
    {"$2a$05$CCCCCCCCCCCCCCCCCCCCC.7uG0VCzI2bS7j6ymqJi9CdcdxiRTWNy",
     ""},
};

// Settings the library must REJECT: an out-of-range cost, an unsupported minor
// version, and a malformed prefix. crypt_rn must return NULL for each.
static const char* const kInvalidSettings[] = {
    "$2a$03$CCCCCCCCCCCCCCCCCCCCC.",
    "$2a$32$CCCCCCCCCCCCCCCCCCCCC.",
    "$2c$05$CCCCCCCCCCCCCCCCCCCCC.",
    "$2z$05$CCCCCCCCCCCCCCCCCCCCC.",
    "$2`$05$CCCCCCCCCCCCCCCCCCCCC.",
    "$2{$05$CCCCCCCCCCCCCCCCCCCCC.",
    "*0",
};

#endif  // HELLO_BCRYPT_TEST_VECTORS_H_
