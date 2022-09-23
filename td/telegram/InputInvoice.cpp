//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputInvoice.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/MimeType.h"
#include "td/utils/PathView.h"

namespace td {

bool operator==(const Invoice &lhs, const Invoice &rhs) {
  return lhs.is_test == rhs.is_test && lhs.need_name == rhs.need_name &&
         lhs.need_phone_number == rhs.need_phone_number && lhs.need_email_address == rhs.need_email_address &&
         lhs.need_shipping_address == rhs.need_shipping_address &&
         lhs.send_phone_number_to_provider == rhs.send_phone_number_to_provider &&
         lhs.send_email_address_to_provider == rhs.send_email_address_to_provider &&
         lhs.is_flexible == rhs.is_flexible && lhs.currency == rhs.currency && lhs.price_parts == rhs.price_parts &&
         lhs.max_tip_amount == rhs.max_tip_amount && lhs.suggested_tip_amounts == rhs.suggested_tip_amounts &&
         lhs.recurring_payment_terms_of_service_url == rhs.recurring_payment_terms_of_service_url;
}

bool operator!=(const Invoice &lhs, const Invoice &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Invoice &invoice) {
  return string_builder << "[" << (invoice.is_flexible ? "Flexible" : "") << (invoice.is_test ? "Test" : "")
                        << "Invoice" << (invoice.need_name ? ", needs name" : "")
                        << (invoice.need_phone_number ? ", needs phone number" : "")
                        << (invoice.need_email_address ? ", needs email address" : "")
                        << (invoice.need_shipping_address ? ", needs shipping address" : "")
                        << (invoice.send_phone_number_to_provider ? ", sends phone number to provider" : "")
                        << (invoice.send_email_address_to_provider ? ", sends email address to provider" : "")
                        << (invoice.recurring_payment_terms_of_service_url.empty()
                                ? string()
                                : ", recurring payments terms of service at " +
                                      invoice.recurring_payment_terms_of_service_url)
                        << " in " << invoice.currency << " with price parts " << format::as_array(invoice.price_parts)
                        << " and suggested tip amounts " << invoice.suggested_tip_amounts << " up to "
                        << invoice.max_tip_amount << "]";
}

bool operator==(const InputInvoice &lhs, const InputInvoice &rhs) {
  return lhs.title == rhs.title && lhs.description == rhs.description && lhs.photo == rhs.photo &&
         lhs.start_parameter == rhs.start_parameter && lhs.invoice == rhs.invoice &&
         lhs.total_amount == rhs.total_amount && lhs.receipt_message_id == rhs.receipt_message_id &&
         lhs.payload == rhs.payload && lhs.provider_token == rhs.provider_token &&
         lhs.provider_data == rhs.provider_data && lhs.extended_media == rhs.extended_media;
}

bool operator!=(const InputInvoice &lhs, const InputInvoice &rhs) {
  return !(lhs == rhs);
}

InputInvoice get_input_invoice(tl_object_ptr<telegram_api::messageMediaInvoice> &&message_invoice, Td *td,
                               DialogId owner_dialog_id, FormattedText &&message) {
  InputInvoice result;
  result.title = std::move(message_invoice->title_);
  result.description = std::move(message_invoice->description_);
  result.photo = get_web_document_photo(td->file_manager_.get(), std::move(message_invoice->photo_), owner_dialog_id);
  result.start_parameter = std::move(message_invoice->start_param_);
  result.invoice.currency = std::move(message_invoice->currency_);
  result.invoice.is_test = message_invoice->test_;
  result.invoice.need_shipping_address = message_invoice->shipping_address_requested_;
  // result.payload = string();
  // result.provider_token = string();
  // result.provider_data = string();
  result.extended_media =
      MessageExtendedMedia(td, std::move(message_invoice->extended_media_), std::move(message), owner_dialog_id);
  if (message_invoice->total_amount_ <= 0 || !check_currency_amount(message_invoice->total_amount_)) {
    LOG(ERROR) << "Receive invalid total amount " << message_invoice->total_amount_;
    message_invoice->total_amount_ = 0;
  }
  result.total_amount = message_invoice->total_amount_;
  if ((message_invoice->flags_ & telegram_api::messageMediaInvoice::RECEIPT_MSG_ID_MASK) != 0) {
    result.receipt_message_id = MessageId(ServerMessageId(message_invoice->receipt_msg_id_));
    if (!result.receipt_message_id.is_valid()) {
      LOG(ERROR) << "Receive as receipt message " << result.receipt_message_id << " in " << owner_dialog_id;
      result.receipt_message_id = MessageId();
    }
  }
  return result;
}

InputInvoice get_input_invoice(tl_object_ptr<telegram_api::botInlineMessageMediaInvoice> &&message_invoice, Td *td,
                               DialogId owner_dialog_id) {
  InputInvoice result;
  result.title = std::move(message_invoice->title_);
  result.description = std::move(message_invoice->description_);
  result.photo = get_web_document_photo(td->file_manager_.get(), std::move(message_invoice->photo_), owner_dialog_id);
  // result.start_parameter = string();
  result.invoice.currency = std::move(message_invoice->currency_);
  result.invoice.is_test = message_invoice->test_;
  result.invoice.need_shipping_address = message_invoice->shipping_address_requested_;
  // result.payload = string();
  // result.provider_token = string();
  // result.provider_data = string();
  // result.extended_media = MessageExtendedMedia();
  if (message_invoice->total_amount_ <= 0 || !check_currency_amount(message_invoice->total_amount_)) {
    LOG(ERROR) << "Receive invalid total amount " << message_invoice->total_amount_;
    message_invoice->total_amount_ = 0;
  }
  result.total_amount = message_invoice->total_amount_;
  // result.receipt_message_id = MessageId();
  return result;
}

Result<InputInvoice> process_input_message_invoice(
    td_api::object_ptr<td_api::InputMessageContent> &&input_message_content, Td *td) {
  CHECK(input_message_content != nullptr);
  CHECK(input_message_content->get_id() == td_api::inputMessageInvoice::ID);
  auto input_invoice = move_tl_object_as<td_api::inputMessageInvoice>(input_message_content);
  if (input_invoice->invoice_ == nullptr) {
    return Status::Error(400, "Invoice must be non-empty");
  }

  if (!clean_input_string(input_invoice->title_)) {
    return Status::Error(400, "Invoice title must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->description_)) {
    return Status::Error(400, "Invoice description must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->photo_url_)) {
    return Status::Error(400, "Invoice photo URL must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->start_parameter_)) {
    return Status::Error(400, "Invoice bot start parameter must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->provider_token_)) {
    return Status::Error(400, "Invoice provider token must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->provider_data_)) {
    return Status::Error(400, "Invoice provider data must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->invoice_->currency_)) {
    return Status::Error(400, "Invoice currency must be encoded in UTF-8");
  }

  InputInvoice result;
  result.title = std::move(input_invoice->title_);
  result.description = std::move(input_invoice->description_);

  auto r_http_url = parse_url(input_invoice->photo_url_);
  if (r_http_url.is_error()) {
    if (!input_invoice->photo_url_.empty()) {
      LOG(INFO) << "Can't register url " << input_invoice->photo_url_;
    }
  } else {
    auto url = r_http_url.ok().get_url();
    auto r_invoice_file_id = td->file_manager_->from_persistent_id(url, FileType::Temp);
    if (r_invoice_file_id.is_error()) {
      LOG(INFO) << "Can't register url " << url;
    } else {
      auto invoice_file_id = r_invoice_file_id.move_as_ok();

      PhotoSize s;
      s.type = 'n';
      s.dimensions = get_dimensions(input_invoice->photo_width_, input_invoice->photo_height_, nullptr);
      s.size = input_invoice->photo_size_;  // TODO use invoice_file_id size
      s.file_id = invoice_file_id;

      result.photo.id = 0;
      result.photo.photos.push_back(s);
    }
  }
  result.start_parameter = std::move(input_invoice->start_parameter_);

  result.invoice.currency = std::move(input_invoice->invoice_->currency_);
  result.invoice.price_parts.reserve(input_invoice->invoice_->price_parts_.size());
  int64 total_amount = 0;
  for (auto &price : input_invoice->invoice_->price_parts_) {
    if (!clean_input_string(price->label_)) {
      return Status::Error(400, "Invoice price label must be encoded in UTF-8");
    }
    if (!check_currency_amount(price->amount_)) {
      return Status::Error(400, "Too big amount of the currency specified");
    }
    result.invoice.price_parts.emplace_back(std::move(price->label_), price->amount_);
    total_amount += price->amount_;
  }
  if (total_amount <= 0) {
    return Status::Error(400, "Total price must be positive");
  }
  if (!check_currency_amount(total_amount)) {
    return Status::Error(400, "Total price is too big");
  }
  result.total_amount = total_amount;

  if (input_invoice->invoice_->max_tip_amount_ < 0 ||
      !check_currency_amount(input_invoice->invoice_->max_tip_amount_)) {
    return Status::Error(400, "Invalid max_tip_amount of the currency specified");
  }
  for (auto tip_amount : input_invoice->invoice_->suggested_tip_amounts_) {
    if (tip_amount <= 0) {
      return Status::Error(400, "Suggested tip amount must be positive");
    }
    if (tip_amount > input_invoice->invoice_->max_tip_amount_) {
      return Status::Error(400, "Suggested tip amount can't be bigger than max_tip_amount");
    }
  }
  if (input_invoice->invoice_->suggested_tip_amounts_.size() > 4) {
    return Status::Error(400, "There can be at most 4 suggested tip amounts");
  }

  result.invoice.max_tip_amount = input_invoice->invoice_->max_tip_amount_;
  result.invoice.suggested_tip_amounts = std::move(input_invoice->invoice_->suggested_tip_amounts_);
  result.invoice.recurring_payment_terms_of_service_url =
      std::move(input_invoice->invoice_->recurring_payment_terms_of_service_url_);
  result.invoice.is_test = input_invoice->invoice_->is_test_;
  result.invoice.need_name = input_invoice->invoice_->need_name_;
  result.invoice.need_phone_number = input_invoice->invoice_->need_phone_number_;
  result.invoice.need_email_address = input_invoice->invoice_->need_email_address_;
  result.invoice.need_shipping_address = input_invoice->invoice_->need_shipping_address_;
  result.invoice.send_phone_number_to_provider = input_invoice->invoice_->send_phone_number_to_provider_;
  result.invoice.send_email_address_to_provider = input_invoice->invoice_->send_email_address_to_provider_;
  result.invoice.is_flexible = input_invoice->invoice_->is_flexible_;
  if (result.invoice.send_phone_number_to_provider) {
    result.invoice.need_phone_number = true;
  }
  if (result.invoice.send_email_address_to_provider) {
    result.invoice.need_email_address = true;
  }
  if (result.invoice.is_flexible) {
    result.invoice.need_shipping_address = true;
  }

  result.payload = std::move(input_invoice->payload_);
  result.provider_token = std::move(input_invoice->provider_token_);
  result.provider_data = std::move(input_invoice->provider_data_);

  // TRY_RESULT(extended_media, MessageExtendedMedia::get_message_extended_media(td, std::move(input_invoice->extended_media_)));
  // result.extended_media = std::move(extended_media);

  return result;
}

tl_object_ptr<td_api::messageInvoice> get_message_invoice_object(const InputInvoice &input_invoice, Td *td,
                                                                 bool skip_bot_commands, int32 max_media_timestamp) {
  return make_tl_object<td_api::messageInvoice>(
      input_invoice.title, get_product_description_object(input_invoice.description),
      get_photo_object(td->file_manager_.get(), input_invoice.photo), input_invoice.invoice.currency,
      input_invoice.total_amount, input_invoice.start_parameter, input_invoice.invoice.is_test,
      input_invoice.invoice.need_shipping_address, input_invoice.receipt_message_id.get(),
      input_invoice.extended_media.get_message_extended_media_object(td, skip_bot_commands, max_media_timestamp));
}

static tl_object_ptr<telegram_api::invoice> get_input_invoice(const Invoice &invoice) {
  int32 flags = 0;
  if (invoice.is_test) {
    flags |= telegram_api::invoice::TEST_MASK;
  }
  if (invoice.need_name) {
    flags |= telegram_api::invoice::NAME_REQUESTED_MASK;
  }
  if (invoice.need_phone_number) {
    flags |= telegram_api::invoice::PHONE_REQUESTED_MASK;
  }
  if (invoice.need_email_address) {
    flags |= telegram_api::invoice::EMAIL_REQUESTED_MASK;
  }
  if (invoice.need_shipping_address) {
    flags |= telegram_api::invoice::SHIPPING_ADDRESS_REQUESTED_MASK;
  }
  if (invoice.send_phone_number_to_provider) {
    flags |= telegram_api::invoice::PHONE_TO_PROVIDER_MASK;
  }
  if (invoice.send_email_address_to_provider) {
    flags |= telegram_api::invoice::EMAIL_TO_PROVIDER_MASK;
  }
  if (invoice.is_flexible) {
    flags |= telegram_api::invoice::FLEXIBLE_MASK;
  }
  if (invoice.max_tip_amount != 0) {
    flags |= telegram_api::invoice::MAX_TIP_AMOUNT_MASK;
  }
  if (!invoice.recurring_payment_terms_of_service_url.empty()) {
    flags |= telegram_api::invoice::RECURRING_TERMS_URL_MASK;
  }

  auto prices = transform(invoice.price_parts, [](const LabeledPricePart &price) {
    return telegram_api::make_object<telegram_api::labeledPrice>(price.label, price.amount);
  });
  return make_tl_object<telegram_api::invoice>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, invoice.currency, std::move(prices),
      invoice.max_tip_amount, vector<int64>(invoice.suggested_tip_amounts),
      invoice.recurring_payment_terms_of_service_url);
}

static tl_object_ptr<telegram_api::inputWebDocument> get_input_web_document(const FileManager *file_manager,
                                                                            const Photo &photo) {
  if (photo.is_empty()) {
    return nullptr;
  }

  CHECK(photo.photos.size() == 1);
  const PhotoSize &size = photo.photos[0];
  CHECK(size.file_id.is_valid());

  vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
  if (size.dimensions.width != 0 && size.dimensions.height != 0) {
    attributes.push_back(
        make_tl_object<telegram_api::documentAttributeImageSize>(size.dimensions.width, size.dimensions.height));
  }

  auto file_view = file_manager->get_file_view(size.file_id);
  CHECK(file_view.has_url());

  auto file_name = get_url_file_name(file_view.url());
  return make_tl_object<telegram_api::inputWebDocument>(
      file_view.url(), size.size, MimeType::from_extension(PathView(file_name).extension(), "image/jpeg"),
      std::move(attributes));
}

tl_object_ptr<telegram_api::inputMediaInvoice> get_input_media_invoice(const InputInvoice &input_invoice, Td *td) {
  int32 flags = 0;
  if (!input_invoice.start_parameter.empty()) {
    flags |= telegram_api::inputMediaInvoice::START_PARAM_MASK;
  }
  auto input_web_document = get_input_web_document(td->file_manager_.get(), input_invoice.photo);
  if (input_web_document != nullptr) {
    flags |= telegram_api::inputMediaInvoice::PHOTO_MASK;
  }

  return make_tl_object<telegram_api::inputMediaInvoice>(
      flags, input_invoice.title, input_invoice.description, std::move(input_web_document),
      get_input_invoice(input_invoice.invoice), BufferSlice(input_invoice.payload), input_invoice.provider_token,
      telegram_api::make_object<telegram_api::dataJSON>(
          input_invoice.provider_data.empty() ? "null" : input_invoice.provider_data),
      input_invoice.start_parameter, nullptr);
}

tl_object_ptr<telegram_api::inputBotInlineMessageMediaInvoice> get_input_bot_inline_message_media_invoice(
    const InputInvoice &input_invoice, tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup, Td *td) {
  int32 flags = 0;
  if (reply_markup != nullptr) {
    flags |= telegram_api::inputBotInlineMessageMediaInvoice::REPLY_MARKUP_MASK;
  }
  auto input_web_document = get_input_web_document(td->file_manager_.get(), input_invoice.photo);
  if (input_web_document != nullptr) {
    flags |= telegram_api::inputBotInlineMessageMediaInvoice::PHOTO_MASK;
  }
  return make_tl_object<telegram_api::inputBotInlineMessageMediaInvoice>(
      flags, input_invoice.title, input_invoice.description, std::move(input_web_document),
      get_input_invoice(input_invoice.invoice), BufferSlice(input_invoice.payload), input_invoice.provider_token,
      telegram_api::make_object<telegram_api::dataJSON>(
          input_invoice.provider_data.empty() ? "null" : input_invoice.provider_data),
      std::move(reply_markup));
}

vector<FileId> get_input_invoice_file_ids(const Td *td, const InputInvoice &input_invoice) {
  auto file_ids = photo_get_file_ids(input_invoice.photo);
  input_invoice.extended_media.append_file_ids(td, file_ids);
  return file_ids;
}

void input_invoice_delete_thumbnail(Td *td, InputInvoice &input_invoice) {
  input_invoice.extended_media.delete_thumbnail(td);
}

bool has_input_invoice_media_timestamp(const InputInvoice &input_invoice) {
  return input_invoice.extended_media.has_media_timestamp();
}

const FormattedText *get_input_invoice_caption(const InputInvoice &input_invoice) {
  return input_invoice.extended_media.get_caption();
}

int32 get_input_invoice_duration(const Td *td, const InputInvoice &input_invoice) {
  return input_invoice.extended_media.get_duration(td);
}

FileId get_input_invoice_upload_file_id(const InputInvoice &input_invoice) {
  return input_invoice.extended_media.get_upload_file_id();
}

FileId get_input_invoice_any_file_id(const InputInvoice &input_invoice) {
  return input_invoice.extended_media.get_any_file_id();
}

FileId get_input_invoice_thumbnail_file_id(const Td *td, const InputInvoice &input_invoice) {
  return input_invoice.extended_media.get_thumbnail_file_id(td);
}

bool update_input_invoice_extended_media(InputInvoice &input_invoice,
                                         telegram_api::object_ptr<telegram_api::MessageExtendedMedia> extended_media,
                                         DialogId owner_dialog_id, Td *td) {
  return input_invoice.extended_media.update_to(td, std::move(extended_media), owner_dialog_id);
}

tl_object_ptr<td_api::formattedText> get_product_description_object(const string &description) {
  FormattedText result;
  result.text = description;
  result.entities = find_entities(result.text, true, true);
  return get_formatted_text_object(result, true, 0);
}

}  // namespace td
