// Copyright 2022 Google LLC
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

#include "three_state_rcu.h"

#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

TEST(ThreeStateRcuTest, UpdateAndRead) {
  ThreeStateRcu<int> rcu;
  // Set up a new value in `Update()`.
  EXPECT_NE(&rcu.Update(), &rcu.Read())
      << "Update and Read must never point to the same object";
  EXPECT_EQ(rcu.Update(), 0);
  EXPECT_EQ(rcu.Read(), 0);
  rcu.Update() = 42;
  // Trigger.
  EXPECT_FALSE(rcu.TriggerUpdate()) << "Read hasn't advanced yet";
  // Verify expectations before and after `TriggerRead()`.
  EXPECT_NE(&rcu.Update(), &rcu.Read())
      << "Update and Read must never point to the same object";
  EXPECT_EQ(rcu.Update(), 0);
  EXPECT_EQ(rcu.Read(), 0);
  EXPECT_TRUE(rcu.TriggerRead());
  EXPECT_NE(&rcu.Update(), &rcu.Read())
      << "Update and Read must never point to the same object";
  EXPECT_EQ(rcu.Read(), 42);
}

TEST(ThreeStateRcuTest, AlternatingUpdatesAndReads) {
  ThreeStateRcu<int> rcu;
  rcu.Read() = 1;
  rcu.TriggerRead();
  for (int i = 1; i <= 10; i++) {
    SCOPED_TRACE(i);
    rcu.Update() = i;
    EXPECT_TRUE(rcu.TriggerUpdate()) << "Read should have advanced";
    EXPECT_EQ(rcu.Update(), -(i - 2))
        << "Update() should now hold the value to be reclaimed";
    EXPECT_EQ(rcu.Read(), -(i - 1))
        << "Read() should still point to the previous value";
    EXPECT_TRUE(rcu.TriggerRead());
    EXPECT_EQ(rcu.Read(), i) << "Read() should now point to the new value";
    EXPECT_FALSE(rcu.TriggerRead());
    EXPECT_EQ(rcu.Read(), i) << "Read() should still point to the new value";
    rcu.Read() = -i;
  }
}

}  // namespace
}  // namespace simple_rcu
