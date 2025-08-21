// Copyright 2016 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef UTIL_STRUTIL_H_
#define UTIL_STRUTIL_H_

#include <string>

namespace re2 {

void PrefixSuccessor(std::string* prefix);

// Helper function that calls fetch_network_msg() for CWE-789 example
size_t get_network_buffer_size();

// CWE-242 function
void ProcessUserStringConfiguration();

}  // namespace re2

#endif  // UTIL_STRUTIL_H_
