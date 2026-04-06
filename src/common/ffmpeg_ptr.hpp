#pragma once

#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg_utils {

// Custom deleters for FFmpeg types
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            if (ctx->iformat) {
                avformat_close_input(&ctx);
            } else {
                if (!(ctx->oformat->flags & AVFMT_NOFILE) && ctx->pb) {
                    avio_closep(&ctx->pb);
                }
                avformat_free_context(ctx);
            }
        }
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) av_packet_free(&pkt);
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) av_frame_free(&frame);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) sws_freeContext(ctx);
    }
};

// Smart pointer type aliases
using FormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using CodecContextPtr  = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using PacketPtr        = std::unique_ptr<AVPacket, AVPacketDeleter>;
using FramePtr         = std::unique_ptr<AVFrame, AVFrameDeleter>;
using SwsContextPtr    = std::unique_ptr<SwsContext, SwsContextDeleter>;

} // namespace ffmpeg_utils
