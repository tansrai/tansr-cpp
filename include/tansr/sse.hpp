#pragma once

#include "tansr/error.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tansr::sse {

inline constexpr std::size_t default_max_frame_bytes = 2U * 1024U * 1024U;

// 所有字段拥有存储；未知 event 名称保留给上层合同校验。
struct Frame {
    std::optional<std::string> event;
    // 仅本帧有效 id 字段；空字符串表示重置，缺省不继承上一帧 id。
    std::optional<std::string> id;
    std::string data;
    // 传输提示，不授权自动重放业务操作。
    std::optional<std::uint64_t> retry;
};

// 单所有者增量解析器；调用方负责线程同步和已处理水位。
class Parser {
  public:
    // 0 采用默认 2 MiB。帽包含评论/未知字段，起始 BOM 不计入；
    // CRLF 作为一个行终止符计数，与 Rust SDK 的帧帽保持一致。
    TANSR_API explicit Parser(std::size_t max_frame_bytes = 0);

    // 可在任意字节处分片；返回值不借用输入。任何解析错误永久关闭输入。
    [[nodiscard]] TANSR_API Result<std::vector<Frame>> feed(std::string_view bytes);

    // EOF 不补发半帧，更不表示服务端当前轮完成；重复关闭返回空列表。
    [[nodiscard]] TANSR_API Result<std::vector<Frame>> finish();

    // 解析位置不等于应用已经耐久处理的游标。
    [[nodiscard]] TANSR_API const std::string &last_event_id() const noexcept;

  private:
    Result<void> consume_byte(unsigned char byte, std::vector<Frame> &frames);
    Result<void> complete_line(std::vector<Frame> &frames);
    void dispatch(std::vector<Frame> &frames);

    std::size_t max_frame_bytes_;
    std::size_t frame_bytes_{0};
    std::string line_;
    std::string bom_;
    bool bom_checked_{false};
    bool skip_lf_{false};
    std::string data_;
    bool has_data_{false};
    std::optional<std::string> event_;
    std::optional<std::string> id_;
    std::optional<std::uint64_t> retry_;
    std::string last_event_id_;
    bool done_{false};
};

} // namespace tansr::sse
