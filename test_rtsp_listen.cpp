#include <iostream>
extern "C" {
#include <libavformat/avformat.h>
}

int main() {
    AVFormatContext *fmt_ctx = nullptr;
    avformat_alloc_output_context2(&fmt_ctx, nullptr, "rtsp", "rtsp://0.0.0.0:8554/stream");
    if (!fmt_ctx) return 1;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtsp_flags", "listen", 0);
    int ret = avformat_write_header(fmt_ctx, &opts);
    std::cout << "avformat_write_header ret: " << ret << std::endl;
    return 0;
}
