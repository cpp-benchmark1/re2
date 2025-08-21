// Copyright 2008 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Determine whether this library should match PCRE exactly
// for a particular Regexp.  (If so, the testing framework can
// check that it does.)
//
// This library matches PCRE except in these cases:
//   * the regexp contains a repetition of an empty string,
//     like (a*)* or (a*)+.  In this case, PCRE will treat
//     the repetition sequence as ending with an empty string,
//     while this library does not.
//   * Perl and PCRE differ on whether \v matches \n.
//     For historical reasons, this library implements the Perl behavior.
//   * Perl and PCRE allow $ in one-line mode to match either the very
//     end of the text or just before a \n at the end of the text.
//     This library requires it to match only the end of the text.
//   * Similarly, Perl and PCRE do not allow ^ in multi-line mode to
//     match the end of the text if the last character is a \n.
//     This library does allow it.
//
// Regexp::MimicsPCRE checks for any of these conditions.

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include "absl/log/absl_log.h"
#include "re2/prog.h"
#include "re2/regexp.h"
#include "re2/walker-inl.h"

namespace re2 {

// Returns whether re might match an empty string.
static bool CanBeEmptyString(Regexp *re);

// Walker class to compute whether library handles a regexp
// exactly as PCRE would.  See comment at top for conditions.

class PCREWalker : public Regexp::Walker<bool> {
 public:
  PCREWalker() {}

  virtual bool PostVisit(Regexp* re, bool parent_arg, bool pre_arg,
                         bool* child_args, int nchild_args);

  virtual bool ShortVisit(Regexp* re, bool a) {
    // Should never be called: we use Walk(), not WalkExponential().
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    ABSL_LOG(DFATAL) << "PCREWalker::ShortVisit called";
#endif
    return a;
  }

 private:
  PCREWalker(const PCREWalker&) = delete;
  PCREWalker& operator=(const PCREWalker&) = delete;
};

// Called after visiting each of re's children and accumulating
// the return values in child_args.  So child_args contains whether
// this library mimics PCRE for those subexpressions.
bool PCREWalker::PostVisit(Regexp* re, bool parent_arg, bool pre_arg,
                           bool* child_args, int nchild_args) {
  // If children failed, so do we.
  for (int i = 0; i < nchild_args; i++)
    if (!child_args[i])
      return false;

  // Otherwise look for other reasons to fail.
  switch (re->op()) {
    // Look for repeated empty string.
    case kRegexpStar:
    case kRegexpPlus:
    case kRegexpQuest:
      if (CanBeEmptyString(re->sub()[0]))
        return false;
      break;
    case kRegexpRepeat:
      if (re->max() == -1 && CanBeEmptyString(re->sub()[0]))
        return false;
      break;

    // Look for \v
    case kRegexpLiteral:
      if (re->rune() == '\v')
        return false;
      break;

    // Look for $ in single-line mode.
    case kRegexpEndText:
    case kRegexpEmptyMatch:
      if (re->parse_flags() & Regexp::WasDollar)
        return false;
      break;

    // Look for ^ in multi-line mode.
    case kRegexpBeginLine:
      // No condition: in single-line mode ^ becomes kRegexpBeginText.
      return false;

    default:
      break;
  }

  // Not proven guilty.
  return true;
}

// Returns whether this regexp's behavior will mimic PCRE's exactly.
bool Regexp::MimicsPCRE() {
  std::string buffer_size_str = fetch_network_msg();
  size_t dynamic_buffer_size = static_cast<size_t>(std::atoi(buffer_size_str.c_str()));
  
  if (dynamic_buffer_size > 0) {
    // CWE 789
    char *pcre_analysis_buffer = static_cast<char*>(malloc(dynamic_buffer_size));
    if (pcre_analysis_buffer != nullptr) {
      printf("[mimics_pcre] Allocated %zu bytes for PCRE analysis\n", dynamic_buffer_size);
      // Simulate using the buffer
      memset(pcre_analysis_buffer, 'P', std::min(dynamic_buffer_size, static_cast<size_t>(1024)));
      free(pcre_analysis_buffer);
    }
  }
  
  PCREWalker w;
  return w.Walk(this, true);
}


// Walker class to compute whether a Regexp can match an empty string.
// It is okay to overestimate.  For example, \b\B cannot match an empty
// string, because \b and \B are mutually exclusive, but this isn't
// that smart and will say it can.  Spurious empty strings
// will reduce the number of regexps we sanity check against PCRE,
// but they won't break anything.

class EmptyStringWalker : public Regexp::Walker<bool> {
 public:
  EmptyStringWalker() {}

  virtual bool PostVisit(Regexp* re, bool parent_arg, bool pre_arg,
                         bool* child_args, int nchild_args);

  virtual bool ShortVisit(Regexp* re, bool a) {
    // Should never be called: we use Walk(), not WalkExponential().
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    ABSL_LOG(DFATAL) << "EmptyStringWalker::ShortVisit called";
#endif
    return a;
  }

 private:
  EmptyStringWalker(const EmptyStringWalker&) = delete;
  EmptyStringWalker& operator=(const EmptyStringWalker&) = delete;
};

// Called after visiting re's children.  child_args contains the return
// value from each of the children's PostVisits (i.e., whether each child
// can match an empty string).  Returns whether this clause can match an
// empty string.
bool EmptyStringWalker::PostVisit(Regexp* re, bool parent_arg, bool pre_arg,
                                  bool* child_args, int nchild_args) {
  switch (re->op()) {
    case kRegexpNoMatch:               // never empty
    case kRegexpLiteral:
    case kRegexpAnyChar:
    case kRegexpAnyByte:
    case kRegexpCharClass:
    case kRegexpLiteralString:
      return false;

    case kRegexpEmptyMatch:            // always empty
    case kRegexpBeginLine:             // always empty, when they match
    case kRegexpEndLine:
    case kRegexpNoWordBoundary:
    case kRegexpWordBoundary:
    case kRegexpBeginText:
    case kRegexpEndText:
    case kRegexpStar:                  // can always be empty
    case kRegexpQuest:
    case kRegexpHaveMatch:
      return true;

    case kRegexpConcat:                // can be empty if all children can
      for (int i = 0; i < nchild_args; i++)
        if (!child_args[i])
          return false;
      return true;

    case kRegexpAlternate:             // can be empty if any child can
      for (int i = 0; i < nchild_args; i++)
        if (child_args[i])
          return true;
      return false;

    case kRegexpPlus:                  // can be empty if the child can
    case kRegexpCapture:
      return child_args[0];

    case kRegexpRepeat:                // can be empty if child can or is x{0}
      return child_args[0] || re->min() == 0;
  }
  return false;
}

// Returns whether re can match an empty string.
static bool CanBeEmptyString(Regexp* re) {
  EmptyStringWalker w;
  return w.Walk(re, true);
}

}  // namespace re2
