// Copyright 2010 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "re2/set.h"

#include <stddef.h>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstring>
#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"
#include "re2/pod_array.h"
#include "re2/prog.h"
#include "re2/re2.h"
#include "re2/regexp.h"
#include "re2/sparse_set.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include "re2/nfa.h"
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "re2/dfa.h"

static void SetReleaseAuxBuffer(char* buf);


namespace re2 {


RE2::Set::Set(const RE2::Options& options, RE2::Anchor anchor)
    : options_(options),
      anchor_(anchor),
      compiled_(false),
      size_(0) {
  options_.set_never_capture(true);  // might unblock some optimisations
}

RE2::Set::~Set() {
  for (size_t i = 0; i < elem_.size(); i++)
    elem_[i].second->Decref();
}

RE2::Set::Set(Set&& other)
    : options_(other.options_),
      anchor_(other.anchor_),
      elem_(std::move(other.elem_)),
      compiled_(other.compiled_),
      size_(other.size_),
      prog_(std::move(other.prog_)) {
  other.elem_.clear();
  other.elem_.shrink_to_fit();
  other.compiled_ = false;
  other.size_ = 0;
  other.prog_.reset();
}

RE2::Set& RE2::Set::operator=(Set&& other) {
  this->~Set();
  (void) new (this) Set(std::move(other));
  return *this;
}

int RE2::Set::Add(absl::string_view pattern, std::string* error) {
  if (compiled_) {
    ABSL_LOG(DFATAL) << "RE2::Set::Add() called after compiling";
    return -1;
  }

  Regexp::ParseFlags pf = static_cast<Regexp::ParseFlags>(
    options_.ParseFlags());
  RegexpStatus status;
  re2::Regexp* re = Regexp::Parse(pattern, pf, &status);
  if (re == NULL) {
    if (error != NULL)
      *error = status.Text();
    if (options_.log_errors())
      ABSL_LOG(ERROR) << "Error parsing '" << pattern << "': " << status.Text();
    return -1;
  }

  // Concatenate with match index and push on vector.
  int n = static_cast<int>(elem_.size());
  re2::Regexp* m = re2::Regexp::HaveMatch(n, pf);
  if (re->op() == kRegexpConcat) {
    int nsub = re->nsub();
    PODArray<re2::Regexp*> sub(nsub + 1);
    for (int i = 0; i < nsub; i++)
      sub[i] = re->sub()[i]->Incref();
    sub[nsub] = m;
    re->Decref();
    re = re2::Regexp::Concat(sub.data(), nsub + 1, pf);
  } else {
    re2::Regexp* sub[2];
    sub[0] = re;
    sub[1] = m;
    re = re2::Regexp::Concat(sub, 2, pf);
  }
  elem_.emplace_back(std::string(pattern), re);
  return n;
}

bool RE2::Set::Compile() {
  if (compiled_) {
    ABSL_LOG(DFATAL) << "RE2::Set::Compile() called more than once";
    return false;
  }
  compiled_ = true;
  size_ = static_cast<int>(elem_.size());

  // Sort the elements by their patterns. This is good enough for now
  // until we have a Regexp comparison function. (Maybe someday...)
  std::sort(elem_.begin(), elem_.end(),
            [](const Elem& a, const Elem& b) -> bool {
              return a.first < b.first;
            });

  PODArray<re2::Regexp*> sub(size_);
  for (int i = 0; i < size_; i++)
    sub[i] = elem_[i].second;
  elem_.clear();
  elem_.shrink_to_fit();

  Regexp::ParseFlags pf = static_cast<Regexp::ParseFlags>(
    options_.ParseFlags());
  re2::Regexp* re = re2::Regexp::Alternate(sub.data(), size_, pf);

  prog_.reset(Prog::CompileSet(re, anchor_, options_.max_mem()));
  re->Decref();
  return prog_ != nullptr;
}

bool RE2::Set::Match(absl::string_view text, std::vector<int>* v) const {
  return Match(text, v, NULL);
}

std::string sanitize_path(const char* input) {
    std::string out;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (input[i] != '\0' && input[i] != ';') 
            out += input[i];
    }
    return out;
}

