#include <iostream>
#include <thread>
#include <chrono>
extern "C" {
#include <libavformat/avformat.h>
}

int main() {
    AVFormatContext *fmt_ctx = nullptr;
    avformat_alloc_output_context2(&fmt_ctx, nullptr, "rtsp", "rtsp://0.0.0.0:8554/stream");
    if (!fmt_ctx) return 1;
    
    AVStream *stream = avformat_new_stream(fmt_ctx, NULL);
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_H264;
    stream->codecpar->width = 1280;
    stream->codecpar->height = 720;
    
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtsp_flags", "listen", 0);
    std::cout << "Waiting for connection..." << std::endl;
    int ret = avformat_write_header(fmt_ctx, &opts);
    std::cout << "avformat_write_header ret: " << ret << std::endl;
    return 0;
}
