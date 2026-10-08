#include "tansr/sse.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using tansr::ErrorCode;
using tansr::sse::Frame;
using tansr::sse::Parser;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<Frame> feed(Parser &parser, std::string_view bytes) {
    auto result = parser.feed(bytes);
    if (!result) {
        throw std::runtime_error(result.error().message);
    }
    return std::move(result).value();
}

void append(std::vector<Frame> &destination, std::vector<Frame> source) {
    for (auto &frame : source) {
        destination.push_back(std::move(frame));
    }
}

void require_finished(Parser &parser) {
    auto result = parser.finish();
    require(result.has_value(), "finish failed for complete input");
    require(result.value().empty(), "finish fabricated a frame");
}

void every_byte_boundary() {
    const std::string input =
        u8"\ufeff: keepalive\r\nid: 7\r\nevent: session.event\r\nretry: 1200\r\n"
        u8"data: 汉😀\r\ndata: second\r\n\r\n";
    for (std::size_t split = 0; split <= input.size(); ++split) {
        Parser parser{1024};
        const std::string_view bytes{input};
        auto frames = feed(parser, bytes.substr(0, split));
        append(frames, feed(parser, bytes.substr(split)));
        require_finished(parser);
        require(frames.size() == 1, "byte split changed frame count");
        require(frames[0].event == "session.event", "event was not preserved");
        require(frames[0].id == "7", "id was not preserved");
        require(frames[0].retry == 1200, "retry was not preserved");
        require(frames[0].data == u8"汉😀\nsecond", "UTF-8 or multiline data changed");
        require(parser.last_event_id() == "7", "parsed id was not retained");
    }
    Parser parser{1024};
    std::vector<Frame> frames;
    for (const char &byte : input) {
        append(frames, feed(parser, std::string_view{&byte, 1}));
    }
    require(frames.size() == 1, "one-byte chunks changed frame count");
    require_finished(parser);
}

void cr_dispatches_without_read_ahead() {
    Parser parser{64};
    const auto first = feed(parser, "data: first\r\r");
    require(first.size() == 1 && first[0].data == "first", "CR did not dispatch immediately");
    require(feed(parser, "\n").empty(), "CRLF suffix emitted a second event");
    const auto second = feed(parser, "data: second\r\ndata: third\r\n\r");
    require(second.size() == 1 && second[0].data == "second\nthird", "CRLF multiline changed");
    require(feed(parser, "\n").empty(), "trailing LF was not absorbed");
    const auto mixed = feed(parser, "data: a\rdata: b\ndata: c\r\n\n");
    require(mixed.size() == 1 && mixed[0].data == "a\nb\nc", "mixed newlines changed");
    require_finished(parser);
}

void empty_data_and_id_only_frames() {
    Parser parser{128};
    require(feed(parser, "id: 42\nevent: unseen\nretry: 10\n\n").empty(),
            "id-only frame dispatched");
    require(parser.last_event_id() == "42", "id-only frame did not update parsed id");
    const auto frames = feed(parser, "data:\n\nid:\n\ndata: next\n\n");
    require(frames.size() == 2, "empty data was not dispatched");
    require(frames[0].data.empty(), "empty data changed");
    require(!frames[0].id && !frames[0].event && !frames[0].retry,
            "metadata leaked into next frame");
    require(frames[1].data == "next", "second data changed");
    require(parser.last_event_id().empty(), "empty id did not reset parsed id");
    require_finished(parser);
}

void fields_and_retry_rules() {
    Parser parser{1024};
    constexpr char input[] =
        "id: valid\nid: bad\0id\nretry: 0010\nretry: +1\nretry: -1\nretry: 1.0\n"
        "retry: 18446744073709551616\nretry:\nunknown: ignored\nData: wrong-case\n"
        "event: old\nevent: unfamiliar.extension\ndata:  one space\ndata\n\n";
    const auto frames = feed(parser, std::string_view{input, sizeof(input) - 1});
    require(frames.size() == 1, "field input did not produce one frame");
    require(frames[0].id == "valid", "NUL id replaced valid id");
    require(frames[0].event == "unfamiliar.extension", "unknown event name was lost");
    require(frames[0].retry == 10, "invalid retry replaced valid decimal");
    require(frames[0].data == " one space\n", "field whitespace or missing colon changed");
    const auto maximum = feed(parser, "retry: 18446744073709551615\ndata: max\n\n");
    require(maximum[0].retry == std::numeric_limits<std::uint64_t>::max(),
            "maximum retry rejected");
    const auto zero = feed(parser, "retry: 0\nevent:\ndata: zero\n\n");
    require(zero[0].retry == 0 && zero[0].event == "", "zero retry or empty event lost");
    require_finished(parser);
}

void frame_byte_caps() {
    Parser parser{8};
    require(feed(parser, "data:a\n\n").size() == 1, "exact cap rejected");
    require(feed(parser, "data:b\n\n").size() == 1, "cap did not reset per frame");
    require(feed(parser, u8"\ufeffx\n\n").empty(), "noninitial BOM became a data field");
    Parser crlf{8};
    require(feed(crlf, u8"\ufeffdata:a\r\n\r\n").size() == 1, "BOM/CRLF cap differs from contract");
    for (const auto &sample : std::vector<std::pair<std::size_t, std::string>>{
             {7, "data:a\n\n"}, {5, ":aaaaa"}, {7, "x:aaaa\n\n"}}) {
        Parser bounded{sample.first};
        const auto result = bounded.feed(sample.second);
        require(!result && result.error().code == ErrorCode::contract,
                "frame limit was not enforced");
    }
    Parser pending{8};
    require(feed(pending, "data:abc").empty(), "partial line dispatched");
    require(!pending.feed("d"), "pending line exceeded cap");
    const auto after_failure = pending.feed("\n\n");
    require(!after_failure && after_failure.error().code == ErrorCode::closed,
            "failed parser reopened");
}

