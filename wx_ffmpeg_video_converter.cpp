// wx_ffmpeg_video_converter.cpp
// Video converter using wxWidgets GUI and FFmpeg libraries (libavformat/libavcodec/libswscale)
// Features:
//  - Open a video file
//  - Choose an output container format (mp4, mkv, avi, mov)
//  - Either remux (stream copy) or re-encode the video to H.264 (libx264)
//  - Runs conversion in a background wxThread and updates progress/log
// Limitations:
//  - Audio streams are copied (stream copy) when re-encoding video.
//  - Minimal error handling; intended as a starting point.

#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/thread.h>
#include <wx/progdlg.h>
#include <wx/checkbox.h>
#include <atomic>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}


class ConverterThread;

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame();
    void setRunning(bool value) { m_running.store(value); }

private:
    void OnOpen(wxCommandEvent&);
    void OnStart(wxCommandEvent&);
    void OnClose(wxCloseEvent&);

    wxButton* m_openBtn;
    wxButton* m_startBtn;
    wxChoice* m_formatChoice;
    wxTextCtrl* m_inputPath;
    wxTextCtrl* m_log;
    wxGauge* m_progress;
    wxCheckBox* m_reencodeCheck;

    ConverterThread* m_thread = nullptr;
    std::atomic<bool> m_running{false};

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_Open = wxID_HIGHEST + 1,
    ID_Start
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(ID_Open, MainFrame::OnOpen)
    EVT_BUTTON(ID_Start, MainFrame::OnStart)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

class ConverterThread : public wxThread {
public:
    ConverterThread(MainFrame* handler, const std::string& in, const std::string& outFormat, bool reencode)
        : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_input(in), m_outFormat(outFormat), m_reencode(reencode) {}

protected:
    virtual ExitCode Entry() override;

private:
    MainFrame* m_handler;
    std::string m_input;
    std::string m_outFormat;
    bool m_reencode;

    void Log(const std::string& s);
    void PostProgress(int pct);
};

wxDEFINE_EVENT(wxEVT_LOG_UPDATE, wxCommandEvent);

MainFrame::MainFrame()
    : wxFrame(NULL, wxID_ANY, "wxWidgets + FFmpeg Converter", wxDefaultPosition, wxSize(760,460))
{
    av_log_set_level(AV_LOG_ERROR); // silence verbose ffmpeg logs by default
    avformat_network_init();

    wxPanel* panel = new wxPanel(this);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* fileSizer = new wxBoxSizer(wxHORIZONTAL);
    m_inputPath = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(520, -1));
    m_openBtn = new wxButton(panel, ID_Open, "Open");
    fileSizer->Add(m_inputPath, 1, wxEXPAND|wxALL, 5);
    fileSizer->Add(m_openBtn, 0, wxALL, 5);

    wxBoxSizer* optsSizer = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString formats;
    formats.Add("mp4"); formats.Add("mkv"); formats.Add("avi"); formats.Add("mov");
    m_formatChoice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, formats);
    m_formatChoice->SetSelection(0);
    m_startBtn = new wxButton(panel, ID_Start, "Start Conversion");
    m_reencodeCheck = new wxCheckBox(panel, wxID_ANY, "Re-encode video (H.264)");

    optsSizer->Add(new wxStaticText(panel, wxID_ANY, "Output format:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 6);
    optsSizer->Add(m_formatChoice, 0, wxALL, 6);
    optsSizer->Add(m_reencodeCheck, 0, wxALL|wxALIGN_CENTER_VERTICAL, 6);
    optsSizer->Add(m_startBtn, 0, wxALL|wxALIGN_CENTER_VERTICAL, 6);

    m_progress = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1,20));
    m_log = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE|wxTE_READONLY);

    topSizer->Add(fileSizer, 0, wxEXPAND);
    topSizer->Add(optsSizer, 0, wxEXPAND);
    topSizer->Add(m_progress, 0, wxEXPAND|wxALL, 5);
    topSizer->Add(m_log, 1, wxEXPAND|wxALL, 5);

    panel->SetSizer(topSizer);

    // Bind custom log event; parse simple PROGRESS: messages
    Bind(wxEVT_LOG_UPDATE, [&](wxCommandEvent& ev){
        wxString s = ev.GetString();
        if (s.StartsWith("PROGRESS:")) {
            wxString num = s.Mid(9);
            long pct = 0; num.ToLong(&pct);
            m_progress->SetValue((int)pct);
        } else {
            m_log->AppendText(s);
            m_log->AppendText("");
        }
    });
}

