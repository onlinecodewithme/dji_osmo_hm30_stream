/**
 * DJI Osmo HM30 Streamer
 *
 * Generates an H.264 Test Pattern or acts as a proxy for the stream.
 */
#include <iostream>
#include <string>
#include <csignal>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <cmath>
#include <algorithm>
#include "../common/ffmpeg_ptr.hpp"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

using namespace ffmpeg_utils;

static volatile sig_atomic_t g_running = 1;
static void signal_handler(int) { g_running = 0; }

static const uint8_t BAR_R[] = {235, 235, 16,  16,  235, 235, 16,  16};
static const uint8_t BAR_G[] = {235, 235, 235, 235, 16,  16,  16,  16};
static const uint8_t BAR_B[] = {235, 16,  235, 16,  235, 16,  235, 16};

static inline void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b, uint8_t &y, uint8_t &u, uint8_t &v) {
    int yy = ((66  * r + 129 * g +  25 * b + 128) >> 8) + 16;
    int uu = ((-38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
    int vv = ((112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
    y = static_cast<uint8_t>(std::clamp(yy, 0, 255));
    u = static_cast<uint8_t>(std::clamp(uu, 0, 255));
    v = static_cast<uint8_t>(std::clamp(vv, 0, 255));
}

static void generate_test_frame(AVFrame *frame, int width, int height, int frame_number) {
    int bar_width = width / 8;
    for (int y_pos = 0; y_pos < height; y_pos++) {
        for (int x = 0; x < width; x++) {
            int bar = std::min(x / bar_width, 7);
            uint8_t yy, uu, vv;
            rgb_to_yuv(BAR_R[bar], BAR_G[bar], BAR_B[bar], yy, uu, vv);
            frame->data[0][y_pos * frame->linesize[0] + x] = yy;
        }
    }
    for (int y_pos = 0; y_pos < height / 2; y_pos++) {
        for (int x = 0; x < width / 2; x++) {
            int bar = std::min((x * 2) / bar_width, 7);
            uint8_t yy, uu, vv;
            rgb_to_yuv(BAR_R[bar], BAR_G[bar], BAR_B[bar], yy, uu, vv);
            frame->data[1][y_pos * frame->linesize[1] + x] = uu;
            frame->data[2][y_pos * frame->linesize[2] + x] = vv;
        }
    }
    int ticker_x = (frame_number * 4) % width;
    for (int y_pos = 0; y_pos < height; y_pos++) {
        for (int dx = 0; dx < 4 && (ticker_x + dx) < width; dx++) {
            frame->data[0][y_pos * frame->linesize[0] + ticker_x + dx] = 235;
        }
    }
}

int main(int argc, char *argv[]) {
    std::string dst_ip = "192.168.144.25";
    int dst_port = 8554;
    std::string dst_path = "/stream";
    int width = 1280;
    int height = 720;
    int fps = 30;
    int bitrate = 2000000;
    int duration_sec = 0;

    static struct option long_opts[] = {
        {"ip",       required_argument, nullptr, 'i'},
        {"port",     required_argument, nullptr, 'p'},
        {"path",     required_argument, nullptr, 's'},
        {"width",    required_argument, nullptr, 'w'},
        {"height",   required_argument, nullptr, 'h'},
        {"fps",      required_argument, nullptr, 'f'},
        {"bitrate",  required_argument, nullptr, 'b'},
        {"duration", required_argument, nullptr, 'd'},
        {"udp-broadcast", no_argument,  nullptr, 'U'},
        {"help",     no_argument,       nullptr, 'H'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    bool use_udp_broadcast = false;
    while ((opt = getopt_long(argc, argv, "i:p:s:w:h:f:b:d:UH", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'i': dst_ip = optarg; break;
            case 'p': dst_port = std::stoi(optarg); break;
            case 's': dst_path = optarg; break;
            case 'w': width = std::stoi(optarg); break;
            case 'h': height = std::stoi(optarg); break;
            case 'f': fps = std::stoi(optarg); break;
            case 'b': bitrate = std::stoi(optarg); break;
            case 'd': duration_sec = std::stoi(optarg); break;
            case 'U': use_udp_broadcast = true; break;
            case 'H':
            default:
                std::cerr << "Usage: " << argv[0] << " [options]\n";
                return (opt == 'H') ? 0 : 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "=== SIYI HM30 Test-Pattern Streamer ===\n";
    if (use_udp_broadcast) {
        std::cout << "Destination : udp://255.255.255.255:" << dst_port << " (Broadcast)\n";
    } else {
        std::cout << "Destination : rtsp://" << dst_ip << ":" << dst_port << dst_path << "\n";
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "ERROR: H.264 encoder not found.\n";
        return 1;
    }

    CodecContextPtr enc_ctx(avcodec_alloc_context3(codec));
    if (!enc_ctx) return 1;

    enc_ctx->bit_rate = bitrate;
    enc_ctx->width = width;
    enc_ctx->height = height;
    enc_ctx->time_base = {1, fps};
    enc_ctx->framerate = {fps, 1};
    enc_ctx->gop_size = fps;
    enc_ctx->max_b_frames = 0;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(enc_ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(enc_ctx->priv_data, "profile", "baseline", 0);
    }
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(enc_ctx.get(), codec, nullptr) < 0) return 1;

    char rtsp_url[512];
    AVFormatContext* raw_fmt_ctx = nullptr;
    int ret = 0;
    
    if (use_udp_broadcast) {
        snprintf(rtsp_url, sizeof(rtsp_url), "udp://255.255.255.255:%d?broadcast=1", dst_port);
        ret = avformat_alloc_output_context2(&raw_fmt_ctx, nullptr, "mpegts", rtsp_url);
    } else {
        snprintf(rtsp_url, sizeof(rtsp_url), "rtsp://%s:%d%s", dst_ip.c_str(), dst_port, dst_path.c_str());
        ret = avformat_alloc_output_context2(&raw_fmt_ctx, nullptr, "rtsp", rtsp_url);
        if (ret < 0 || !raw_fmt_ctx) {
            snprintf(rtsp_url, sizeof(rtsp_url), "rtp://%s:%d", dst_ip.c_str(), dst_port);
            ret = avformat_alloc_output_context2(&raw_fmt_ctx, nullptr, "rtp", rtsp_url);
        }
    }
    FormatContextPtr fmt_ctx(raw_fmt_ctx);
    if (!fmt_ctx) return 1;

    AVStream* stream = avformat_new_stream(fmt_ctx.get(), codec);
    if (!stream) return 1;

    avcodec_parameters_from_context(stream->codecpar, enc_ctx.get());
    stream->time_base = enc_ctx->time_base;

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, rtsp_url, AVIO_FLAG_WRITE) < 0) return 1;
    }

    if (avformat_write_header(fmt_ctx.get(), nullptr) < 0) return 1;

    FramePtr frame(av_frame_alloc());
    frame->format = enc_ctx->pix_fmt;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame.get(), 0);

    PacketPtr pkt(av_packet_alloc());

    int64_t frame_count = 0;
    int64_t total_frames = (duration_sec > 0) ? (int64_t)duration_sec * fps : 0;
    int64_t frame_dur_us = 1000000 / fps;

    while (g_running && (total_frames == 0 || frame_count < total_frames)) {
        int64_t frame_start = av_gettime_relative();
        av_frame_make_writable(frame.get());
        generate_test_frame(frame.get(), width, height, (int)frame_count);
        frame->pts = frame_count;

        ret = avcodec_send_frame(enc_ctx.get(), frame.get());
        while (ret >= 0) {
            ret = avcodec_receive_packet(enc_ctx.get(), pkt.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) { g_running = 0; break; }

            av_packet_rescale_ts(pkt.get(), enc_ctx->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            av_interleaved_write_frame(fmt_ctx.get(), pkt.get());
        }

        frame_count++;
        int64_t elapsed_us = av_gettime_relative() - frame_start;
        if (frame_dur_us > elapsed_us) av_usleep((unsigned)(frame_dur_us - elapsed_us));
    }

    avcodec_send_frame(enc_ctx.get(), nullptr);
    while (avcodec_receive_packet(enc_ctx.get(), pkt.get()) == 0) {
        av_packet_rescale_ts(pkt.get(), enc_ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmt_ctx.get(), pkt.get());
    }

    av_write_trailer(fmt_ctx.get());
    return 0;
}
