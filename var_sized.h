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

#include <memory>
#include <new>
#include <type_traits>

#ifndef _VAR_SIZED_H
#define _VAR_SIZED_H

// Variable-sized class allocation. Allows to create a new instance of a class
// together with an array in with single memory allocation.

namespace refptr {

// Owns a block of memory large enough to store a properly aligned instance of
// `T` and additional `size` number of elements of type `A`.
//
// TODO: Rename to `VarAllocation`.
template <typename T, typename A>
class Placement {
 public:
  // Calls `~T` and deletes the corresponding `Placement`.
  // Doesn't delete the `A[]`, therefore it must be a primitive type or a
  // trivially destructible one.
  class Deleter {
   public:
    static_assert(!std::is_destructible<A>::value ||
                      std::is_trivially_destructible<A>::value,
                  "The array type must be primitive or trivially destructible");

    void operator()(T *to_delete) {
      Placement<T, A> deleter(to_delete, size_);
      deleter.Node()->~T();
    }

   private:
    Deleter(size_t size) : size_(size) {}

    size_t size_;

    friend class Placement<T, A>;
  };

  // Allocates memory for a properly aligned instance of `T`, plus additional
  // array of `size` elements of `A`.
  explicit Placement(size_t size)
      : size_(size),
        allocation_(std::allocator<Unit>().allocate(AllocatedUnits())) {
    static_assert(std::is_trivial<Placeholder>::value,
                  "Internal error: Placeholder class must be trivial");
  }
  // Creates a placement from its building blocks.
  //
  // - `ptr` must be a pointer previously obtained from `Placement::Node`.
  // - `size` must be a value previously obtained from `Placement::Release`.
  Placement(T *ptr, size_t size)
      : size_(size),
        allocation_(reinterpret_cast<char *>(ptr) -
                    offsetof(Placeholder, node)) {}
  Placement(Placement const &) = delete;
  Placement(Placement &&other) {
    allocation_ = other.allocation_;
    size_ = other.size_;
    other.allocation_ = nullptr;
  }

  ~Placement() {
    if (allocation_) {
      std::allocator<Unit>().deallocate(static_cast<Unit *>(allocation_),
                                        AllocatedUnits());
    }
  }

  // Returns a pointer to an uninitialized memory area available for an
  // instance of `T`.
  T *Node() const { return reinterpret_cast<T *>(&AsPlaceholder()->node); }
  // Returns a pointer to an uninitialized memory area available for
  // holding `size` (specified in the constructor) elements of `A`.
  A *Array() const { return reinterpret_cast<A *>(&AsPlaceholder()->array); }

  size_t Size() const { return size_; }
  // Returns the `Size()` and releases ownership of `Node()`.
  // This is used by self-owned classes for which `placement.Node() == this`.
  size_t Release() && {
    allocation_ = nullptr;
    return size_;
  }

  // Constructs a deleter for this particular `Placement`.
  // If used with a different instance, the behaivor is undefined.
  Deleter ToDeleter() && {
    allocation_ = nullptr;
    return Deleter(size_);
  }

 private:
  // Holds a properly aligned instance of `T` and an array of length 1 of `A`.
  struct Placeholder {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type node;
    // The array type must be the last one in the struct.
    typename std::aligned_storage<sizeof(A[1]), alignof(A[1])>::type array;
  };
  // Properly aligned unit used for the actual allocation.
  // It can occupy more than 1 byte, therefore we need to properly compute
  // their required number below.
  struct Unit {
    typename std::aligned_storage<1, alignof(Placeholder)>::type _;
  };

  constexpr size_t AllocatedUnits() {
    return (sizeof(Placeholder) + (size_ - 1) * sizeof(A) + sizeof(A) - 1) /
           sizeof(A);
  }

  Placeholder *AsPlaceholder() const {
    return static_cast<Placeholder *>(allocation_);
  }

  size_t size_;
  void *allocation_;
};

// Constructs a new instance of `U` in-place using the given arguments, with an
// additional block of memory of `B[length]`. A `B*` pointer to this buffer
// and its `size_t` length are passed as the first two arguments to the
// constructor of `U`.
template <typename U, typename B, typename... Arg>
inline std::unique_ptr<U, typename Placement<U, B>::Deleter> MakeUnique(
    size_t length, Arg &&... args) {
  Placement<U, B> placement(length);
  auto *node = placement.Node();
  auto *array = new (placement.Array()) char[length];
  auto *value = new (node) U(array, length, std::forward<Arg>(args)...);
  return std::unique_ptr<U, typename Placement<U, B>::Deleter>(
      value, std::move(placement).ToDeleter());
}

}  // namespace refptr

#endif  // _VAR_SIZED_H