MainFrame::~MainFrame() {
    avformat_network_deinit();
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog openFile(this, "Open video file", wxEmptyString, wxEmptyString,
                          "Video files (*.mp4;*.mkv;*.avi;*.mov)|*.mp4;*.mkv;*.avi;*.mov|All files (*.*)|*.*",
                          wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (openFile.ShowModal() == wxID_OK) {
        m_inputPath->SetValue(openFile.GetPath());
    }
}

void MainFrame::OnStart(wxCommandEvent&) {
    if (m_running.load()) {
        wxMessageBox("Conversion already running", "Info");
        return;
    }
    wxString in = m_inputPath->GetValue();
    if (in.IsEmpty()) { wxMessageBox("Choose an input file first", "Error"); return; }
    wxString fmt = m_formatChoice->GetStringSelection();

    m_log->Clear();
    m_progress->SetValue(0);

    bool reencode = m_reencodeCheck->GetValue();

    m_running.store(true);
    m_thread = new ConverterThread(this, std::string(in.mb_str()), std::string(fmt.mb_str()), reencode);
    if (m_thread->Run() != wxTHREAD_NO_ERROR) {
        wxMessageBox("Failed to start conversion thread", "Error");
        m_running.store(false);
        delete m_thread; m_thread = nullptr;
    }
}

void MainFrame::OnClose(wxCloseEvent& ev) {
    if (m_running.load()) {
        if (wxMessageBox("A conversion is running. Quit anyway?", "Confirm", wxYES_NO) != wxYES) { ev.Veto(); return; }
    }
    Destroy();
}

void ConverterThread::Log(const std::string& s) {
    wxCommandEvent* ev = new wxCommandEvent(wxEVT_LOG_UPDATE);
    ev->SetString(s);
    // Queue the event to the GUI thread (wx takes ownership of the event pointer)
    wxQueueEvent(m_handler, ev);
}

void ConverterThread::PostProgress(int pct) {
    wxCommandEvent* ev = new wxCommandEvent(wxEVT_LOG_UPDATE);
    ev->SetString(std::string("PROGRESS:") + std::to_string(pct));
    wxQueueEvent(m_handler, ev);
}

// Simple function to derive output filename from input + format
static std::string make_output_path(const std::string& inPath, const std::string& outFmt) {
    size_t p = inPath.find_last_of("/\\");
    std::string dir = (p==std::string::npos) ? std::string() : inPath.substr(0, p+1);
    std::string base = (p==std::string::npos) ? inPath : inPath.substr(p+1);
    size_t dot = base.find_last_of('.');
    if (dot!=std::string::npos) base = base.substr(0,dot);
    return dir + base + "_converted." + outFmt;
}

// The main converter thread entry. Depending on m_reencode it will either remux (stream copy)
// or decode->encode the video stream (H.264) while copying other streams.
wxThread::ExitCode ConverterThread::Entry() {
    Log(m_reencode ? "Starting encoding conversion..." : "Starting remux (stream-copy) conversion...");

    const char* in_filename = m_input.c_str();
    std::string out_filename = make_output_path(m_input, m_outFormat);

    AVFormatContext* in_ctx = nullptr;
    AVFormatContext* out_ctx = nullptr;
    int ret = 0;

    // Open input
    ret = avformat_open_input(&in_ctx, in_filename, NULL, NULL);
    if (ret < 0) {
        char errbuf[256]; av_strerror(ret, errbuf, sizeof(errbuf));
        Log(std::string("Failed to open input: ") + errbuf);
        m_handler->setRunning(false);
        return (wxThread::ExitCode)0;
    }

    if ((ret = avformat_find_stream_info(in_ctx, NULL)) < 0) {
        Log("Failed to find stream info");
        avformat_close_input(&in_ctx);
        m_handler->setRunning(false);
        return 0;
    }

    // If no re-encode requested -> perform simple remux (stream copy)
    if (!m_reencode) {
        avformat_alloc_output_context2(&out_ctx, NULL, m_outFormat.c_str(), out_filename.c_str());
        if (!out_ctx) {
            Log("Could not create output context (unsupported format?)");
            avformat_close_input(&in_ctx);
            m_handler->setRunning(false);
            return 0;
        }

        std::vector<int> stream_mapping(in_ctx->nb_streams, -1);
        int stream_index = 0;
        for (unsigned i = 0; i < in_ctx->nb_streams; ++i) {
            AVStream* in_stream = in_ctx->streams[i];
            AVCodecParameters* in_par = in_stream->codecpar;
            AVStream* out_stream = avformat_new_stream(out_ctx, NULL);
            if (!out_stream) {
                Log("Failed allocating output stream");
                ret = AVERROR_UNKNOWN; break;
            }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_par);
            if (ret < 0) { Log("Failed to copy codec parameters"); break; }
            out_stream->codecpar->codec_tag = 0;
            stream_mapping[i] = stream_index++;
        }

        if (ret < 0) {
            avformat_close_input(&in_ctx);
            if (out_ctx) avformat_free_context(out_ctx);
            m_handler->setRunning(false);
            return 0;
        }

        if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&out_ctx->pb, out_filename.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                char errbuf[256]; av_strerror(ret, errbuf, sizeof(errbuf));
                Log(std::string("Could not open output file: ") + errbuf);
                avformat_close_input(&in_ctx);
                avformat_free_context(out_ctx);
                m_handler->setRunning(false);
                return 0;
            }
        }

        ret = avformat_write_header(out_ctx, NULL);
        if (ret < 0) {
            Log("Error occurred when writing header");
            if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb);
            avformat_close_input(&in_ctx);
            avformat_free_context(out_ctx);
            m_handler->setRunning(false);
            return 0;
        }

        AVPacket pkt;
        while (true) {
            ret = av_read_frame(in_ctx, &pkt);
            if (ret < 0) break; // EOF or error
            AVStream* in_stream = in_ctx->streams[pkt.stream_index];
            if (pkt.stream_index >= (int)stream_mapping.size() || stream_mapping[pkt.stream_index] < 0) {
                av_packet_unref(&pkt);
                continue;
            }
            pkt.stream_index = stream_mapping[pkt.stream_index];

            AVStream* out_stream = out_ctx->streams[pkt.stream_index];
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                       (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                       (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;

            ret = av_interleaved_write_frame(out_ctx, &pkt);
            if (ret < 0) {
                Log("Error muxing packet");
                av_packet_unref(&pkt);
                break;
            }

            // approximate progress if duration known
            if (in_ctx->duration > 0 && pkt.pts != AV_NOPTS_VALUE) {
                int pct = (int)((pkt.pts * av_q2d(out_stream->time_base) * AV_TIME_BASE) * 100 / in_ctx->duration);
                if (pct < 0) pct = 0; if (pct > 100) pct = 100;
                PostProgress(pct);
            }

            av_packet_unref(&pkt);
            if (TestDestroy()) { Log("Conversion cancelled"); break; }
        }

        av_write_trailer(out_ctx);
        if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb);
        avformat_close_input(&in_ctx);
        avformat_free_context(out_ctx);

        Log(std::string("Remux finished. Output: ") + out_filename);
        m_handler->setRunning(false);
        return (wxThread::ExitCode)0;
    }

    // ---- re-encode path (video -> H.264), copy other streams ----
    int video_stream_index = -1;
    for (unsigned i = 0; i < in_ctx->nb_streams; i++) {
        if (in_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { video_stream_index = i; break; }
    }
    if (video_stream_index < 0) {
        Log("No video stream found for re-encoding");
        avformat_close_input(&in_ctx);
        m_handler->setRunning(false);
        return 0;
    }

    // Open decoder for input video stream
    const AVCodec* dec = avcodec_find_decoder(in_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!dec) { Log("Decoder not found"); avformat_close_input(&in_ctx); m_handler->setRunning(false); return 0; }
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, in_ctx->streams[video_stream_index]->codecpar);
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) { Log("Failed to open decoder"); avcodec_free_context(&dec_ctx); avformat_close_input(&in_ctx); m_handler->setRunning(false); return 0; }

    // Create output context and add streams: video will be encoded, others copied
    avformat_alloc_output_context2(&out_ctx, NULL, m_outFormat.c_str(), out_filename.c_str());
    if (!out_ctx) { Log("Could not create output context"); avcodec_free_context(&dec_ctx); avformat_close_input(&in_ctx); m_handler->setRunning(false); return 0; }

    std::vector<int> stream_mapping(in_ctx->nb_streams, -1);
    int out_stream_cnt = 0;
    for (unsigned i = 0; i < in_ctx->nb_streams; ++i) {
        AVStream* in_stream = in_ctx->streams[i];
        if ((int)i == video_stream_index) {
            // create placeholder stream for encoded video
            AVStream* out_stream = avformat_new_stream(out_ctx, NULL);
            if (!out_stream) { Log("Failed allocating output video stream"); ret = AVERROR_UNKNOWN; break; }
            stream_mapping[i] = out_stream_cnt++;
        } else {
            AVStream* out_stream = avformat_new_stream(out_ctx, NULL);
            if (!out_stream) { Log("Failed allocating output stream"); ret = AVERROR_UNKNOWN; break; }
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            if (ret < 0) { Log("Failed to copy codec parameters for non-video stream"); break; }
            out_stream->codecpar->codec_tag = 0;
            stream_mapping[i] = out_stream_cnt++;
        }
    }
    if (ret < 0) { avformat_close_input(&in_ctx); avformat_free_context(out_ctx); avcodec_free_context(&dec_ctx); m_handler->setRunning(false); return 0; }

    // Find encoder for H.264
    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!enc) { Log("H.264 encoder not found"); avformat_close_input(&in_ctx); avformat_free_context(out_ctx); avcodec_free_context(&dec_ctx); m_handler->setRunning(false); return 0; }

    // Setup encoder context
    AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
    enc_ctx->height = dec_ctx->height;
    enc_ctx->width = dec_ctx->width;
    enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    AVRational framerate = av_guess_frame_rate(in_ctx, in_ctx->streams[video_stream_index], NULL);
    if (framerate.num == 0) framerate = in_ctx->streams[video_stream_index]->r_frame_rate;
    if (framerate.num == 0) framerate = {25,1};
    enc_ctx->time_base = av_inv_q(framerate);
    enc_ctx->framerate = framerate;
    enc_ctx->bit_rate = 800000; // 800kbps default; adjust as needed

    // Open encoder (you can pass AVDictionary for options like preset/crf)
    if ((ret = avcodec_open2(enc_ctx, enc, NULL)) < 0) { Log("Failed to open encoder"); avcodec_free_context(&enc_ctx); avformat_free_context(out_ctx); avcodec_free_context(&dec_ctx); avformat_close_input(&in_ctx); m_handler->setRunning(false); return 0; }

    // Copy encoder params to output video stream
    AVStream* out_video_stream = out_ctx->streams[ stream_mapping[video_stream_index] ];
    avcodec_parameters_from_context(out_video_stream->codecpar, enc_ctx);
    out_video_stream->time_base = enc_ctx->time_base;

    // Open output file
    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_ctx->pb, out_filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) { char errbuf[256]; av_strerror(ret, errbuf, sizeof(errbuf)); Log(std::string("Could not open output file: ") + errbuf); avcodec_free_context(&enc_ctx); avformat_free_context(out_ctx); avcodec_free_context(&dec_ctx); avformat_close_input(&in_ctx); m_handler->setRunning(false); return 0; }
    }

    // Write header
    ret = avformat_write_header(out_ctx, NULL);
    if (ret < 0) { Log("Error occurred when writing header"); if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb); avcodec_free_context(&enc_ctx); avformat_free_context(out_ctx); avcodec_free_context(&dec_ctx); avformat_close_input(&in_ctx); m_handler->setRunning(false); return 0; }

    // Allocate frames/packets
    AVFrame* frame = av_frame_alloc();
    AVFrame* sws_frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    AVPacket* enc_pkt = av_packet_alloc();

    // Prepare swscale for pixel format conversion
    struct SwsContext* sws_ctx = sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
        SWS_BILINEAR, NULL, NULL, NULL);

    sws_frame->format = enc_ctx->pix_fmt;
    sws_frame->width  = enc_ctx->width;
    sws_frame->height = enc_ctx->height;
    av_frame_get_buffer(sws_frame, 32);

    // Read packets and process
    while (true) {
        ret = av_read_frame(in_ctx, pkt);
        if (ret < 0) break; // EOF or error

        if ((int)pkt->stream_index == video_stream_index) {
            // send to decoder
            ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0) { Log("Error sending packet to decoder"); av_packet_unref(pkt); break; }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) { Log("Error during decoding"); goto cleanup; }

                // Convert pixel format to encoder's format
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, dec_ctx->height, sws_frame->data, sws_frame->linesize);
                sws_frame->pts = frame->pts;

                // send to encoder
                ret = avcodec_send_frame(enc_ctx, sws_frame);
                if (ret < 0) { Log("Error sending frame to encoder"); goto cleanup; }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(enc_ctx, enc_pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) { Log("Error during encoding"); goto cleanup; }

                    // rescale and write
                    av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_video_stream->time_base);
                    enc_pkt->stream_index = out_video_stream->index; // use stream index assigned by avformat
                    ret = av_interleaved_write_frame(out_ctx, enc_pkt);
                    av_packet_unref(enc_pkt);
                    if (ret < 0) { Log("Error muxing encoded packet"); goto cleanup; }
                }

                // progress (approx)
                if (in_ctx->duration > 0 && frame->pts != AV_NOPTS_VALUE) {
                    int pct = (int)((frame->pts * av_q2d(in_ctx->streams[video_stream_index]->time_base) * AV_TIME_BASE) * 100 / in_ctx->duration);
                    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
                    PostProgress(pct);
                }

                if (TestDestroy()) { Log("Conversion cancelled"); goto cleanup; }
            }
        } else {
            // copy non-video streams (remux)
            AVStream* in_stream = in_ctx->streams[pkt->stream_index];
            if (pkt->stream_index >= (int)stream_mapping.size() || stream_mapping[pkt->stream_index] < 0) {
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = stream_mapping[pkt->stream_index];

            AVStream* out_stream = out_ctx->streams[pkt->stream_index];
            pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base,
                                       (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base,
                                       (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;

            ret = av_interleaved_write_frame(out_ctx, pkt);
            if (ret < 0) { Log("Error muxing packet for non-video stream"); av_packet_unref(pkt); goto cleanup; }
            av_packet_unref(pkt);
        }
    }

    // flush encoder
    avcodec_send_frame(enc_ctx, NULL);
    while (true) {
        ret = avcodec_receive_packet(enc_ctx, enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) { Log("Error flushing encoder"); break; }
        av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_video_stream->time_base);
        enc_pkt->stream_index = out_video_stream->index;
        av_interleaved_write_frame(out_ctx, enc_pkt);
        av_packet_unref(enc_pkt);
    }

    av_write_trailer(out_ctx);

cleanup:
    av_frame_free(&frame);
    av_frame_free(&sws_frame);
    av_packet_free(&pkt);
    av_packet_free(&enc_pkt);
    sws_freeContext(sws_ctx);

    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);

    if (out_ctx) {
        if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb);
        avformat_free_context(out_ctx);
    }
    avformat_close_input(&in_ctx);

    m_handler->setRunning(false);
    Log(std::string("Conversion finished. Output: ") + out_filename);
    return (wxThread::ExitCode)0;
}

class MyApp : public wxApp {
public:
    virtual bool OnInit() override {
        MainFrame* f = new MainFrame();
        f->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
