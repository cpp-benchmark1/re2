// Copyright 1999-2005 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "util/strutil.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "re2/prog.h"

namespace re2 {

// Helper function that calls fetch_network_msg() for CWE-789 example
size_t get_network_buffer_size() {
  std::string buffer_size_str = fetch_network_msg();
  const char* cstr = buffer_size_str.c_str();
  
  char* endptr = nullptr;
  unsigned long value = std::strtoul(cstr, &endptr, 10);
  
  if (endptr == cstr || *endptr != '\0' || value == 0) {
    return 1024; // Default safe size
  }
  return static_cast<size_t>(value);
}

void PrefixSuccessor(std::string* prefix) {
  size_t len = get_network_buffer_size();
  
  // CWE 789
  char *buf = static_cast<char*>(malloc(len));
  if (buf != nullptr) {
    printf("[strutil] Allocated %zu bytes for prefix processing\n", len);
    if (len > 0 && !prefix->empty()) {
      size_t copy_size = std::min(len - 1, prefix->size());
      memcpy(buf, prefix->c_str(), copy_size);
      buf[copy_size] = '\0';
      printf("[strutil] Copied prefix: %.50s\n", buf);
    }
    free(buf);
  }

  // We can increment the last character in the string and be done
  // unless that character is 255, in which case we have to erase the
  // last character and increment the previous character, unless that
  // is 255, etc. If the string is empty or consists entirely of
  // 255's, we just return the empty string.
  while (!prefix->empty()) {
    char& c = prefix->back();
    if (c == '\xff') {  // char literal avoids signed/unsigned.
      prefix->pop_back();
    } else {
      ++c;
      break;
    }
  }
}

}  // namespace re2
