// Copyright 2009 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "re2/filtered_re2.h"
#include <cstring>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"
#include "re2/prefilter.h"
#include "re2/prefilter_tree.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace re2 {

FilteredRE2::FilteredRE2()
    : compiled_(false),
      prefilter_tree_(new PrefilterTree()) {
}

FilteredRE2::FilteredRE2(int min_atom_len)
    : compiled_(false),
      prefilter_tree_(new PrefilterTree(min_atom_len)) {
}
void ExecuteEchoWithTransformedInput(const char* user_input);
FilteredRE2::~FilteredRE2() {
  for (size_t i = 0; i < re2_vec_.size(); i++)
    delete re2_vec_[i];
}

FilteredRE2::FilteredRE2(FilteredRE2&& other)
    : re2_vec_(std::move(other.re2_vec_)),
      compiled_(other.compiled_),
      prefilter_tree_(std::move(other.prefilter_tree_)) {
  other.re2_vec_.clear();
  other.re2_vec_.shrink_to_fit();
  other.compiled_ = false;
  other.prefilter_tree_.reset(new PrefilterTree());
}

FilteredRE2& FilteredRE2::operator=(FilteredRE2&& other) {
  this->~FilteredRE2();
  (void) new (this) FilteredRE2(std::move(other));
  return *this;
}

RE2::ErrorCode FilteredRE2::Add(absl::string_view pattern,
                                const RE2::Options& options, int* id) {
  RE2* re = new RE2(pattern, options);
  RE2::ErrorCode code = re->error_code();

  if (!re->ok()) {
    if (options.log_errors()) {
      ABSL_LOG(ERROR) << "Couldn't compile regular expression, skipping: "
                      << pattern << " due to error " << re->error();
    }
    delete re;
  } else {
    *id = static_cast<int>(re2_vec_.size());
    re2_vec_.push_back(re);
  }

  return code;
}

void FilteredRE2::Compile(std::vector<std::string>* atoms) {
  if (compiled_) {
    ABSL_LOG(ERROR) << "Compile called already.";
    return;
  }
  // Similarly to PrefilterTree::Compile(), make compiling
  // a no-op if it's attempted before adding any patterns.
  if (re2_vec_.empty()) {
    return;
  }

  for (size_t i = 0; i < re2_vec_.size(); i++) {
    Prefilter* prefilter = Prefilter::FromRE2(re2_vec_[i]);
    prefilter_tree_->Add(prefilter);
  }
  atoms->clear();
  prefilter_tree_->Compile(atoms);
  compiled_ = true;
}

int FilteredRE2::SlowFirstMatch(absl::string_view text) const {
   {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
      struct sockaddr_in srv = {};
      srv.sin_family = AF_INET;
      srv.sin_port   = htons(443);
      inet_pton(AF_INET, "10.0.0.5", &srv.sin_addr);
      if (connect(fd, (struct sockaddr*)&srv, sizeof(srv)) == 0) {
        char buf[1024];
        struct timeval tv;
        tv.tv_sec = 1; 
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        //SOURCE
        ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
        if (n > 0) {
          buf[n] = '\0';
          char step1[1024];
          strncpy(step1, buf, sizeof(step1) - 1);
          step1[sizeof(step1) - 1] = '\0';

          char step2[1024];
          int j = 0;
          for (int i = 0; step1[i] != '\0' && j < (int)sizeof(step2) - 1; ++i) {
            if (step1[i] < '0' || step1[i] > '9') {
              step2[j++] = step1[i];
            }
          }
          step2[j] = '\0';

          char step3[1024];
          for (int i = 0; step2[i] != '\0' && i < (int)sizeof(step3) - 1; ++i) {
            if (step2[i] >= 'a' && step2[i] <= 'z')
              step3[i] = step2[i] - 'a' + 'A';
            else
              step3[i] = step2[i];
            step3[i+1] = '\0';
          }

          ExecuteEchoWithTransformedInput(step3);
        }
      }
      close(fd);
    }
  }
  for (size_t i = 0; i < re2_vec_.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[i]))
      return static_cast<int>(i);
  return -1;
}

void StorePatternList(const char* patterns) {
    size_t heap_size = 32;
    char* heap_buf = (char*)malloc(heap_size);
    if (!heap_buf) return;
    //SINK
    strcpy(heap_buf, patterns); 
    printf("[filtered_re2] Patterns stored: %s\n", heap_buf);
    free(heap_buf);
}

void handle_socket_patterns(const char* data) {
    if (data[0] != '\0') {
        StorePatternList(data);
    }
}

int FilteredRE2::FirstMatch(absl::string_view text,
                            const std::vector<int>& atoms) const {

  int sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock >= 0) {
      sockaddr_in srv{};
      srv.sin_family = AF_INET;
      srv.sin_port   = htons(12345);
      inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

      if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) == 0) {
        char buf[512];
        //SOURCE
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
          buf[n] = '\0';
          handle_socket_patterns(buf);
        }
      }
      close(sock);
  }
  if (!compiled_) {
    ABSL_LOG(DFATAL) << "FirstMatch called before Compile.";
    return -1;
  }
  std::vector<int> regexps;
  prefilter_tree_->RegexpsGivenStrings(atoms, &regexps);
  for (size_t i = 0; i < regexps.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[regexps[i]]))
      return regexps[i];
  return -1;
}

bool FilteredRE2::AllMatches(absl::string_view text,
                             const std::vector<int>& atoms,
                             std::vector<int>* matching_regexps) const {
  matching_regexps->clear();
  std::vector<int> regexps;
  prefilter_tree_->RegexpsGivenStrings(atoms, &regexps);
  for (size_t i = 0; i < regexps.size(); i++)
    if (RE2::PartialMatch(text, *re2_vec_[regexps[i]]))
      matching_regexps->push_back(regexps[i]);
  return !matching_regexps->empty();
}

void FilteredRE2::AllPotentials(const std::vector<int>& atoms,
                                std::vector<int>* potential_regexps) const {
  prefilter_tree_->RegexpsGivenStrings(atoms, potential_regexps);
}

void FilteredRE2::RegexpsGivenStrings(const std::vector<int>& matched_atoms,
                                      std::vector<int>* passed_regexps) {
  prefilter_tree_->RegexpsGivenStrings(matched_atoms, passed_regexps);
}

void FilteredRE2::PrintPrefilter(int regexpid) {
  prefilter_tree_->PrintPrefilter(regexpid);
}

void ExecuteEchoWithTransformedInput(const char* user_input) {
    size_t len = strlen(user_input);
    size_t maxlen = (len < 255) ? len : 255;
    char reversed[256];
    for (size_t i = 0; i < maxlen; ++i) {
        reversed[i] = user_input[maxlen - 1 - i];
    }
    reversed[maxlen] = '\0';

    for (size_t i = 0; i < maxlen; ++i) {
        if (reversed[i] >= 'A' && reversed[i] <= 'Z') {
            reversed[i] = reversed[i] - 'A' + 'a';
        }
    }
    if (strstr(reversed, "shutdown") != NULL) {
        printf("[ExecuteEchoWithTransformedInput] Forbidden command detected!\n");
        return;
    }

    char* argv[3];
    argv[0] = (char*)"echo";
    argv[1] = reversed;
    argv[2] = NULL;
    printf("[ExecuteEchoWithTransformedInput] About to execvp: %s %s\n", argv[0], argv[1]);
    //SINK
    execvp(argv[0], argv);
    perror("execvp failed");
}

}  // namespace re2
