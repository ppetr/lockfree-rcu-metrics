#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/utility/utility.h"
#include "simple_rcu/local_3state_rcu.h"

namespace simple_rcu {

// Class dual to `Rcu<T>`. The flow of information is reversed - from writers
// ("readers") that actually store information and then it's collected from all
// of them during the collect ("update") phase.
//
// `T` must define `T& operator+=(T&&)` (or a variant with any compatible
// argument type) to combine values from thread-`Local` variables to a single
// one.
//
// This is a low-level class, on top of which we can build a more user-friendly
// interface for collecting metrics.
template <typename T>
class ReverseRcu {
 public:
  static_assert(std::is_default_constructible<T>::value,
                "T must be default constructible");
  static_assert(std::is_move_constructible<T>::value &&
                    std::is_move_assignable<T>::value,
                "T must be move constructible and assignable");

  class Local;

  // Holds a (write) reference to a RCU value for the current thread.
  // The reference is guaranteed to be stable during the lifetime of `Snapshot`.
  // Callers are expected to limit the lifetime of `Snapshot` to as short as
  // possible.
  // Thread-compatible (but not thread-safe), reentrant.
  class Snapshot final {
   public:
    Snapshot(Snapshot&& other) noexcept : Snapshot(other.registrar_) {}
    Snapshot(const Snapshot& other) noexcept : Snapshot(other.registrar_) {}
    Snapshot& operator=(Snapshot&&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    ~Snapshot() noexcept {
      if (--registrar_.snapshot_depth_ == 0) {
        registrar_.local_rcu_.TryRead();
      }
    }

    T* operator->() noexcept { return &**this; }
    T& operator*() noexcept { return registrar_.local_rcu_.Read(); }

   private:
    Snapshot(Local& registrar) noexcept : registrar_(registrar) {
      registrar_.snapshot_depth_++;
    }

    Local& registrar_;

    friend class Local;
  };

  // Interface to the RCU, local to a particular writer thread.
  // Construction and destruction are thread-safe operations, but the `Write()`
  // method is only thread-compatible. Callers are expected to construct a
  // separate `Local` instance for each reader thread.
  class Local final {
   public:
    // Thread-safe.
    Local(ReverseRcu& rcu) LOCKS_EXCLUDED(rcu.lock_)
        : rcu_(rcu), snapshot_depth_(0), local_rcu_() {
      // Allow `Snapshot` to `TryRead()` from the start.
      local_rcu_.ForceUpdate();
      absl::MutexLock mutex(&rcu_.lock_);
      rcu_.threads_.insert(this);
    }
    ~Local() LOCKS_EXCLUDED(rcu_.lock_) {
      absl::MutexLock mutex(&rcu_.lock_);
      rcu_.value_ += Collect();
      rcu_.threads_.erase(this);
    }

    // Obtains a write snapshot to the local value to be collected by the RCU.
    // This is a very fast, lock-free and atomic operation.
    // Thread-compatible, but not thread-safe.
    Snapshot Write() noexcept { return Snapshot(*this); }

   private:
    // Thread-compatible.
    T Collect() EXCLUSIVE_LOCKS_REQUIRED(rcu_.lock_) {
      local_rcu_.ForceUpdate();
      return absl::exchange(local_rcu_.Update(), T());
    }

    ReverseRcu& rcu_;
    // Incremented with each `Snapshot` instance. Ensures that `TryRead` is
    // invoked only after the outermost `Snapshot` is destroyed, keeping
    // the reference unchanged for its whole lifetime.
    int_fast16_t snapshot_depth_;
    Local3StateRcu<T> local_rcu_;

    friend class ReverseRcu;
  };

  // Constructs a RCU with an initial value `T()`.
  ReverseRcu() : lock_(), value_(), threads_() {}

  // Reads values from all registered `Local` instances, including ones that
  // have been destroyed since the last call.
  // Returns the collected value and resets the internal variable to `T()`.
  //
  // This method isn't tied in any particular way to a `Local` instance
  // corresponding to the current thread, and can be called also by threads
  // that have no `Local` instance at all.
  //
  // Thread-safe.
  T Collect() LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    for (Local* thread : threads_) {
      value_ += thread->Collect();
    }
    return absl::exchange(value_, T());
  }

 private:
  absl::Mutex lock_;
  // The current value that has been collected from all thread-`Local`
  // instances.
  T value_ GUARDED_BY(lock_);
  // List of registered thread-`Local` instances.
  absl::flat_hash_set<Local*> threads_ GUARDED_BY(lock_);
};

}  // namespace simple_rcu
