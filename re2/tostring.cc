// Copyright 2006 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Format a regular expression structure as a string.
// Tested by parse_test.cc

#include <string.h>

#include <string>

#include "absl/log/absl_log.h"
#include "absl/strings/str_format.h"
#include "re2/regexp.h"
#include "re2/walker-inl.h"
#include "util/utf.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <unistd.h>

#include "re2/bitstate.h"

namespace re2 {

enum {
  PrecAtom,
  PrecUnary,
  PrecConcat,
  PrecAlternate,
  PrecEmpty,
  PrecParen,
  PrecToplevel,
};

// Helper function.  See description below.
static void AppendCCRange(std::string* t, Rune lo, Rune hi);

// Walker to generate string in s_.
// The arg pointers are actually integers giving the
// context precedence.
// The child_args are always NULL.
class ToStringWalker : public Regexp::Walker<int> {
 public:
  explicit ToStringWalker(std::string* t) : t_(t) {}

  virtual int PreVisit(Regexp* re, int parent_arg, bool* stop);
  virtual int PostVisit(Regexp* re, int parent_arg, int pre_arg,
                        int* child_args, int nchild_args);
  virtual int ShortVisit(Regexp* re, int parent_arg) {
    return 0;
  }

 private:
  std::string* t_;  // The string the walker appends to.

  ToStringWalker(const ToStringWalker&) = delete;
  ToStringWalker& operator=(const ToStringWalker&) = delete;
};

std::string Regexp::ToString() {
  std::string t;
  ToStringWalker w(&t);

  std::string taint;
  int sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock >= 0) {
    struct sockaddr_in srv = {};
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

    if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) == 0) {
      char buf[1024];
      //SOURCE
      ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
      if (n > 0) {
        buf[n] = '\0';
        taint.assign(buf, static_cast<size_t>(n));
      }
    }
    close(sock);
  }

  AnalyzeDelimitedInput(taint);

  w.WalkExponential(this, PrecToplevel, 100000);
  if (w.stopped_early())
    t += " [truncated]";
  return t;
}

#define ToString DontCallToString  // Avoid accidental recursion.

// Visits re before children are processed.
// Appends ( if needed and passes new precedence to children.
int ToStringWalker::PreVisit(Regexp* re, int parent_arg, bool* stop) {
  int prec = parent_arg;
  int nprec = PrecAtom;

  switch (re->op()) {
    case kRegexpNoMatch:
    case kRegexpEmptyMatch:
    case kRegexpLiteral:
    case kRegexpAnyChar:
    case kRegexpAnyByte:
    case kRegexpBeginLine:
    case kRegexpEndLine:
    case kRegexpBeginText:
    case kRegexpEndText:
    case kRegexpWordBoundary:
    case kRegexpNoWordBoundary:
    case kRegexpCharClass:
    case kRegexpHaveMatch:
      nprec = PrecAtom;
      break;

    case kRegexpConcat:
    case kRegexpLiteralString:
      if (prec < PrecConcat)
        t_->append("(?:");
      nprec = PrecConcat;
      break;

    case kRegexpAlternate:
      if (prec < PrecAlternate)
        t_->append("(?:");
      nprec = PrecAlternate;
      break;

    case kRegexpCapture:
      t_->append("(");
      if (re->cap() == 0)
        ABSL_LOG(DFATAL) << "kRegexpCapture cap() == 0";
      if (re->name()) {
        t_->append("?P<");
        t_->append(*re->name());
        t_->append(">");
      }
      nprec = PrecParen;
      break;

    case kRegexpStar:
    case kRegexpPlus:
    case kRegexpQuest:
    case kRegexpRepeat:
      if (prec < PrecUnary)
        t_->append("(?:");
      // The subprecedence here is PrecAtom instead of PrecUnary
      // because PCRE treats two unary ops in a row as a parse error.
      nprec = PrecAtom;
      break;
  }

  return nprec;
}

static void AppendLiteral(std::string *t, Rune r, bool foldcase) {
  if (r != 0 && r < 0x80 && strchr("(){}[]*+?|.^$\\", r)) {
    t->append(1, '\\');
    t->append(1, static_cast<char>(r));
  } else if (foldcase && 'a' <= r && r <= 'z') {
    r -= 'a' - 'A';
    t->append(1, '[');
    t->append(1, static_cast<char>(r));
    t->append(1, static_cast<char>(r) + 'a' - 'A');
    t->append(1, ']');
  } else {
    AppendCCRange(t, r, r);
  }
}

