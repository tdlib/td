//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/utf8.h"

#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/unicode.h"

namespace td {

// states of the shift-based DFA, which checks UTF-8 validity
// transitions by a byte are packed into a uint64: the next state for a state S is stored in 6 bits starting from bit S
enum : uint32 {
  UTF8_REJECT = 0,  // must be zero, so that all unspecified transitions lead to it
  UTF8_ACCEPT = 6,
  UTF8_NEED_1 = 12,    // 1 more continuation byte is needed
  UTF8_NEED_2 = 18,    // 2 more continuation bytes are needed
  UTF8_NEED_3 = 24,    // 3 more continuation bytes are needed
  UTF8_AFTER_E0 = 30,  // continuation byte 0xa0-0xbf and 1 more continuation byte are needed
  UTF8_AFTER_ED = 36,  // continuation byte 0x80-0x9f and 1 more continuation byte are needed
  UTF8_AFTER_F0 = 42,  // continuation byte 0x90-0xbf and 2 more continuation bytes are needed
  UTF8_AFTER_F4 = 48   // continuation byte 0x80-0x8f and 2 more continuation bytes are needed
};

static constexpr uint64 utf8_transition(uint32 from_state, uint32 to_state) {
  return static_cast<uint64>(to_state) << from_state;
}

static constexpr uint64 get_utf8_transitions(uint32 c) {
  if (c < 0x80) {
    return utf8_transition(UTF8_ACCEPT, UTF8_ACCEPT);
  }
  if (c < 0xc0) {
    uint64 result = utf8_transition(UTF8_NEED_1, UTF8_ACCEPT) | utf8_transition(UTF8_NEED_2, UTF8_NEED_1) |
                    utf8_transition(UTF8_NEED_3, UTF8_NEED_2);
    if (c < 0xa0) {
      result |= utf8_transition(UTF8_AFTER_ED, UTF8_NEED_1);
    } else {
      result |= utf8_transition(UTF8_AFTER_E0, UTF8_NEED_1);
    }
    if (c < 0x90) {
      result |= utf8_transition(UTF8_AFTER_F4, UTF8_NEED_2);
    } else {
      result |= utf8_transition(UTF8_AFTER_F0, UTF8_NEED_2);
    }
    return result;
  }
  if (c < 0xc2) {
    return 0;  // overlong encoding
  }
  if (c < 0xe0) {
    return utf8_transition(UTF8_ACCEPT, UTF8_NEED_1);
  }
  if (c == 0xe0) {
    return utf8_transition(UTF8_ACCEPT, UTF8_AFTER_E0);  // prevent overlong encoding
  }
  if (c == 0xed) {
    return utf8_transition(UTF8_ACCEPT, UTF8_AFTER_ED);  // prevent surrogates
  }
  if (c < 0xf0) {
    return utf8_transition(UTF8_ACCEPT, UTF8_NEED_2);
  }
  if (c == 0xf0) {
    return utf8_transition(UTF8_ACCEPT, UTF8_AFTER_F0);  // prevent overlong encoding
  }
  if (c < 0xf4) {
    return utf8_transition(UTF8_ACCEPT, UTF8_NEED_3);
  }
  if (c == 0xf4) {
    return utf8_transition(UTF8_ACCEPT, UTF8_AFTER_F4);  // prevent code points after U+10FFFF
  }
  return 0;
}

struct Utf8Dfa {
  uint64 transitions[256] = {};
};

static constexpr Utf8Dfa get_utf8_dfa() {
  Utf8Dfa result;
  for (uint32 c = 0; c < 256; c++) {
    result.transitions[c] = get_utf8_transitions(c);
  }
  return result;
}

bool check_utf8(Slice str) {
  static constexpr Utf8Dfa UTF8_DFA = get_utf8_dfa();

  // the lowest 6 bits of state contain the current DFA state, other bits are ignored
  // transitions are loaded independently of the state, so each byte adds only a shift to the dependency chain
  const unsigned char *data = str.ubegin();
  const unsigned char *data_end = str.uend();
  uint64 state = UTF8_ACCEPT;
  while (data_end - data >= 4) {
    state = UTF8_DFA.transitions[data[0]] >> (state & 63);
    state = UTF8_DFA.transitions[data[1]] >> (state & 63);
    state = UTF8_DFA.transitions[data[2]] >> (state & 63);
    state = UTF8_DFA.transitions[data[3]] >> (state & 63);
    data += 4;
  }
  while (data != data_end) {
    state = UTF8_DFA.transitions[*data++] >> (state & 63);
  }
  return (state & 63) == UTF8_ACCEPT;
}

const unsigned char *next_utf8_unsafe(const unsigned char *ptr, uint32 *code) {
  uint32 a = ptr[0];
  if ((a & 0x80) == 0) {
    *code = a;
    return ptr + 1;
  } else if ((a & 0x20) == 0) {
    *code = ((a & 0x1f) << 6) | (ptr[1] & 0x3f);
    return ptr + 2;
  } else if ((a & 0x10) == 0) {
    *code = ((a & 0x0f) << 12) | ((ptr[1] & 0x3f) << 6) | (ptr[2] & 0x3f);
    return ptr + 3;
  } else if ((a & 0x08) == 0) {
    *code = ((a & 0x07) << 18) | ((ptr[1] & 0x3f) << 12) | ((ptr[2] & 0x3f) << 6) | (ptr[3] & 0x3f);
    return ptr + 4;
  }
  UNREACHABLE();
  *code = 0;
  return ptr;
}

unsigned char *append_utf8_character_unsafe(unsigned char *ptr, uint32 code) {
  if (code <= 0x7f) {
    *ptr++ = static_cast<unsigned char>(code);
  } else if (code <= 0x7ff) {
    *ptr++ = static_cast<unsigned char>(0xc0 | (code >> 6));
    *ptr++ = static_cast<unsigned char>(0x80 | (code & 0x3f));
  } else if (code <= 0xffff) {
    *ptr++ = static_cast<unsigned char>(0xe0 | (code >> 12));
    *ptr++ = static_cast<unsigned char>(0x80 | ((code >> 6) & 0x3f));
    *ptr++ = static_cast<unsigned char>(0x80 | (code & 0x3f));
  } else {
    *ptr++ = static_cast<unsigned char>(0xf0 | (code >> 18));
    *ptr++ = static_cast<unsigned char>(0x80 | ((code >> 12) & 0x3f));
    *ptr++ = static_cast<unsigned char>(0x80 | ((code >> 6) & 0x3f));
    *ptr++ = static_cast<unsigned char>(0x80 | (code & 0x3f));
  }
  return ptr;
}

string utf8_to_lower(Slice str) {
  string result;
  auto pos = str.ubegin();
  auto end = str.uend();
  while (pos != end) {
    uint32 code;
    pos = next_utf8_unsafe(pos, &code);
    append_utf8_character(result, unicode_to_lower(code));
  }
  return result;
}

vector<string> utf8_get_search_words(Slice str) {
  bool in_word = false;
  string word;
  vector<string> words;
  auto pos = str.ubegin();
  auto end = str.uend();
  while (pos != end) {
    uint32 code;
    pos = next_utf8_unsafe(pos, &code);

    code = prepare_search_character(code);
    if (code == 0) {
      continue;
    }
    if (code == ' ') {
      if (in_word) {
        words.push_back(std::move(word));
        word.clear();
        in_word = false;
      }
    } else {
      in_word = true;
      code = remove_diacritics(code);
      append_utf8_character(word, code);
    }
  }
  if (in_word) {
    words.push_back(std::move(word));
  }
  return words;
}

string utf8_prepare_search_string(Slice str) {
  return implode(utf8_get_search_words(str));
}

string utf8_encode(Slice data) {
  if (check_utf8(data)) {
    return data.str();
  }
  return PSTRING() << "url_decode(" << url_encode(data) << ')';
}

size_t utf8_utf16_length(Slice str) {
  size_t result = 0;
  for (auto c : str) {
    result += is_utf8_character_first_code_unit(c) + ((c & 0xf8) == 0xf0);
  }
  return result;
}

Slice utf8_utf16_truncate(Slice str, size_t length) {
  for (size_t i = 0; i < str.size(); i++) {
    auto c = static_cast<unsigned char>(str[i]);
    if (is_utf8_character_first_code_unit(c)) {
      if (length <= 0) {
        return str.substr(0, i);
      } else {
        length--;
        if (c >= 0xf0) {  // >= 4 bytes in symbol => surrogate pair
          length--;
        }
      }
    }
  }
  return str;
}

Slice utf8_utf16_substr(Slice str, size_t offset) {
  if (offset == 0) {
    return str;
  }
  auto offset_pos = utf8_utf16_truncate(str, offset).size();
  return str.substr(offset_pos);
}

Slice utf8_utf16_substr(Slice str, size_t offset, size_t length) {
  return utf8_utf16_truncate(utf8_utf16_substr(str, offset), length);
}

}  // namespace td
