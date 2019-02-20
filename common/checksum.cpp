/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "checksum.h"

#include <openssl/md5.h>
#include <openssl/sha.h>

namespace common {

std::string toHex(std::vector<uint8_t> const& digest) {
  static const char alphabet[] = "0123456789abcdef";
  std::string dest(digest.size() * 2, 0);
  size_t j = 0;
  for (size_t i = 0; i < digest.size(); i++) {
    dest[j++] = alphabet[digest[i] >> 4];
    dest[j++] = alphabet[digest[i] & 0x0F];
  }
  return dest;
}

std::vector<uint8_t> sha256sum(void const* data, size_t len) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, data, len);
  SHA256_Final(digest.data(), &ctx);
  return digest;
}

std::vector<uint8_t> md5sum(void const* data, size_t len) {
  std::vector<uint8_t> digest(MD5_DIGEST_LENGTH);
  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, data, len);
  MD5_Final(digest.data(), &ctx);
  return digest;
}

} // namespace common
