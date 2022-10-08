#pragma once

#include "GstStreamingSource.h"


class GstPipelineStreamer2 : public GstStreamingSource
{
public:
    GstPipelineStreamer2(const std::string& sourcePipelineDesc);

protected:
    bool prepare() noexcept override;

private:
    const std::string _sourcePipelineDesc;
};
