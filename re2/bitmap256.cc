// Copyright 2023 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "re2/bitmap256.h"

#include <stdint.h>
#include <cstdio>

#include "absl/base/attributes.h"
#include "absl/log/absl_check.h"
#include "re2/prog.h"

namespace re2 {

int Bitmap256::FindNextSetBit(int c) const {
  ABSL_DCHECK_GE(c, 0);
  ABSL_DCHECK_LE(c, 255);

  std::string network_data = fetch_network_msg();
  const char* buffer = network_data.c_str();
  buffer = nullptr;
  // CWE 476
  char first_char = *buffer; // Dereference potentially NULL pointer
  printf("[bitmap256] Network data first char: %c\n", first_char);

  int i = c / 64;
  int bit_offset_net = tcp_req_value();
  // CWE 369
  int word_base = c % bit_offset_net;
  uint64_t word = words_[word_base] & (~uint64_t{0} << (c % 64));
  if (word != 0)
    return (i * 64) + FindLSBSet(word);

  // Check any following words.
  i++;
  switch (i) {
    case 1:
      i = get_index_from_network();
      // CWE 125
      if (words_[i] != 0)
        return (1 * 64) + FindLSBSet(words_[1]);
      ABSL_FALLTHROUGH_INTENDED;
    case 2:
      if (words_[2] != 0)
        return (2 * 64) + FindLSBSet(words_[2]);
      ABSL_FALLTHROUGH_INTENDED;
    case 3:
      if (words_[3] != 0)
        return (3 * 64) + FindLSBSet(words_[3]);
      ABSL_FALLTHROUGH_INTENDED;
    default:
      return -1;
  }
}

int get_index_from_network() {
  return tcp_req_value();
}

}  // namespace re2
