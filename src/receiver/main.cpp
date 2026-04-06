/**
 * DJI Osmo HM30 Receiver
 *
 * Receives an H.264 stream and displays in an SDL2 window.
 */
#include <iostream>
#include <string>
#include <csignal>
#include <getopt.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include "../common/ffmpeg_ptr.hpp"

extern "C" {
#include <libavutil/time.h>
}

using namespace ffmpeg_utils;

static volatile sig_atomic_t g_running = 1;
static void signal_handler(int) { g_running = 0; }

struct DisplayCtx {
    SDL_Window   *window   = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture  *texture  = nullptr;

    ~DisplayCtx() {
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }

    bool init(int w, int h) {
        window = SDL_CreateWindow("SIYI HM30 — Stream Receiver",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window) return false;

        renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) return false;

        texture = SDL_CreateTexture(renderer,
            SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, w, h);
        return texture != nullptr;
    }

    void display(AVFrame *frame) {
        SDL_UpdateYUVTexture(texture, nullptr,
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }
};

static std::string generate_sdp(int port) {
    std::string path = "/tmp/siyi_receiver.sdp";
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fprintf(f,
            "v=0\n"
            "o=- 0 0 IN IP4 0.0.0.0\n"
            "s=SIYI Stream\n"
            "c=IN IP4 0.0.0.0\n"
            "t=0 0\n"
            "m=video %d RTP/AVP 96\n"
            "a=rtpmap:96 H264/90000\n"
            "a=fmtp:96 packetization-mode=1\n", port);
        fclose(f);
    }
    return path;
}

int main(int argc, char *argv[]) {
    std::string rtsp_url;
    int udp_port = 0;

    static struct option long_opts[] = {
        {"rtsp",  required_argument, nullptr, 'r'},
        {"udp",   required_argument, nullptr, 'u'},
        {"help",  no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:u:h", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'r': rtsp_url = optarg; break;
            case 'u': udp_port = std::stoi(optarg); break;
            case 'h':
            default:
                std::cerr << "Usage: " << argv[0] << " [options]\n";
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (rtsp_url.empty() && udp_port == 0) {
        std::cerr << "ERROR: Specify --rtsp <url> or --udp <port>.\n";
        return 1;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    std::string input_url;
    std::string sdp_path;
    if (!rtsp_url.empty()) {
        input_url = rtsp_url;
    } else {
        sdp_path = generate_sdp(udp_port);
        input_url = sdp_path;
    }

    AVFormatContext *raw_fmt_ctx = nullptr;
    AVDictionary *opts = nullptr;

    if (!rtsp_url.empty()) {
        av_dict_set(&opts, "rtsp_transport", "udp", 0);
        av_dict_set(&opts, "stimeout", "5000000", 0);
        av_dict_set(&opts, "buffer_size", "512000", 0);
    } else {
        av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp", 0);
    }

    if (avformat_open_input(&raw_fmt_ctx, input_url.c_str(), nullptr, &opts) < 0) {
        av_dict_free(&opts);
        SDL_Quit();
        return 1;
    }
    av_dict_free(&opts);
    FormatContextPtr fmt_ctx(raw_fmt_ctx);

    if (avformat_find_stream_info(fmt_ctx.get(), nullptr) < 0) {
        SDL_Quit();
        return 1;
    }

    int video_idx = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = (int)i;
            break;
        }
    }

    if (video_idx < 0) {
        SDL_Quit();
        return 1;
    }

    AVCodecParameters *codecpar = fmt_ctx->streams[video_idx]->codecpar;
    const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        SDL_Quit();
        return 1;
    }

    CodecContextPtr dec_ctx(avcodec_alloc_context3(decoder));
    avcodec_parameters_to_context(dec_ctx.get(), codecpar);
    dec_ctx->flags  |= AV_CODEC_FLAG_LOW_DELAY;
    dec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(dec_ctx.get(), decoder, nullptr) < 0) {
        SDL_Quit();
        return 1;
    }

    DisplayCtx dctx;
    bool display_ready = false;
    SwsContextPtr sws_ctx;
    FramePtr rgb_frame;
    FramePtr frame(av_frame_alloc());
    PacketPtr pkt(av_packet_alloc());

    std::cout << "Receiving stream...\n";

    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || 
               (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                g_running = 0;
                break;
            }
        }
        if (!g_running) break;

        if (av_read_frame(fmt_ctx.get(), pkt.get()) < 0) {
            av_usleep(10000);
            continue;
        }

        if (pkt->stream_index != video_idx) {
            av_packet_unref(pkt.get());
            continue;
        }

        if (avcodec_send_packet(dec_ctx.get(), pkt.get()) >= 0) {
            while (avcodec_receive_frame(dec_ctx.get(), frame.get()) == 0) {
                if (!display_ready) {
                    int w = frame->width;
                    int h = frame->height;
                    if (!dctx.init(w, h)) {
                        g_running = 0;
                        break;
                    }
                    display_ready = true;

                    if (frame->format != AV_PIX_FMT_YUV420P) {
                        sws_ctx.reset(sws_getContext(w, h, (AVPixelFormat)frame->format,
                            w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));
                        rgb_frame.reset(av_frame_alloc());
                        rgb_frame->format = AV_PIX_FMT_YUV420P;
                        rgb_frame->width = w;
                        rgb_frame->height = h;
                        av_frame_get_buffer(rgb_frame.get(), 0);
                    }
                }

                if (sws_ctx) {
                    sws_scale(sws_ctx.get(), frame->data, frame->linesize, 0, frame->height,
                              rgb_frame->data, rgb_frame->linesize);
                    dctx.display(rgb_frame.get());
                } else {
                    dctx.display(frame.get());
                }
            }
        }
        av_packet_unref(pkt.get());
    }

    SDL_Quit();
    if (!sdp_path.empty()) remove(sdp_path.c_str());

    std::cout << "Done.\n";
    return 0;
}
