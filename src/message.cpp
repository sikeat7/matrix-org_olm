/* Copyright 2015 OpenMarket Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "axolotl/message.hh"

namespace {

template<typename T>
std::size_t varint_length(
    T value
) {
    std::size_t result = 1;
    while (value > 128U) {
        ++result;
        value >>= 7;
    }
    return result;
}


template<typename T>
std::uint8_t * varint_encode(
    std::uint8_t * output,
    T value
) {
    while (value > 128U) {
        *(output++) = (0x7F & value) | 0x80;
    }
    (*output++) = value;
    return output;
}


template<typename T>
T varint_decode(
    std::uint8_t const * varint_start,
    std::uint8_t const * varint_end
) {
    T value = 0;
    do {
        value <<= 7;
        value |= 0x7F & *(--varint_end);
    } while (varint_end != varint_start);
    return value;
}


std::uint8_t const * varint_skip(
    std::uint8_t const * input,
    std::uint8_t const * input_end
) {
    while (input != input_end) {
        std::uint8_t tmp = *(input++);
        if ((tmp & 0x80) == 0) {
            return input;
        }
    }
    return input;
}


std::size_t varstring_length(
    std::size_t string_length
) {
    return varint_length(string_length) + string_length;
}

static std::size_t const VERSION_LENGTH = 1;
static std::uint8_t const RATCHET_KEY_TAG = 012;
static std::uint8_t const COUNTER_TAG = 020;
static std::uint8_t const CIPHERTEXT_TAG = 042;

} // namespace


std::size_t axolotl::encode_message_length(
    std::uint32_t counter,
    std::size_t ratchet_key_length,
    std::size_t ciphertext_length,
    std::size_t mac_length
) {
    std::size_t length = VERSION_LENGTH;
    length += 1 + varstring_length(ratchet_key_length);
    length += 1 + varint_length(counter);
    length += 1 + varstring_length(ciphertext_length);
    length += mac_length;
    return length;
}


void axolotl::encode_message(
    axolotl::MessageWriter & writer,
    std::uint8_t version,
    std::uint32_t counter,
    std::size_t ratchet_key_length,
    std::size_t ciphertext_length,
    std::uint8_t * output
) {
    std::uint8_t * pos = output;
    *(pos++) = version;
    *(pos++) = COUNTER_TAG;
    pos = varint_encode(pos, counter);
    *(pos++) = RATCHET_KEY_TAG;
    pos = varint_encode(pos, ratchet_key_length);
    writer.ratchet_key = pos;
    pos += ratchet_key_length;
    *(pos++) = CIPHERTEXT_TAG;
    pos = varint_encode(pos, ciphertext_length);
    writer.ciphertext = pos;
    pos += ciphertext_length;
}


std::size_t axolotl::decode_message(
    axolotl::MessageReader & reader,
    std::uint8_t const * input, std::size_t input_length,
    std::size_t mac_length
) {
    std::uint8_t const * pos = input;
    std::uint8_t const * end = input + input_length - mac_length;
    std::uint8_t flags = 0;
    std::size_t result = std::size_t(-1);
    if (pos == end) return result;
    reader.version = *(pos++);
    while (pos != end) {
        uint8_t tag = *(pos);
        if (tag == COUNTER_TAG) {
            ++pos;
            std::uint8_t const * counter_start = pos;
            pos = varint_skip(pos, end);
            reader.counter = varint_decode<std::uint32_t>(counter_start, pos);
            flags |= 1;
        } else if (tag == RATCHET_KEY_TAG) {
            ++pos;
            std::uint8_t const * len_start = pos;
            pos = varint_skip(pos, end);
            std::size_t len = varint_decode<std::size_t>(len_start, pos);
            if (len > end - pos) return result;
            reader.ratchet_key_length = len;
            reader.ratchet_key = pos;
            pos += len;
            flags |= 2;
        } else if (tag == CIPHERTEXT_TAG) {
            ++pos;
            std::uint8_t const * len_start = pos;
            pos = varint_skip(pos, end);
            std::size_t len = varint_decode<std::size_t>(len_start, pos);
            if (len > end - pos) return result;
            reader.ciphertext_length = len;
            reader.ciphertext = pos;
            pos += len;
            flags |= 4;
        } else if (tag & 0x7 == 0) {
            pos = varint_skip(pos, end);
            pos = varint_skip(pos, end);
        } else if (tag & 0x7 == 2) {
            std::uint8_t const * len_start = pos;
            pos = varint_skip(pos, end);
            std::size_t len = varint_decode<std::size_t>(len_start, pos);
            if (len > end - pos) return result;
            pos += len;
        } else {
            return std::size_t(-1);
        }
    }
    if (flags == 0x7) {
        reader.input = input;
        reader.input_length = input_length;
        return std::size_t(pos - input);
    }
    return result;
}
