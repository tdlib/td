//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"
#include "td/utils/SharedSlice.h"

using namespace td;

TEST(SharedSlice, Hands) {
  {
    SharedSlice h("hello");
    ASSERT_EQ("hello", h.as_slice());
    // auto g = h; // CE
    auto g = h.clone();
    ASSERT_EQ("hello", g.as_slice());
  }

  {
    SharedSlice h("hello");
    UniqueSharedSlice g(std::move(h));
    ASSERT_EQ("", h.as_slice());
    ASSERT_EQ("hello", g.as_slice());
  }
  {
    SharedSlice h("hello");
    SharedSlice t = h.clone();
    UniqueSharedSlice g(std::move(h));
    ASSERT_EQ("", h.as_slice());
    ASSERT_EQ("hello", g.as_slice());
    ASSERT_EQ("hello", t.as_slice());
  }

  {
    UniqueSharedSlice g(5);
    g.as_mutable_slice().copy_from("hello");
    SharedSlice h(std::move(g));
    ASSERT_EQ("hello", h);
    ASSERT_EQ("", g);
  }

  {
    UniqueSlice h("hello");
    UniqueSlice g(std::move(h));
    ASSERT_EQ("", h.as_slice());
    ASSERT_EQ("hello", g.as_slice());
  }

  {
    SecureString h("hello");
    SecureString g(std::move(h));
    ASSERT_EQ("", h.as_slice());
    ASSERT_EQ("hello", g.as_slice());
  }

  {
    Stage stage;
    SharedSlice a, b;
    std::vector<td::thread> threads(2);
    for (int i = 0; i < 2; i++) {
      threads[i] = td::thread([i, &stage, &a, &b] {
        for (int j = 0; j < 10000; j++) {
          if (i == 0) {
            a = SharedSlice("hello");
            b = a.clone();
          }
          stage.wait((2 * j + 1) * 2);
          if (i == 0) {
            ASSERT_EQ('h', a[0]);
            a.clear();
          } else {
            UniqueSharedSlice c(std::move(b));
            c.as_mutable_slice()[0] = '!';
          }
          stage.wait((2 * j + 2) * 2);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }
}