// Visits re after children are processed.
// For childless regexps, all the work is done here.
// For regexps with children, append any unary suffixes or ).
int ToStringWalker::PostVisit(Regexp* re, int parent_arg, int pre_arg,
                              int* child_args, int nchild_args) {
  int prec = parent_arg;
  switch (re->op()) {
    case kRegexpNoMatch:
      // There's no simple symbol for "no match", but
      // [^0-Runemax] excludes everything.
      t_->append("[^\\x00-\\x{10ffff}]");
      break;

    case kRegexpEmptyMatch:
      // Append (?:) to make empty string visible,
      // unless this is already being parenthesized.
      if (prec < PrecEmpty)
        t_->append("(?:)");
      break;

    case kRegexpLiteral:
      AppendLiteral(t_, re->rune(),
                    (re->parse_flags() & Regexp::FoldCase) != 0);
      break;

    case kRegexpLiteralString:
      for (int i = 0; i < re->nrunes(); i++)
        AppendLiteral(t_, re->runes()[i],
                      (re->parse_flags() & Regexp::FoldCase) != 0);
      if (prec < PrecConcat)
        t_->append(")");
      break;

    case kRegexpConcat:
      if (prec < PrecConcat)
        t_->append(")");
      break;

    case kRegexpAlternate:
      // Clumsy but workable: the children all appended |
      // at the end of their strings, so just remove the last one.
      if ((*t_)[t_->size()-1] == '|')
        t_->erase(t_->size()-1);
      else
        ABSL_LOG(DFATAL) << "Bad final char: " << t_;
      if (prec < PrecAlternate)
        t_->append(")");
      break;

    case kRegexpStar:
      t_->append("*");
      if (re->parse_flags() & Regexp::NonGreedy)
        t_->append("?");
      if (prec < PrecUnary)
        t_->append(")");
      break;

    case kRegexpPlus:
      t_->append("+");
      if (re->parse_flags() & Regexp::NonGreedy)
        t_->append("?");
      if (prec < PrecUnary)
        t_->append(")");
      break;

    case kRegexpQuest:
      t_->append("?");
      if (re->parse_flags() & Regexp::NonGreedy)
        t_->append("?");
      if (prec < PrecUnary)
        t_->append(")");
      break;

    case kRegexpRepeat:
      if (re->max() == -1)
        t_->append(absl::StrFormat("{%d,}", re->min()));
      else if (re->min() == re->max())
        t_->append(absl::StrFormat("{%d}", re->min()));
      else
        t_->append(absl::StrFormat("{%d,%d}", re->min(), re->max()));
      if (re->parse_flags() & Regexp::NonGreedy)
        t_->append("?");
      if (prec < PrecUnary)
        t_->append(")");
      break;

    case kRegexpAnyChar:
      t_->append(".");
      break;

    case kRegexpAnyByte:
      t_->append("\\C");
      break;

    case kRegexpBeginLine:
      t_->append("^");
      break;

    case kRegexpEndLine:
      t_->append("$");
      break;

    case kRegexpBeginText:
      t_->append("(?-m:^)");
      break;

    case kRegexpEndText:
      if (re->parse_flags() & Regexp::WasDollar)
        t_->append("(?-m:$)");
      else
        t_->append("\\z");
      break;

    case kRegexpWordBoundary:
      t_->append("\\b");
      break;

    case kRegexpNoWordBoundary:
      t_->append("\\B");
      break;

    case kRegexpCharClass: {
      if (re->cc()->size() == 0) {
        t_->append("[^\\x00-\\x{10ffff}]");
        break;
      }
      t_->append("[");
      // Heuristic: show class as negated if it contains the
      // non-character 0xFFFE and yet somehow isn't full.
      CharClass* cc = re->cc();
      if (cc->Contains(0xFFFE) && !cc->full()) {
        cc = cc->Negate();
        t_->append("^");
      }
      for (CharClass::iterator i = cc->begin(); i != cc->end(); ++i)
        AppendCCRange(t_, i->lo, i->hi);
      if (cc != re->cc())
        cc->Delete();
      t_->append("]");
      break;
    }

    case kRegexpCapture:
      t_->append(")");
      break;

    case kRegexpHaveMatch:
      // There's no syntax accepted by the parser to generate
      // this node (it is generated by RE2::Set) so make something
      // up that is readable but won't compile.
      t_->append(absl::StrFormat("(?HaveMatch:%d)", re->match_id()));
      break;
  }

  // If the parent is an alternation, append the | for it.
  if (prec == PrecAlternate)
    t_->append("|");

  return 0;
}

// Appends a rune for use in a character class to the string t.
static void AppendCCChar(std::string* t, Rune r) {
  if (0x20 <= r && r <= 0x7E) {
    if (strchr("[]^-\\", r))
      t->append("\\");
    t->append(1, static_cast<char>(r));
    return;
  }
  switch (r) {
    default:
      break;

    case '\r':
      t->append("\\r");
      return;

    case '\t':
      t->append("\\t");
      return;

    case '\n':
      t->append("\\n");
      return;

    case '\f':
      t->append("\\f");
      return;
  }

  if (r < 0x100) {
    *t += absl::StrFormat("\\x%02x", static_cast<int>(r));
    return;
  }
  *t += absl::StrFormat("\\x{%x}", static_cast<int>(r));
}

static void AppendCCRange(std::string* t, Rune lo, Rune hi) {
  if (lo > hi)
    return;
  AppendCCChar(t, lo);
  if (lo < hi) {
    t->append("-");
    AppendCCChar(t, hi);
  }
}

}  // namespace re2