void invalid_utf8_never_dispatches() {
    const std::vector<std::string> invalid{"data: \xff\n\n",
                                           ":\xff\n",
                                           "event: \xc0\xaf\n\n",
                                           "id: \xed\xa0\x80\n\n",
                                           "unknown: \xf4\x90\x80\x80\n",
                                           "data: \xe0\x80\x80\n\n",
                                           "data: \x80\n\n",
                                           "data: \xf0\x80\x80\x80\n\n",
                                           "data: \xe2(\xa1\n\n",
                                           "data: \xf5\x80\x80\x80\n\n",
                                           "\xef\xbbx\n",
                                           "data: \xe6\n\n"};
    for (const auto &input : invalid) {
        for (std::size_t split = 0; split <= input.size(); ++split) {
            Parser parser{100};
            const std::string_view bytes{input};
            const auto first = parser.feed(bytes.substr(0, split));
            if (first) {
                require(first.value().empty(), "invalid UTF-8 prefix emitted a frame");
                const auto second = parser.feed(bytes.substr(split));
                require(!second && second.error().code == ErrorCode::contract,
                        "invalid UTF-8 accepted");
            } else {
                require(first.error().code == ErrorCode::contract,
                        "unexpected parse failure category");
            }
            require(!parser.feed("data: later\n\n"), "UTF-8 failure did not close input");
        }
    }
}

void eof_and_close_rules() {
    for (const auto &input :
         std::vector<std::string>{"data: partial", "data: partial\n", "data:\n", "data: \xe6",
                                  "\xef\xbb", ": partial", "id: partial"}) {
        Parser parser{100};
        require(feed(parser, input).empty(), "partial frame was dispatched");
        const auto result = parser.finish();
        require(!result && result.error().code == ErrorCode::contract, "partial EOF accepted");
        require(!parser.feed("\n\n"), "EOF did not close input");
        require_finished(parser);
    }
    Parser empty{100};
    require_finished(empty);
    Parser completed{100};
    require(feed(completed, "data: complete\n\n").size() == 1, "complete frame lost");
    require_finished(completed);
    require_finished(completed);
    require(!completed.feed(""), "closed parser accepted empty feed");
    Parser id_only{100};
    require(feed(id_only, "id: 42\n").empty(), "id line emitted event");
    require_finished(id_only);
    require(id_only.last_event_id() == "42", "complete id line lost at EOF");
    Parser comment{100};
    require(feed(comment, ": complete\n").empty(), "comment emitted event");
    require_finished(comment);
}

void bom_only_at_start() {
    Parser parser{100};
    const auto frames = feed(parser, u8"data: \ufeffbody\n\n");
    require(frames.size() == 1 && frames[0].data == u8"\ufeffbody", "data BOM was removed");
    require(feed(parser, u8"\ufeffdata: not-a-data-field\n\n").empty(),
            "noninitial BOM was ignored");
    require_finished(parser);
    Parser bom_only{1};
    require(feed(bom_only, u8"\ufeff").empty(), "initial BOM dispatched");
    require_finished(bom_only);
    Parser near_bom{100};
    require(feed(near_bom, "\xef\xbb\xbe: ignored\n\ndata: visible\n\n")[0].data == "visible",
            "valid non-BOM prefix was mishandled");
}

void frames_own_storage() {
    Parser parser{100};
    std::string input = "id: 9\nevent: extension\ndata: retained\n\n";
    auto frames = feed(parser, input);
    input.assign(input.size(), 'x');
    require(feed(parser, "data: replacement\n\n").size() == 1, "later frame lost");
    require(frames[0].data == "retained" && frames[0].event == "extension" && frames[0].id == "9",
            "returned frame borrowed input or parser storage");
    frames[0].id = "user-owned";
    require(parser.last_event_id() == "9", "frame mutation changed parser position");
    require_finished(parser);
}

void default_limit_and_unicode_extremes() {
    const auto limit = tansr::sse::default_max_frame_bytes;
    std::string exact = "data:";
    exact.append(limit - 7, 'a');
    exact.append("\n\n");
    Parser default_parser;
    require(feed(default_parser, exact).size() == 1, "default frame cap rejected exact boundary");
    Parser zero_parser{0};
    exact.insert(exact.size() - 2, 1, 'a');
    require(!zero_parser.feed(exact), "zero did not select the default bounded frame cap");
    Parser unicode{100};
    const std::string data = "\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80"
                             "\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
    const auto frames = feed(unicode, "data:" + data + "\n\n");
    require(frames.size() == 1 && frames[0].data == data, "valid Unicode boundary changed");
}

} // namespace

int main() {
    const std::pair<const char *, void (*)()> tests[]{
        {"every_byte_boundary", every_byte_boundary},
        {"cr_dispatches_without_read_ahead", cr_dispatches_without_read_ahead},
        {"empty_data_and_id_only_frames", empty_data_and_id_only_frames},
        {"fields_and_retry_rules", fields_and_retry_rules},
        {"frame_byte_caps", frame_byte_caps},
        {"invalid_utf8_never_dispatches", invalid_utf8_never_dispatches},
        {"eof_and_close_rules", eof_and_close_rules},
        {"bom_only_at_start", bom_only_at_start},
        {"frames_own_storage", frames_own_storage},
        {"default_limit_and_unicode_extremes", default_limit_and_unicode_extremes},
    };
    std::size_t passed = 0;
    for (const auto &test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception &error) {
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << passed << "/" << std::size(tests) << " SSE tests passed\n";
    return passed == std::size(tests) ? 0 : 1;
}
