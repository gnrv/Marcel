#include "WorkerApp.h"

#include "engine/ClingEngine.h"
#include "ipc/Serialize.h"
#include "render/UiFonts.h"
#include "worker/SlideRenderer.h"
#include "Presentation.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include <cstdio>
#include <utility>

#include <unistd.h>

WorkerApp::WorkerApp(const ipc::HelloMsg &hello, SendFn send)
    : hello_(hello), send_(std::move(send))
{
    scratch_ = std::filesystem::temp_directory_path() /
               ("marcel_worker." + std::to_string(getpid()));
    std::filesystem::create_directories(scratch_);
}

WorkerApp::~WorkerApp() = default;

bool WorkerApp::initGL()
{
    if (!gl_.init())
        return false;

    atlas_ = IM_NEW(ImFontAtlas)();
    UiFonts fonts = LoadUiFonts(atlas_, hello_.dpi_scale);
    big_font_ = fonts.fira_sans_big;

    // Compile-time context: setup code historically ran inside main's
    // active ImGui frame and may call ImGui/GL at compile time, so compiles
    // happen inside a NewFrame/EndFrame bracket on this context.
    compile_ctx_ = ImGui::CreateContext(atlas_);
    ImGui::SetCurrentContext(compile_ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(hello_.design_w),
                            static_cast<float>(hello_.design_h));
    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(hello_.dpi_scale);
    ImGui_ImplOpenGL3_Init("#version 130");
    return true;
}

void WorkerApp::initEngine(int argc, char **argv)
{
    engine_ = std::make_unique<ClingEngine>(argc, argv);
}

void WorkerApp::post(WorkerEvent ev)
{
    std::lock_guard<std::mutex> lock(queue_mx_);
    if (ev.type == WorkerEvent::Type::Shutdown)
        quit_ = true;
    else if (ev.type == WorkerEvent::Type::FrameBegin && !queue_.empty() &&
             queue_.back().type == WorkerEvent::Type::FrameBegin)
        queue_.back() = std::move(ev); // mailbox: newest frame input wins
    else
        queue_.push_back(std::move(ev));
    queue_cv_.notify_one();
}

void WorkerApp::run()
{
    for (;;) {
        WorkerEvent ev;
        {
            std::unique_lock<std::mutex> lock(queue_mx_);
            queue_cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
            if (quit_ && queue_.empty())
                return;
            ev = std::move(queue_.front());
            queue_.pop_front();
        }
        switch (ev.type) {
        case WorkerEvent::Type::SetSource:
            handleSetSource(ev);
            break;
        case WorkerEvent::Type::FrameBegin:
            handleFrameBegin(ev);
            break;
        case WorkerEvent::Type::BufferRelease:
            state(ev.slide).ring.release(ev.buffer_index);
            break;
        case WorkerEvent::Type::Shutdown:
            return;
        }
    }
}

WorkerApp::SlideState &WorkerApp::state(int32_t slide)
{
    return slides_[slide];
}

SourceFile &WorkerApp::sourceFor(int32_t slide, SlideState &st)
{
    if (!st.src) {
        std::string name = slide < 0 ? "setup.cpp"
                                     : "slide" + std::to_string(slide) + ".cpp";
        st.src = std::make_unique<SourceFile>(scratch_ / name);
    }
    return *st.src;
}

