// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstring>
#include <iostream>
#include <string>

#include "refcount_struct.h"

namespace {

class Foo {
 public:
  Foo(char* buf, size_t len, int& counter, const char* text)
      : counter_(counter), buf_(buf) {
    strncpy(buf, text, len);
    buf[len - 1] = '\0';
    counter_++;
  }
  virtual ~Foo() {
    counter_--;
    std::cout << "Destructor called" << std::endl;
  }

  const char* text() const { return buf_; }

 private:
  int& counter_;
  const char* buf_;
};

}  // namespace

int main() {
  using refptr::New;
  using refptr::NewWithBlock;
  using refptr::Shared;
  using refptr::Unique;

  int counter = 0;
  { Unique<char> owned_char(New<char>()); }
  {
    Unique<Foo> owned(NewWithBlock<Foo, int&, const char*>(
        16, counter, "Lorem ipsum dolor sit amet"));
    assert(counter == 1);
    std::cout << owned->text() << std::endl;
    Shared<Foo> shared(std::move(owned).Share());
    assert(counter == 1);
    std::cout << shared->text() << std::endl;
    auto owned_opt = std::move(shared).AttemptToClaim();
    assert(counter == 1);
    assert(("Attempt to claim ownership failed", owned_opt));
    owned = *std::move(owned_opt);
    std::cout << owned->text() << std::endl;
  }
  assert(counter == 0);
  {
    auto self_owned =
        refptr::SelfOwned<Foo>::Make(16, counter, "Lorem ipsum dolor sit amet");
    std::cout << (*self_owned)->text() << std::endl;
    assert(counter == 1);
  }
  assert(counter == 0);
  return 0;
}
