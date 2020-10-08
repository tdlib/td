//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/utils/buffer.h"
#include "td/utils/Random.h"

using namespace td;

TEST(Buffer, buffer_builder) {
  {
    BufferBuilder builder;
    builder.append("b");
    builder.prepend("a");
    builder.append("c");
    ASSERT_EQ(builder.extract().as_slice(), "abc");
  }
  {
    BufferBuilder builder{"hello", 0, 0};
    ASSERT_EQ(builder.extract().as_slice(), "hello");
  }
  {
    BufferBuilder builder{"hello", 1, 1};
    builder.prepend("A ");
    builder.append(" B");
    ASSERT_EQ(builder.extract().as_slice(), "A hello B");
  }
  {
    std::string str = rand_string('a', 'z', 10000);
    auto splitted_str = rand_split(str);

    int l = Random::fast(0, static_cast<int32>(splitted_str.size() - 1));
    int r = l;
    BufferBuilder builder(splitted_str[l], 123, 1000);
    while (l != 0 || r != static_cast<int32>(splitted_str.size()) - 1) {
      if (l == 0 || (Random::fast_bool() && r != static_cast<int32>(splitted_str.size() - 1))) {
        r++;
        if (Random::fast_bool()) {
          builder.append(splitted_str[r]);
        } else {
          builder.append(BufferSlice(splitted_str[r]));
        }
      } else {
        l--;
        if (Random::fast_bool()) {
          builder.prepend(splitted_str[l]);
        } else {
          builder.prepend(BufferSlice(splitted_str[l]));
        }
      }
    }
    ASSERT_EQ(builder.extract().as_slice(), str);
  }
}