void WorkerApp::handleSetSource(WorkerEvent &ev)
{
    ipc::CompileBusyMsg busy{ev.slide, ev.request_id};
    send_(ipc::MsgType::CompileBusy, &busy, sizeof(busy), nullptr, 0);

    SlideState &st = state(ev.slide);
    SourceFile &sf = sourceFor(ev.slide, st);
    sf.setText(ev.text);
    sf.execute(); // reset validated/compiled/syntax_error, count lines
    sf.is_cuda = ev.is_cuda;

    std::string exception;
    if (engine_) {
        ImGuiContext *prev = ImGui::GetCurrentContext();
        bool framed = compile_ctx_ != nullptr;
        if (framed) {
            ImGui::SetCurrentContext(compile_ctx_);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
        }
        try {
            if (ev.slide < 0)
                engine_->compileSetup(sf);
            else
                engine_->compileSlide(sf);
        } catch (std::exception &e) {
            exception = e.what();
        }
        if (framed) {
            ImGui::EndFrame();
            ImGui::SetCurrentContext(prev);
        }
    } else {
        exception = "worker engine not initialized";
    }

    ipc::CompileResultMsg res{};
    res.slide = ev.slide;
    res.request_id = ev.request_id;
    res.validated = sf.validated;
    res.compiled = sf.compiled;
    res.syntax_error = sf.syntax_error;
    res.has_function = sf.function != nullptr;
    res.value_len = static_cast<uint32_t>(sf.value.size());
    res.exception_len = static_cast<uint32_t>(exception.size());
    res.stderr_len = 0; // stderr tail lands with the crash panel (step 5)
    res.num_markers = static_cast<uint32_t>(sf.error_markers.size());

    ipc::Writer w;
    w.append(res);
    w.appendString(sf.value);
    w.appendString(exception);
    for (const auto &[line, text] : sf.error_markers) {
        ipc::ErrorMarkerWire mw{static_cast<uint32_t>(line),
                                static_cast<uint32_t>(text.size())};
        w.append(mw);
        w.appendString(text);
    }
    send_(ipc::MsgType::CompileResult, w.data(), w.size(), nullptr, 0);
}

void WorkerApp::handleFrameBegin(WorkerEvent &ev)
{
    ipc::Writer done;
    ipc::FrameDoneMsg done_msg{ev.frame.frame_id,
                               static_cast<uint32_t>(ev.inputs.size())};
    done.append(done_msg);

    std::vector<ipc::InputEvent> slide_events;
    for (const ipc::SlideInput &input : ev.inputs) {
        ipc::SlideFrameResult r{};
        r.slide = input.slide;
        std::string exception;

        SlideState &st = state(input.slide);
        bool has_content = st.src && (st.src->function || !st.src->value.empty());
        if (input.visible && has_content && gl_.valid()) {
            if (!st.renderer)
                st.renderer = std::make_unique<SlideRenderer>(
                    hello_.design_w, hello_.design_h, hello_.dpi_scale, atlas_, big_font_);
            int b = st.renderer->valid() ? st.ring.acquire() : ipc::BufferRing::kInvalid;
            if (b != ipc::BufferRing::kInvalid) {
                ShmBuffer &buf = st.buffers[b];
                size_t bytes = size_t(hello_.design_w) * hello_.design_h * 4;
                bool announce = !buf.valid();
                if (announce && !buf.create(bytes)) {
                    st.ring.release(static_cast<uint32_t>(b));
                    b = ipc::BufferRing::kInvalid;
                }
                if (b != ipc::BufferRing::kInvalid) {
                    if (announce) {
                        ipc::TextureAnnounceMsg ann{};
                        ann.slide = input.slide;
                        ann.buffer_index = static_cast<uint32_t>(b);
                        ann.transport = ipc::kTransportShm;
                        ann.width = hello_.design_w;
                        ann.height = hello_.design_h;
                        ann.fourcc = ipc::kFourccAbgr8888;
                        ann.num_planes = 1;
                        ann.stride[0] = hello_.design_w * 4;
                        int fd = buf.fd();
                        send_(ipc::MsgType::TextureAnnounce, &ann, sizeof(ann), &fd, 1);
                    }
                    slide_events.clear();
                    for (const ipc::InputEvent &e : ev.events)
                        if (e.slide == input.slide)
                            slide_events.push_back(e);
                    SlideRenderer::Result rr = st.renderer->render(
                        st.src->function, st.src->value, ev.frame.time,
                        ev.frame.delta_time, input, slide_events);
                    st.renderer->readPixels(buf.map());
                    r.buffer_index = static_cast<uint32_t>(b);
                    r.rendered = rr.rendered || !rr.exception.empty();
                    r.want_capture_mouse = rr.want_capture_mouse;
                    r.want_capture_keyboard = rr.want_capture_keyboard;
                    r.want_text_input = rr.want_text_input;
                    exception = rr.exception;
                }
            }
        }

        r.exception_len = static_cast<uint32_t>(exception.size());
        done.append(r);
        done.appendString(exception);
    }
    send_(ipc::MsgType::FrameDone, done.data(), done.size(), nullptr, 0);
}