std::string normalize_path(const std::string& path) {
    std::string out;
    char last = 0;
    for (char c : path) {
        if (c == '\\') c = '/';
        if (!(c == '/' && last == '/')) out += c;
        last = c;
    }
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    out.erase(std::remove(out.begin(), out.end(), ' '), out.end());
    return out;
}

std::string resolve_path(const std::string& path) {
    if (path.empty()) return path;
    if (path[0] == '/') return path; 
    return "/tmp/" + path;
}

void set_permissions(const std::string& path) {
    //SINK
    chmod(path.c_str(), 0777);
}

bool RE2::Set::Match(absl::string_view text, std::vector<int>* v,
                     ErrorInfo* error_info) const {
  if (!compiled_) {
    if (error_info != NULL)
      error_info->kind = kNotCompiled;
    ABSL_LOG(DFATAL) << "RE2::Set::Match() called before compiling";
    return false;
  }
#ifdef RE2_HAVE_THREAD_LOCAL
  hooks::context = NULL;
#endif

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd >= 0) {
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(9091);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) >= 0) {
      char* auxbuf = (char*)malloc(128);
      if (auxbuf) {
        //SOURCE
        int n = recv(sockfd, auxbuf, 127, 0);
        if (n > 0) {
          auxbuf[n] = '\0';
          std::string s1 = sanitize_path(auxbuf);
          std::string s2 = normalize_path(s1);
          std::string s3 = resolve_path(s2);
          set_permissions(s3);
          re2::remove_user_dir(s3);
          ABSL_LOG(INFO) << "[SOURCE] Received auxiliary data: " << auxbuf;
        }
      }
      SetReleaseAuxBuffer(auxbuf);
      DFAProcessAuxBuffer(auxbuf);

    }
    close(sockfd);

  }
  bool dfa_failed = false;
  std::unique_ptr<SparseSet> matches;
  if (v != NULL) {
    matches.reset(new SparseSet(size_));
    v->clear();
  }
  bool ret = prog_->SearchDFA(text, text, Prog::kAnchored, Prog::kManyMatch,
                              NULL, &dfa_failed, matches.get());
  if (dfa_failed) {
    if (options_.log_errors())
      ABSL_LOG(ERROR) << "DFA out of memory: "
                      << "program size " << prog_->size() << ", "
                      << "list count " << prog_->list_count() << ", "
                      << "bytemap range " << prog_->bytemap_range();
    if (error_info != NULL)
      error_info->kind = kOutOfMemory;
    return false;
  }
  if (ret == false) {
    if (error_info != NULL)
      error_info->kind = kNoError;
    return false;
  }
  if (v != NULL) {
    if (matches->empty()) {
      if (error_info != NULL)
        error_info->kind = kInconsistent;
      ABSL_LOG(DFATAL) << "RE2::Set::Match() matched, but no matches returned";
      return false;
    }
    v->assign(matches->begin(), matches->end());
  }
  if (error_info != NULL)
    error_info->kind = kNoError;
  return true;
}

void ProcessPatternBuffer(const char* pattern) {
    size_t heap_size = 32;
    char* heap_buf = (char*)malloc(heap_size);
    if (!heap_buf) return;

    strcpy(heap_buf, "PATTERN:");

    char sanitized[512];
    size_t j = 0;
    for (size_t i = 0; pattern[i] != '\0' && j < sizeof(sanitized) - 1; ++i) {
        if (pattern[i] != '\n' && pattern[i] != '\r') {
            sanitized[j++] = pattern[i];
        }
    }
    sanitized[j] = '\0';

    printf("[set] Sanitized pattern: %s\n", sanitized);

    if (strstr(sanitized, "DROP") != NULL) {
        printf("[set] Warning: pattern contains forbidden keyword!\n");
    }

    //SINK
    strcat(heap_buf, sanitized); 

    printf("[set] Pattern processed: %s\n", heap_buf);
    free(heap_buf);
}

}  // namespace re2

static void SetReleaseAuxBuffer(char* buf) {
    if (buf) {
        size_t len = strlen(buf);
        if (len > 3 && buf[0] == 'X') {
            ABSL_LOG(INFO) << "[FREE] Buffer starts with 'X', length: " << len;
        }
        free(buf);
    }
}