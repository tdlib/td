//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogEventLog.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogInviteLink.h"
#include "td/telegram/DialogLocation.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/Global.h"
#include "td/telegram/GroupCallManager.h"
#include "td/telegram/GroupCallParticipant.h"
#include "td/telegram/InputGroupCallId.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageTtl.h"
#include "td/telegram/Photo.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

static td_api::object_ptr<td_api::ChatEventAction> get_chat_event_action_object(
    Td *td, ChannelId channel_id, tl_object_ptr<telegram_api::ChannelAdminLogEventAction> &&action_ptr,
    DialogId &actor_dialog_id) {
  CHECK(action_ptr != nullptr);
  switch (action_ptr->get_id()) {
    case telegram_api::channelAdminLogEventActionParticipantJoin::ID:
      return td_api::make_object<td_api::chatEventMemberJoined>();
    case telegram_api::channelAdminLogEventActionParticipantJoinByInvite::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantJoinByInvite>(action_ptr);
      DialogInviteLink invite_link(std::move(action->invite_), "channelAdminLogEventActionParticipantJoinByInvite");
      if (!invite_link.is_valid()) {
        LOG(ERROR) << "Wrong invite link: " << invite_link;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventMemberJoinedByInviteLink>(
          invite_link.get_chat_invite_link_object(td->contacts_manager_.get()));
    }
    case telegram_api::channelAdminLogEventActionParticipantJoinByRequest::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantJoinByRequest>(action_ptr);
      DialogInviteLink invite_link(std::move(action->invite_), "channelAdminLogEventActionParticipantJoinByRequest");
      UserId approver_user_id(action->approved_by_);
      if (!approver_user_id.is_valid()) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventMemberJoinedByRequest>(
          td->contacts_manager_->get_user_id_object(approver_user_id, "chatEventMemberJoinedByRequest"),
          invite_link.get_chat_invite_link_object(td->contacts_manager_.get()));
    }
    case telegram_api::channelAdminLogEventActionParticipantLeave::ID:
      return td_api::make_object<td_api::chatEventMemberLeft>();
    case telegram_api::channelAdminLogEventActionParticipantInvite::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantInvite>(action_ptr);
      DialogParticipant dialog_participant(std::move(action->participant_),
                                           td->contacts_manager_->get_channel_type(channel_id));
      if (!dialog_participant.is_valid() || dialog_participant.dialog_id_.get_type() != DialogType::User) {
        LOG(ERROR) << "Wrong invite: " << dialog_participant;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventMemberInvited>(
          td->contacts_manager_->get_user_id_object(dialog_participant.dialog_id_.get_user_id(),
                                                    "chatEventMemberInvited"),
          dialog_participant.status_.get_chat_member_status_object());
    }
    case telegram_api::channelAdminLogEventActionParticipantToggleBan::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantToggleBan>(action_ptr);
      auto channel_type = td->contacts_manager_->get_channel_type(channel_id);
      DialogParticipant old_dialog_participant(std::move(action->prev_participant_), channel_type);
      DialogParticipant new_dialog_participant(std::move(action->new_participant_), channel_type);
      if (old_dialog_participant.dialog_id_ != new_dialog_participant.dialog_id_) {
        LOG(ERROR) << old_dialog_participant.dialog_id_ << " VS " << new_dialog_participant.dialog_id_;
        return nullptr;
      }
      if (!old_dialog_participant.is_valid() || !new_dialog_participant.is_valid()) {
        LOG(ERROR) << "Wrong restrict: " << old_dialog_participant << " -> " << new_dialog_participant;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventMemberRestricted>(
          get_message_sender_object(td, old_dialog_participant.dialog_id_, "chatEventMemberRestricted"),
          old_dialog_participant.status_.get_chat_member_status_object(),
          new_dialog_participant.status_.get_chat_member_status_object());
    }
    case telegram_api::channelAdminLogEventActionParticipantToggleAdmin::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantToggleAdmin>(action_ptr);
      auto channel_type = td->contacts_manager_->get_channel_type(channel_id);
      DialogParticipant old_dialog_participant(std::move(action->prev_participant_), channel_type);
      DialogParticipant new_dialog_participant(std::move(action->new_participant_), channel_type);
      if (old_dialog_participant.dialog_id_ != new_dialog_participant.dialog_id_) {
        LOG(ERROR) << old_dialog_participant.dialog_id_ << " VS " << new_dialog_participant.dialog_id_;
        return nullptr;
      }
      if (!old_dialog_participant.is_valid() || !new_dialog_participant.is_valid() ||
          old_dialog_participant.dialog_id_.get_type() != DialogType::User) {
        LOG(ERROR) << "Wrong edit administrator: " << old_dialog_participant << " -> " << new_dialog_participant;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventMemberPromoted>(
          td->contacts_manager_->get_user_id_object(old_dialog_participant.dialog_id_.get_user_id(),
                                                    "chatEventMemberPromoted"),
          old_dialog_participant.status_.get_chat_member_status_object(),
          new_dialog_participant.status_.get_chat_member_status_object());
    }
    case telegram_api::channelAdminLogEventActionChangeTitle::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeTitle>(action_ptr);
      return td_api::make_object<td_api::chatEventTitleChanged>(std::move(action->prev_value_),
                                                                std::move(action->new_value_));
    }
    case telegram_api::channelAdminLogEventActionChangeAbout::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeAbout>(action_ptr);
      return td_api::make_object<td_api::chatEventDescriptionChanged>(std::move(action->prev_value_),
                                                                      std::move(action->new_value_));
    }
    case telegram_api::channelAdminLogEventActionChangeUsername::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeUsername>(action_ptr);
      return td_api::make_object<td_api::chatEventUsernameChanged>(std::move(action->prev_value_),
                                                                   std::move(action->new_value_));
    }
    case telegram_api::channelAdminLogEventActionChangePhoto::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangePhoto>(action_ptr);
      auto file_manager = td->file_manager_.get();
      auto old_photo = get_photo(file_manager, std::move(action->prev_photo_), DialogId(channel_id));
      auto new_photo = get_photo(file_manager, std::move(action->new_photo_), DialogId(channel_id));
      return td_api::make_object<td_api::chatEventPhotoChanged>(get_chat_photo_object(file_manager, old_photo),
                                                                get_chat_photo_object(file_manager, new_photo));
    }
    case telegram_api::channelAdminLogEventActionDefaultBannedRights::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionDefaultBannedRights>(action_ptr);
      auto old_permissions = RestrictedRights(action->prev_banned_rights_);
      auto new_permissions = RestrictedRights(action->new_banned_rights_);
      return td_api::make_object<td_api::chatEventPermissionsChanged>(old_permissions.get_chat_permissions_object(),
                                                                      new_permissions.get_chat_permissions_object());
    }
    case telegram_api::channelAdminLogEventActionToggleInvites::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionToggleInvites>(action_ptr);
      return td_api::make_object<td_api::chatEventInvitesToggled>(action->new_value_);
    }
    case telegram_api::channelAdminLogEventActionToggleSignatures::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionToggleSignatures>(action_ptr);
      return td_api::make_object<td_api::chatEventSignMessagesToggled>(action->new_value_);
    }
    case telegram_api::channelAdminLogEventActionUpdatePinned::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionUpdatePinned>(action_ptr);
      DialogId sender_dialog_id;
      auto message = td->messages_manager_->get_dialog_event_log_message_object(
          DialogId(channel_id), std::move(action->message_), sender_dialog_id);
      if (message == nullptr) {
        return nullptr;
      }
      if (message->is_pinned_) {
        return td_api::make_object<td_api::chatEventMessagePinned>(std::move(message));
      } else {
        return td_api::make_object<td_api::chatEventMessageUnpinned>(std::move(message));
      }
    }
    case telegram_api::channelAdminLogEventActionSendMessage::ID: {
      return nullptr;
    }
    case telegram_api::channelAdminLogEventActionEditMessage::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionEditMessage>(action_ptr);
      DialogId old_sender_dialog_id;
      auto old_message = td->messages_manager_->get_dialog_event_log_message_object(
          DialogId(channel_id), std::move(action->prev_message_), old_sender_dialog_id);
      DialogId new_sender_dialog_id;
      auto new_message = td->messages_manager_->get_dialog_event_log_message_object(
          DialogId(channel_id), std::move(action->new_message_), new_sender_dialog_id);
      if (old_message == nullptr || new_message == nullptr) {
        return nullptr;
      }
      if (old_sender_dialog_id == new_sender_dialog_id) {
        actor_dialog_id = old_sender_dialog_id;
      }
      return td_api::make_object<td_api::chatEventMessageEdited>(std::move(old_message), std::move(new_message));
    }
    case telegram_api::channelAdminLogEventActionStopPoll::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionStopPoll>(action_ptr);
      auto message = td->messages_manager_->get_dialog_event_log_message_object(
          DialogId(channel_id), std::move(action->message_), actor_dialog_id);
      if (message == nullptr) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventPollStopped>(std::move(message));
    }
    case telegram_api::channelAdminLogEventActionDeleteMessage::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionDeleteMessage>(action_ptr);
      auto message = td->messages_manager_->get_dialog_event_log_message_object(
          DialogId(channel_id), std::move(action->message_), actor_dialog_id);
      if (message == nullptr) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventMessageDeleted>(std::move(message));
    }
    case telegram_api::channelAdminLogEventActionChangeStickerSet::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeStickerSet>(action_ptr);
      auto old_sticker_set_id = td->stickers_manager_->add_sticker_set(std::move(action->prev_stickerset_));
      auto new_sticker_set_id = td->stickers_manager_->add_sticker_set(std::move(action->new_stickerset_));
      if (!old_sticker_set_id.is_valid() || !new_sticker_set_id.is_valid()) {
        LOG(ERROR) << "Skip " << to_string(action);
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventStickerSetChanged>(old_sticker_set_id.get(),
                                                                     new_sticker_set_id.get());
    }
    case telegram_api::channelAdminLogEventActionTogglePreHistoryHidden::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionTogglePreHistoryHidden>(action_ptr);
      return td_api::make_object<td_api::chatEventIsAllHistoryAvailableToggled>(!action->new_value_);
    }
    case telegram_api::channelAdminLogEventActionChangeLinkedChat::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeLinkedChat>(action_ptr);

      auto get_dialog_from_channel_id = [messages_manager = td->messages_manager_.get()](int64 channel_id_int) {
        ChannelId channel_id(channel_id_int);
        if (!channel_id.is_valid()) {
          return DialogId();
        }

        DialogId dialog_id(channel_id);
        messages_manager->force_create_dialog(dialog_id, "get_dialog_from_channel_id");
        return dialog_id;
      };

      auto old_linked_dialog_id = get_dialog_from_channel_id(action->prev_value_);
      auto new_linked_dialog_id = get_dialog_from_channel_id(action->new_value_);
      if (old_linked_dialog_id == new_linked_dialog_id) {
        LOG(ERROR) << "Receive the same linked " << new_linked_dialog_id;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventLinkedChatChanged>(old_linked_dialog_id.get(),
                                                                     new_linked_dialog_id.get());
    }
    case telegram_api::channelAdminLogEventActionChangeLocation::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeLocation>(action_ptr);
      auto old_location = DialogLocation(std::move(action->prev_value_));
      auto new_location = DialogLocation(std::move(action->new_value_));
      return td_api::make_object<td_api::chatEventLocationChanged>(old_location.get_chat_location_object(),
                                                                   new_location.get_chat_location_object());
    }
    case telegram_api::channelAdminLogEventActionToggleSlowMode::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionToggleSlowMode>(action_ptr);
      auto old_slow_mode_delay = clamp(action->prev_value_, 0, 86400 * 366);
      auto new_slow_mode_delay = clamp(action->new_value_, 0, 86400 * 366);
      return td_api::make_object<td_api::chatEventSlowModeDelayChanged>(old_slow_mode_delay, new_slow_mode_delay);
    }
    case telegram_api::channelAdminLogEventActionExportedInviteEdit::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionExportedInviteEdit>(action_ptr);
      DialogInviteLink old_invite_link(std::move(action->prev_invite_), "channelAdminLogEventActionExportedInviteEdit");
      DialogInviteLink new_invite_link(std::move(action->new_invite_), "channelAdminLogEventActionExportedInviteEdit");
      if (!old_invite_link.is_valid() || !new_invite_link.is_valid()) {
        LOG(ERROR) << "Wrong edited invite link: " << old_invite_link << " -> " << new_invite_link;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventInviteLinkEdited>(
          old_invite_link.get_chat_invite_link_object(td->contacts_manager_.get()),
          new_invite_link.get_chat_invite_link_object(td->contacts_manager_.get()));
    }
    case telegram_api::channelAdminLogEventActionExportedInviteRevoke::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionExportedInviteRevoke>(action_ptr);
      DialogInviteLink invite_link(std::move(action->invite_), "channelAdminLogEventActionExportedInviteRevoke");
      if (!invite_link.is_valid()) {
        LOG(ERROR) << "Wrong revoked invite link: " << invite_link;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventInviteLinkRevoked>(
          invite_link.get_chat_invite_link_object(td->contacts_manager_.get()));
    }
    case telegram_api::channelAdminLogEventActionExportedInviteDelete::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionExportedInviteDelete>(action_ptr);
      DialogInviteLink invite_link(std::move(action->invite_), "channelAdminLogEventActionExportedInviteDelete");
      if (!invite_link.is_valid()) {
        LOG(ERROR) << "Wrong deleted invite link: " << invite_link;
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventInviteLinkDeleted>(
          invite_link.get_chat_invite_link_object(td->contacts_manager_.get()));
    }
    case telegram_api::channelAdminLogEventActionStartGroupCall::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionStartGroupCall>(action_ptr);
      auto input_group_call_id = InputGroupCallId(action->call_);
      if (!input_group_call_id.is_valid()) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventVideoChatCreated>(
          td->group_call_manager_->get_group_call_id(input_group_call_id, DialogId(channel_id)).get());
    }
    case telegram_api::channelAdminLogEventActionDiscardGroupCall::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionDiscardGroupCall>(action_ptr);
      auto input_group_call_id = InputGroupCallId(action->call_);
      if (!input_group_call_id.is_valid()) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventVideoChatEnded>(
          td->group_call_manager_->get_group_call_id(input_group_call_id, DialogId(channel_id)).get());
    }
    case telegram_api::channelAdminLogEventActionParticipantMute::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantMute>(action_ptr);
      GroupCallParticipant participant(action->participant_, 0);
      if (!participant.is_valid()) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventVideoChatParticipantIsMutedToggled>(
          get_message_sender_object(td, participant.dialog_id, "chatEventVideoChatParticipantIsMutedToggled"), true);
    }
    case telegram_api::channelAdminLogEventActionParticipantUnmute::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantUnmute>(action_ptr);
      GroupCallParticipant participant(action->participant_, 0);
      if (!participant.is_valid()) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventVideoChatParticipantIsMutedToggled>(
          get_message_sender_object(td, participant.dialog_id, "chatEventVideoChatParticipantIsMutedToggled"), false);
    }
    case telegram_api::channelAdminLogEventActionParticipantVolume::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionParticipantVolume>(action_ptr);
      GroupCallParticipant participant(action->participant_, 0);
      if (!participant.is_valid()) {
        return nullptr;
      }
      return td_api::make_object<td_api::chatEventVideoChatParticipantVolumeLevelChanged>(
          get_message_sender_object(td, participant.dialog_id, "chatEventVideoChatParticipantVolumeLevelChanged"),
          participant.volume_level);
    }
    case telegram_api::channelAdminLogEventActionToggleGroupCallSetting::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionToggleGroupCallSetting>(action_ptr);
      return td_api::make_object<td_api::chatEventVideoChatMuteNewParticipantsToggled>(action->join_muted_);
    }
    case telegram_api::channelAdminLogEventActionChangeHistoryTTL::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeHistoryTTL>(action_ptr);
      auto old_value = MessageTtl(clamp(action->prev_value_, 0, 86400 * 366));
      auto new_value = MessageTtl(clamp(action->new_value_, 0, 86400 * 366));
      return td_api::make_object<td_api::chatEventMessageTtlChanged>(old_value.get_message_ttl_object(),
                                                                     new_value.get_message_ttl_object());
    }
    case telegram_api::channelAdminLogEventActionToggleNoForwards::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionToggleNoForwards>(action_ptr);
      return td_api::make_object<td_api::chatEventHasProtectedContentToggled>(action->new_value_);
    }
    case telegram_api::channelAdminLogEventActionChangeAvailableReactions::ID: {
      auto action = move_tl_object_as<telegram_api::channelAdminLogEventActionChangeAvailableReactions>(action_ptr);
      return td_api::make_object<td_api::chatEventAvailableReactionsChanged>(std::move(action->prev_value_),
                                                                             std::move(action->new_value_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

class GetChannelAdminLogQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::chatEvents>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetChannelAdminLogQuery(Promise<td_api::object_ptr<td_api::chatEvents>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &query, int64 from_event_id, int32 limit,
            tl_object_ptr<telegram_api::channelAdminLogEventsFilter> filter,
            vector<tl_object_ptr<telegram_api::InputUser>> input_users) {
    channel_id_ = channel_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = 0;
    if (filter != nullptr) {
      flags |= telegram_api::channels_getAdminLog::EVENTS_FILTER_MASK;
    }
    if (!input_users.empty()) {
      flags |= telegram_api::channels_getAdminLog::ADMINS_MASK;
    }

    send_query(G()->net_query_creator().create(telegram_api::channels_getAdminLog(
        flags, std::move(input_channel), query, std::move(filter), std::move(input_users), from_event_id, 0, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getAdminLog>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto events = result_ptr.move_as_ok();
    LOG(INFO) << "Receive in " << channel_id_ << ' ' << to_string(events);
    td_->contacts_manager_->on_get_users(std::move(events->users_), "on_get_event_log");
    td_->contacts_manager_->on_get_chats(std::move(events->chats_), "on_get_event_log");

    auto result = td_api::make_object<td_api::chatEvents>();
    result->events_.reserve(events->events_.size());
    for (auto &event : events->events_) {
      if (event->date_ <= 0) {
        LOG(ERROR) << "Receive wrong event date = " << event->date_;
        event->date_ = 0;
      }

      UserId user_id(event->user_id_);
      if (!user_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << user_id;
        continue;
      }
      LOG_IF(ERROR, !td_->contacts_manager_->have_user(user_id)) << "Have no info about " << user_id;

      DialogId actor_dialog_id;
      auto action = get_chat_event_action_object(td_, channel_id_, std::move(event->action_), actor_dialog_id);
      if (action == nullptr) {
        continue;
      }
      if (user_id == ContactsManager::get_channel_bot_user_id() && actor_dialog_id.is_valid() &&
          actor_dialog_id.get_type() != DialogType::User) {
        user_id = UserId();
      } else {
        actor_dialog_id = DialogId();
      }
      auto actor = get_message_sender_object_const(td_, user_id, actor_dialog_id, "GetChannelAdminLogQuery");
      result->events_.push_back(
          td_api::make_object<td_api::chatEvent>(event->id_, event->date_, std::move(actor), std::move(action)));
    }

    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelAdminLogQuery");
    promise_.set_error(std::move(status));
  }
};

static telegram_api::object_ptr<telegram_api::channelAdminLogEventsFilter> get_input_channel_admin_log_events_filter(
    const td_api::object_ptr<td_api::chatEventLogFilters> &filters) {
  if (filters == nullptr) {
    return nullptr;
  }

  int32 flags = 0;
  if (filters->message_edits_) {
    flags |= telegram_api::channelAdminLogEventsFilter::EDIT_MASK;
  }
  if (filters->message_deletions_) {
    flags |= telegram_api::channelAdminLogEventsFilter::DELETE_MASK;
  }
  if (filters->message_pins_) {
    flags |= telegram_api::channelAdminLogEventsFilter::PINNED_MASK;
  }
  if (filters->member_joins_) {
    flags |= telegram_api::channelAdminLogEventsFilter::JOIN_MASK;
  }
  if (filters->member_leaves_) {
    flags |= telegram_api::channelAdminLogEventsFilter::LEAVE_MASK;
  }
  if (filters->member_invites_) {
    flags |= telegram_api::channelAdminLogEventsFilter::INVITE_MASK;
  }
  if (filters->member_promotions_) {
    flags |= telegram_api::channelAdminLogEventsFilter::PROMOTE_MASK;
    flags |= telegram_api::channelAdminLogEventsFilter::DEMOTE_MASK;
  }
  if (filters->member_restrictions_) {
    flags |= telegram_api::channelAdminLogEventsFilter::BAN_MASK;
    flags |= telegram_api::channelAdminLogEventsFilter::UNBAN_MASK;
    flags |= telegram_api::channelAdminLogEventsFilter::KICK_MASK;
    flags |= telegram_api::channelAdminLogEventsFilter::UNKICK_MASK;
  }
  if (filters->info_changes_) {
    flags |= telegram_api::channelAdminLogEventsFilter::INFO_MASK;
  }
  if (filters->setting_changes_) {
    flags |= telegram_api::channelAdminLogEventsFilter::SETTINGS_MASK;
  }
  if (filters->invite_link_changes_) {
    flags |= telegram_api::channelAdminLogEventsFilter::INVITES_MASK;
  }
  if (filters->video_chat_changes_) {
    flags |= telegram_api::channelAdminLogEventsFilter::GROUP_CALL_MASK;
  }

  return telegram_api::make_object<telegram_api::channelAdminLogEventsFilter>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/);
}

void get_dialog_event_log(Td *td, DialogId dialog_id, const string &query, int64 from_event_id, int32 limit,
                          const td_api::object_ptr<td_api::chatEventLogFilters> &filters,
                          const vector<UserId> &user_ids, Promise<td_api::object_ptr<td_api::chatEvents>> &&promise) {
  if (!td->messages_manager_->have_dialog_force(dialog_id, "get_dialog_event_log")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  if (dialog_id.get_type() != DialogType::Channel) {
    return promise.set_error(Status::Error(400, "Chat is not a supergroup chat"));
  }

  auto channel_id = dialog_id.get_channel_id();
  if (!td->contacts_manager_->have_channel(channel_id)) {
    return promise.set_error(Status::Error(400, "Chat info not found"));
  }

  if (!td->contacts_manager_->get_channel_status(channel_id).is_administrator()) {
    return promise.set_error(Status::Error(400, "Not enough rights to get event log"));
  }

  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  for (auto user_id : user_ids) {
    auto r_input_user = td->contacts_manager_->get_input_user(user_id);
    if (r_input_user.is_error()) {
      return promise.set_error(r_input_user.move_as_error());
    }
    input_users.push_back(r_input_user.move_as_ok());
  }

  td->create_handler<GetChannelAdminLogQuery>(std::move(promise))
      ->send(channel_id, query, from_event_id, limit, get_input_channel_admin_log_events_filter(filters),
             std::move(input_users));
}

}  // namespace td
