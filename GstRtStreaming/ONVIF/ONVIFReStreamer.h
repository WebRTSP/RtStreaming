#pragma once

#include <optional>

#include "../GstReStreamer2.h"


class ONVIFReStreamer : public GstReStreamer2
{
public:
    ONVIFReStreamer(
        const std::string& sourceUrl,
        const std::string& forceH264ProfileLevelId,
        const std::optional<std::string>& username,
        const std::optional<std::string>& password) noexcept;
    ~ONVIFReStreamer();

protected:
    bool prepare() noexcept override;
    void cleanup() noexcept override;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
