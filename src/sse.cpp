#include "tansr/sse.hpp"

#include <limits>
#include <utility>

namespace tansr::sse {
namespace {

bool valid_utf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset++]);
        if (first < 0x80U) {
            continue;
        }
        std::size_t continuation = 0;
        unsigned char second_min = 0x80U;
        unsigned char second_max = 0xbfU;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2;
            if (first == 0xe0U) {
                second_min = 0xa0U;
            } else if (first == 0xedU) {
                second_max = 0x9fU;
            }
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3;
            if (first == 0xf0U) {
                second_min = 0x90U;
            } else if (first == 0xf4U) {
                second_max = 0x8fU;
            }
        } else {
            return false;
        }
        if (value.size() - offset < continuation) {
            return false;
        }
        const auto second = static_cast<unsigned char>(value[offset]);
        if (second < second_min || second > second_max) {
            return false;
        }
        for (std::size_t index = 1; index < continuation; ++index) {
            const auto byte = static_cast<unsigned char>(value[offset + index]);
            if (byte < 0x80U || byte > 0xbfU) {
                return false;
            }
        }
        offset += continuation;
    }
    return true;
}

std::optional<std::uint64_t> parse_retry(std::string_view value) noexcept {
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        result = result * 10U + digit;
    }
    return result;
}

} // namespace

Parser::Parser(std::size_t max_frame_bytes)
    : max_frame_bytes_(max_frame_bytes == 0 ? default_max_frame_bytes : max_frame_bytes) {}

const std::string &Parser::last_event_id() const noexcept { return last_event_id_; }

Result<std::vector<Frame>> Parser::feed(std::string_view bytes) {
    if (done_) {
        return Error{ErrorCode::closed, "SSE parser is closed"};
    }
    std::vector<Frame> frames;
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        if (!bom_checked_) {
            constexpr unsigned char bom[] = {0xefU, 0xbbU, 0xbfU};
            if (byte == bom[bom_.size()]) {
                bom_.push_back(character);
                if (bom_.size() == sizeof(bom)) {
                    bom_.clear();
                    bom_checked_ = true;
                }
                continue;
            }
            bom_checked_ = true;
            for (const char prefix : bom_) {
                auto result = consume_byte(static_cast<unsigned char>(prefix), frames);
                if (!result) {
                    done_ = true;
                    return result.error();
                }
            }
            bom_.clear();
        }
        auto result = consume_byte(byte, frames);
        if (!result) {
            done_ = true;
            return result.error();
        }
    }
    return frames;
}

Result<void> Parser::consume_byte(unsigned char byte, std::vector<Frame> &frames) {
    if (skip_lf_) {
        skip_lf_ = false;
        if (byte == '\n') {
            return {};
        }
    }
    if (frame_bytes_ == max_frame_bytes_) {
        return Error{ErrorCode::contract, "SSE frame exceeds byte limit"};
    }
    ++frame_bytes_;
    if (byte == '\r' || byte == '\n') {
        skip_lf_ = byte == '\r';
        return complete_line(frames);
    }
    line_.push_back(static_cast<char>(byte));
    return {};
}

Result<void> Parser::complete_line(std::vector<Frame> &frames) {
    if (!valid_utf8(line_)) {
        return Error{ErrorCode::contract, "SSE contains invalid UTF-8"};
    }
    if (line_.empty()) {
        dispatch(frames);
        return {};
    }
    const std::string_view line{line_};
    if (line.front() != ':') {
        const auto colon = line.find(':');
        const auto field = line.substr(0, colon);
        auto value = colon == std::string_view::npos ? std::string_view{} : line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        if (field == "data") {
            if (has_data_) {
                data_.push_back('\n');
            }
            data_.append(value);
            has_data_ = true;
        } else if (field == "event") {
            event_ = std::string{value};
        } else if (field == "id" && value.find('\0') == std::string_view::npos) {
            id_ = std::string{value};
        } else if (field == "retry") {
            if (const auto retry = parse_retry(value)) {
                retry_ = retry;
            }
        }
    }
    line_.clear();
    return {};
}

void Parser::dispatch(std::vector<Frame> &frames) {
    if (id_) {
        last_event_id_ = *id_;
    }
    if (has_data_) {
        frames.push_back(Frame{std::move(event_), std::move(id_), std::move(data_), retry_});
    }
    event_.reset();
    id_.reset();
    data_.clear();
    retry_.reset();
    has_data_ = false;
    frame_bytes_ = 0;
}

Result<std::vector<Frame>> Parser::finish() {
    std::vector<Frame> frames;
    if (done_) {
        return frames;
    }
    done_ = true;
    line_.append(bom_);
    bom_.clear();
    if (!valid_utf8(line_)) {
        return Error{ErrorCode::contract, "SSE contains incomplete UTF-8"};
    }
    if (!line_.empty() || has_data_) {
        return Error{ErrorCode::contract, "SSE ended inside an incomplete frame"};
    }
    // 无 data 的完整 id 行仍更新解析位置，不伪造可交付事件。
    dispatch(frames);
    return frames;
}

} // namespace tansr::sse
