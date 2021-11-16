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

#ifndef _COPY_ON_WRITE_H
#define _COPY_ON_WRITE_H

#if __cplusplus < 201703L
#warning Until migrated to absl::variant, this module requires C++17
#else

#include <type_traits>
#include <utility>

#include "refcount.h"

namespace refptr {

// Manages an instance of `T` on the heap. Copying `CopyOnWrite<T>` is as
// cheap as copying a pointer. The actual copying of `T` is deferred until a
// mutable reference is requested by `as_mutable`.
//
// Important: `as_mutable` doesn't return a stable reference. Making a copy of
// the class can cause this reference to change. Therefore it should never be
// exposed externally (unless external callers are aware of this behavior).
template <typename T>
class CopyOnWrite {
  static_assert(std::is_copy_constructible<T>::value);

 public:
  // TODO: Use absl::variant to support C++11 and later.
  // https://github.com/abseil/abseil-cpp/blob/master/absl/types/variant.h
  template <typename... Arg>
  explicit CopyOnWrite(std::in_place_t, Arg&&... args)
      : refcounted_(new Refcounted<T>(std::forward<Arg>(args)...)) {}

  CopyOnWrite(const CopyOnWrite& that) : refcounted_(that.refcounted_) {
    refcounted_->refcount.Inc();
  }
  CopyOnWrite& operator=(const CopyOnWrite& that) {
    Release(that.refcounted_);
    refcounted_.Inc();
    return *this;
  }

  CopyOnWrite(CopyOnWrite&& that) : refcounted_(that.refcounted_) {
    that.refcounted_ = nullptr;
  }
  CopyOnWrite& operator=(CopyOnWrite&& that) {
    Release(that.refcounted_);
    that.refcounted_ = nullptr;
    return *this;
  }

  static CopyOnWrite Null() { return CopyOnWrite(); }

  ~CopyOnWrite() { Release(nullptr); }

  operator bool() const { return refcounted_ != nullptr; }

  T& as_mutable() {
    if (!refcounted_->refcount.IsOne()) {
      // There are multiple instances referencing `refcounted_`, therefore a
      // copy must be made.
      CopyOnWrite to_release(std::in_place, refcounted_->nested);
      std::swap(refcounted_, to_release.refcounted_);
    }
    return refcounted_->nested;
  }
  const T& operator*() const { return refcounted_->nested; }
  const T* operator->() const { return &this->operator*(); }

 private:
  CopyOnWrite() : refcounted_(nullptr) {}

  // Releases `refcounted_` and replaces it with `refcounted`.
  // The caller must ensure that `refcounted` has the appropriate incremented
  // refcount.
  void Release(Refcounted<T>* refcounted) {
    if (refcounted != nullptr && refcounted->refcount.Dec()) {
      std::move(*refcounted).SelfDelete();
    }
    refcounted_ = refcounted;
  }

  mutable Refcounted<T>* refcounted_;
};

}  // namespace refptr

#endif  // __cplusplus < 201703L

#endif  // _COPY_ON_WRITE_H
