#include <iostream>
extern "C" {
#include <libavformat/avformat.h>
}

int main() {
    AVFormatContext *fmt_ctx = nullptr;
    int ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, "mpegts", "udp://192.168.144.255:5600?broadcast=1");
    if (ret < 0 || !fmt_ctx) return 1;
    AVDictionary *opts = nullptr;
    // We would need to set up streams and then avio_open
    ret = avio_open2(&fmt_ctx->pb, "udp://192.168.144.255:5600?broadcast=1", AVIO_FLAG_WRITE, nullptr, &opts);
    std::cout << "avio_open2 ret: " << ret << std::endl;
    return 0;
}
