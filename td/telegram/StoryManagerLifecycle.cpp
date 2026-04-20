//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/StoryContent.h"
#include "td/telegram/StoryForwardInfo.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

namespace td {

void StoryManager::on_story_reload_timeout_callback(void *story_manager_ptr, int64 story_global_id) {
  if (G()->close_flag()) {
    return;
  }

  auto story_manager = static_cast<StoryManager *>(story_manager_ptr);
  send_closure_later(story_manager->actor_id(story_manager), &StoryManager::on_story_reload_timeout, story_global_id);
}

void StoryManager::on_story_reload_timeout(int64 story_global_id) {
  if (G()->close_flag()) {
    return;
  }

  auto story_full_id = stories_by_global_id_.get(story_global_id);
  auto story = get_story(story_full_id);
  if (story == nullptr || opened_stories_.count(story_full_id) == 0) {
    LOG(INFO) << "There is no need to reload " << story_full_id;
    return;
  }

  reload_story(story_full_id, Promise<Unit>(), "on_story_reload_timeout");
  story_reload_timeout_.set_timeout_in(story_global_id, OPENED_STORY_POLL_PERIOD);
}

void StoryManager::on_story_expire_timeout_callback(void *story_manager_ptr, int64 story_global_id) {
  if (G()->close_flag()) {
    return;
  }

  auto story_manager = static_cast<StoryManager *>(story_manager_ptr);
  send_closure_later(story_manager->actor_id(story_manager), &StoryManager::on_story_expire_timeout, story_global_id);
}

void StoryManager::on_story_expire_timeout(int64 story_global_id) {
  if (G()->close_flag()) {
    return;
  }

  auto story_full_id = stories_by_global_id_.get(story_global_id);
  auto story = get_story(story_full_id);
  if (story == nullptr || story->is_live_) {
    return;
  }
  if (is_active_story(story)) {
    // timeout used monotonic time instead of wall clock time
    LOG(INFO) << "Receive timeout for non-expired " << story_full_id << ": expire_date = " << story->expire_date_
              << ", current time = " << G()->unix_time();
    return set_story_expire_timeout(story);
  }

  LOG(INFO) << "Have expired " << story_full_id;
  auto owner_dialog_id = story_full_id.get_dialog_id();
  CHECK(owner_dialog_id.is_valid());
  if (story->content_ != nullptr && !can_access_expired_story(owner_dialog_id, story)) {
    on_delete_story(story_full_id);  // also updates active stories
  } else {
    auto active_stories = get_active_stories(owner_dialog_id);
    if (active_stories != nullptr && contains(active_stories->story_ids_, story_full_id.get_story_id())) {
      auto story_ids = active_stories->story_ids_;
      on_update_active_stories(owner_dialog_id, active_stories->max_read_story_id_, std::move(story_ids),
                               Promise<Unit>(), "on_story_expire_timeout");
    }
  }
}

void StoryManager::on_story_can_get_viewers_timeout_callback(void *story_manager_ptr, int64 story_global_id) {
  if (G()->close_flag()) {
    return;
  }

  auto story_manager = static_cast<StoryManager *>(story_manager_ptr);
  send_closure_later(story_manager->actor_id(story_manager), &StoryManager::on_story_can_get_viewers_timeout,
                     story_global_id);
}

void StoryManager::on_story_can_get_viewers_timeout(int64 story_global_id) {
  if (G()->close_flag()) {
    return;
  }

  auto story_full_id = stories_by_global_id_.get(story_global_id);
  auto story = get_story(story_full_id);
  if (story == nullptr) {
    return;
  }

  LOG(INFO) << "Have expired viewers in " << story_full_id;
  if (has_unexpired_viewers(story_full_id, story)) {
    // timeout used monotonic time instead of wall clock time
    // also a reaction could have been added on the story
    LOG(INFO) << "Receive timeout for " << story_full_id
              << " with available viewers: expire_date = " << story->expire_date_
              << ", current time = " << G()->unix_time();
    return set_story_can_get_viewers_timeout(story);
  }

  // can_get_viewers flag could have been changed; reload the story to repair it
  reload_story(story_full_id, Promise<Unit>(), "on_story_can_get_viewers_timeout");
}

void StoryManager::load_expired_database_stories() {
  if (!G()->use_message_database()) {
    if (!td_->auth_manager_->is_bot()) {
      set_timeout_in(Random::fast(300, 420));
    }
    return;
  }

  LOG(INFO) << "Load " << load_expired_database_stories_next_limit_ << " expired stories";
  G()->td_db()->get_story_db_async()->get_expiring_stories(
      G()->unix_time() - 1, load_expired_database_stories_next_limit_,
      PromiseCreator::lambda([actor_id = actor_id(this)](Result<vector<StoryDbStory>> r_stories) {
        if (G()->close_flag()) {
          return;
        }
        CHECK(r_stories.is_ok());
        send_closure(actor_id, &StoryManager::on_load_expired_database_stories, r_stories.move_as_ok());
      }));
}

void StoryManager::on_load_expired_database_stories(vector<StoryDbStory> stories) {
  if (G()->close_flag()) {
    return;
  }

  int32 next_request_delay;
  if (stories.size() == static_cast<size_t>(load_expired_database_stories_next_limit_)) {
    CHECK(load_expired_database_stories_next_limit_ < (1 << 30));
    load_expired_database_stories_next_limit_ *= 2;
    next_request_delay = 1;
  } else {
    load_expired_database_stories_next_limit_ = DEFAULT_LOADED_EXPIRED_STORIES;
    next_request_delay = Random::fast(300, 420);
  }
  set_timeout_in(next_request_delay);

  LOG(INFO) << "Receive " << stories.size() << " expired stories with next request in " << next_request_delay
            << " seconds";
  for (auto &database_story : stories) {
    auto story = parse_story(database_story.story_full_id_, std::move(database_story.data_));
    if (story != nullptr) {
      LOG(ERROR) << "Receive non-expired " << database_story.story_full_id_;
    }
  }
}

}  // namespace td
