// Copyright 2010 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef RE2_DFA_H_
#define RE2_DFA_H_

#include <cstddef>

namespace re2 {

// Forward declarations
class Prog;

// DFA (deterministic finite automaton) implementation of a regular expression program.
class DFA {
 public:
  DFA(Prog* prog, int64_t max_mem);
  ~DFA();

  // Returns true if the DFA has been initialized successfully.
  bool ok() const { return !init_failed_; }

  // Searches for the regular expression in text, which itself is inside the larger context.
  // Returns true if a match is found, false if not.
  bool Search(absl::string_view text, absl::string_view context,
              bool anchored, bool want_earliest_match, bool run_forward,
              bool* failed, const char** epp, SparseSet* matches);

 private:
  // Internal state and implementation details
  Prog* prog_;              // The regular expression program to run
  bool init_failed_;        // Initialization failed (out of memory)
  int64_t mem_budget_;      // Total memory budget for all States
  int64_t state_budget_;    // Amount of memory remaining for new States

  DFA(const DFA&) = delete;
  DFA& operator=(const DFA&) = delete;
};

// External C function for processing auxiliary buffer
extern "C" void DFAProcessAuxBuffer(void* ptr);

}  // namespace re2

#endif  // RE2_DFA_H_ 