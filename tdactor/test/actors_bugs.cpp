//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/actor/actor.h"
#include "td/actor/Timeout.h"

using namespace td;

TEST(MultiTimeout, bug) {
  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  sched.start();
  std::unique_ptr<MultiTimeout> multi_timeout;
  struct Data {
    MultiTimeout *multi_timeout;
  };
  Data data;

  {
    auto guard = sched.get_current_guard();
    multi_timeout = std::make_unique<MultiTimeout>("MultiTimeout");
    data.multi_timeout = multi_timeout.get();
    multi_timeout->set_callback([](void *void_data, int64 key) {
      auto &data = *static_cast<Data *>(void_data);
      if (key == 1) {
        data.multi_timeout->cancel_timeout(key + 1);
        data.multi_timeout->set_timeout_in(key + 2, 1);
      } else {
        Scheduler::instance()->finish();
      }
    });
    multi_timeout->set_callback_data(&data);
    multi_timeout->set_timeout_in(1, 1);
    multi_timeout->set_timeout_in(2, 2);
  }

  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}
