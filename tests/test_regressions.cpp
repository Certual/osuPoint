#include "osupoint/motion_filters.h"
#include "osupoint/config_manager.h"
#include <cmath>
#include <cstdio>
#include "osupoint/driver_service.h"
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <cstdlib>
#include <new>

static std::atomic<bool> track_allocations{false};
static std::atomic<size_t> allocations{0};
[[gnu::noinline]] void* operator new(size_t size) {
    if (track_allocations.load(std::memory_order_relaxed)) allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static int failures = 0;
static void Check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

namespace osupoint {
struct DriverServiceTestAccess {
    static void InterruptConfigWriter(DriverService& service) { service.m_local_config.seq.fetch_or(1); }
    static void ResumeConfigWriter(DriverService& service) { service.m_local_config.seq.fetch_and(~uint64_t{1}); }
    static uint64_t DiagnosticRevision(DriverService& service) { return service.m_local_debug.seq.load(); }
};
}

static RuntimeTabletSpec TestSpec() {
    RuntimeTabletSpec spec{};
    spec.vid = 0x056a; spec.pid = 0x037a; spec.report_id = 2;
    spec.max_x = 15200; spec.max_y = 9500;
    spec.phys_w = 152; spec.phys_h = 95;
    spec.x_offset = 2; spec.y_offset = 4; spec.coord_bits = 16;
    return spec;
}

static void TestPipeline() {
    auto spec = TestSpec();
    TransformConfig cfg{};
    osupoint::OsuPointConfig config;
    config.ToTransformConfig(spec, osupoint::DisplayGeometry{}, cfg);
    cfg.mouse_mode = 0;
    osupoint::InputPipeline pipeline;
    pipeline.Configure(spec, cfg);
    PenSample sample{};
    sample.raw_x = 4000; sample.raw_y = 3000;
    sample.flags = PEN_FLAG_IN_RANGE | PEN_FLAG_PRESSED | PEN_FLAG_RAW_HW_TIP;
    sample.pressure = 20;
    osupoint::InputOutput out{};
    Check(pipeline.Process(sample, 0.004, out), "actual pipeline accepts a valid sample");
    pipeline.Commit(out);
    sample.pressure = 21;
    pipeline.Process(sample, 0.004, out);
    Check(pipeline.ShouldEmit(out), "stationary pressure change is not discarded");
    pipeline.Commit(out);
    sample.flags = 0; sample.pressure = 0;
    pipeline.Process(sample, 0.004, out);
    Check(pipeline.ShouldEmit(out) && !out.in_range && !out.left, "stationary pen lift releases the tool and tip");

    cfg.filter_1euro = 80;
    pipeline.Configure(spec, cfg);
    sample.flags = PEN_FLAG_IN_RANGE; sample.raw_x = 0;
    pipeline.Process(sample, 0.004, out);
    sample.raw_x = 5000;
    pipeline.Process(sample, 0.004, out);
    const int lagged = out.x;
    pipeline.Commit(out);
    pipeline.Process(sample, 0.004, out);
    Check(out.x > lagged && pipeline.ShouldEmit(out), "repeated raw coordinates still advance filter settling");
    pipeline.Process(sample, 10.0, out);
    Check(out.x > lagged, "a long pause does not retain accumulated filter lag");

    osupoint::InputPipeline high_resolution;
    auto high_spec = spec; high_spec.max_x *= 10; high_spec.max_y *= 10;
    TransformConfig high_cfg = cfg;
    osupoint::ComputeTransform(high_spec, osupoint::AreaConfig{}, osupoint::MonitorRect{}, osupoint::VirtualDesktop{}, high_cfg);
    pipeline.Reset(); pipeline.Configure(spec, cfg); high_resolution.Configure(high_spec, high_cfg);
    bool same = true;
    for (int i = 0; i < 100; ++i) {
        sample.raw_x = i * 20; sample.raw_y = 1000;
        pipeline.Process(sample, 0.004, out);
        auto high_sample = sample; high_sample.raw_x *= 10; high_sample.raw_y *= 10;
        osupoint::InputOutput high_out{};
        high_resolution.Process(high_sample, 0.004, high_out);
        same &= out.x == high_out.x && out.y == high_out.y;
    }
    Check(same, "filter response is independent of tablet coordinate resolution");

    osupoint::OsuPointConfig saved;
    saved.width = 50; saved.height = 30; saved.center_x = 76; saved.center_y = 47.5;
    saved.cached_desktop[0] = -1920; saved.cached_desktop[2] = 3840; saved.cached_desktop[3] = 1080;
    saved.cached_monitor[2] = 1920; saved.cached_monitor[3] = 1080;
    auto restored = ConfigManager::ParseIni(ConfigManager::SerializeIni(saved));
    restored.ToTransformConfig(spec, cfg);
    pipeline.Reset(); pipeline.Configure(spec, cfg);
    sample.raw_x = 0; sample.raw_y = 4750;
    pipeline.Process(sample, 0.004, out);
    const int unclipped = out.x;
    cfg.clip_area = 1; pipeline.Configure(spec, cfg); pipeline.Process(sample, 0.004, out);
    Check(out.x > unclipped && out.x >= 32760, "clip constrains output to the selected monitor, not just the virtual desktop");
    Check(restored.cached_desktop[0] == -1920 && cfg.vshift[0] > 32760,
          "negative-origin multi-monitor geometry survives headless config reload");
    Check(cfg.output_resolution_x > 0 && cfg.output_resolution_y > 0 &&
          std::abs(double(cfg.output_resolution_y) / cfg.output_resolution_x - 3840.0 / 1080) < 1e-9,
          "virtual stylus advertises nonzero axis resolution and correct desktop aspect");
    auto rebound = cfg;
    osupoint::RebindTabletTransform(high_spec, rebound);
    high_resolution.Reset(); high_resolution.Configure(high_spec, rebound);
    sample.raw_x = 7600; sample.raw_y = 4750;
    pipeline.Process(sample, 0.004, out);
    auto scaled = sample; scaled.raw_x *= 10; scaled.raw_y *= 10;
    osupoint::InputOutput high_out{};
    high_resolution.Process(scaled, 0.004, high_out);
    Check(out.x == high_out.x && out.y == high_out.y, "reconnecting a different-resolution tablet retains display mapping");
    Check(rebound.output_resolution_x == cfg.output_resolution_x && rebound.output_resolution_y == cfg.output_resolution_y,
          "tablet reconnect does not replace the virtual output resolution");
}

static void TestVirtualSoak() {
    const auto spec = TestSpec();
    TransformConfig cfg;
    cfg.filter_1euro = 35; cfg.filter_motion = 40; cfg.extrapolation = 1; cfg.extrapolation_tau = 0.5f;
    osupoint::InputPipeline pipeline;
    pipeline.Configure(spec, cfg);
    osupoint::DebugSession debug;
    debug.SetEnabled(true, 1);
    PenSample sample{};
    sample.raw_x = 4000; sample.raw_y = 3000; sample.flags = PEN_FLAG_IN_RANGE;
    osupoint::InputOutput out{};
    uint8_t packet[10] = {2, 0x21};
    osupoint::DebugPoint point{};
    const size_t count = std::getenv("OSUPOINT_TEST_FAST") ? 10000 : 7200000; // 8 virtual hours at 250 Hz
    allocations.store(0);
    track_allocations.store(true);
    bool finite = true;
    for (size_t i = 0; i < count; ++i) {
        finite &= pipeline.Process(sample, 0.004, out, &point);
        const uint64_t now = 1 + i * 4000000ULL;
        debug.Receive(now, packet, sizeof(packet));
        debug.Decode(0);
        debug.Output(1000, true, false, 0, sample.raw_x, sample.raw_y, out.x, out.y, 0, sample.flags, point);
    }
    track_allocations.store(false);
    auto snapshot = debug.Snapshot(1 + count * 4000000ULL);
    Check(finite && snapshot.received == count && snapshot.latency_samples == 512 && snapshot.trace_count == 64,
          "virtual long-run input does not grow queues/history or lose valid output");
    Check(allocations.load() == 0, "actual filter and diagnostic hot paths make zero heap allocations");
}

static void TestDiagnostics() {
    osupoint::DebugSession session;
    const uint8_t packet[] = {2, 0x21, 0, 0};
    session.Receive(1000, packet, sizeof(packet));
    session.Output(1000, true, false, 0, 1, 1, 1, 1, 10, 3, {});
    Check(!session.Snapshot(2000).enabled && !session.Snapshot(2000).received, "disabled diagnostics process no samples");
    session.SetEnabled(true, 1000000);
    for (uint64_t latency : {1000ULL, 9000ULL, 2000ULL}) {
        session.Receive(1000000 + latency, packet, sizeof(packet));
        session.Decode(0);
        session.Output(latency, true, false, 0, 1, 1, 1, 1, 10, 3, {});
    }
    auto snapshot = session.Snapshot(2000000);
    Check(snapshot.latency_p50_us == 2 && snapshot.latency_p99_us == 9, "reported percentiles are real percentiles, not the last sample");
    for (int i = 0; i < 100000; ++i) {
        session.Receive(3000000ULL + uint64_t(i) * 4000000ULL, packet, sizeof(packet));
        session.Output(1000, true, false, 0, i, i, i, i, 1, 3, {});
    }
    snapshot = session.Snapshot(500000000000ULL);
    Check(snapshot.latency_samples == 512 && snapshot.interval_samples == 512 && snapshot.trace_count == 64,
          "long captures have fixed-size buffers");
    session.SetEnabled(false);
    snapshot = session.Snapshot(600000000000ULL);
    Check(!snapshot.received && !snapshot.started_ns && !snapshot.trace_count && !snapshot.packet_size &&
          !snapshot.latency_samples && !snapshot.latency_session_max_us, "OFF erases counters, packets, history and session maxima");
    session.SetEnabled(true, 800000000000ULL);
    Check(!session.Snapshot(800000001000ULL).received, "a new capture cannot resurrect the previous session");
}

static void TestFailedWrite() {
    int fds[2];
    Check(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0, "create isolated output pipe");
    UinputEmitter emitter;
    emitter.InitializeForTesting(fds[1]);
    emitter.EmitMouseSample(100, 100, false, false);
    char buffer[4096]{}; while (read(fds[0], buffer, sizeof(buffer)) > 0) {}
    while (write(fds[1], buffer, sizeof(buffer)) > 0) {}
    Check(!emitter.EmitMouseSample(200, 200, true, false), "nonblocking output backpressure is detected");
    while (read(fds[0], buffer, sizeof(buffer)) > 0) {}
    emitter.EmitMouseSample(200, 200, true, false);
    input_event events[16];
    const ssize_t n = read(fds[0], events, sizeof(events));
    bool pressed = false;
    for (ssize_t i = 0; i < n / static_cast<ssize_t>(sizeof(input_event)); ++i)
        pressed |= events[i].type == EV_KEY && events[i].code == BTN_LEFT && events[i].value == 1;
    Check(pressed, "a failed write cannot consume a button transition");
    close(fds[0]); close(fds[1]);
}

static bool WaitCapture(osupoint::DriverService& service, bool enabled, uint64_t received = 0) {
    const auto deadline = GetMonotonicTimeNs() + 700000000ULL;
    while (GetMonotonicTimeNs() < deadline) {
        osupoint::DebugSnapshot snapshot{};
        service.ReadDiagnostics(snapshot);
        if (bool(snapshot.enabled) == enabled && snapshot.received >= received) return true;
        usleep(1000);
    }
    return false;
}

static void TestWorkerLifecycle() {
    int sockets[2], output[2];
    Check(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0 &&
          pipe2(output, O_NONBLOCK | O_CLOEXEC) == 0, "create isolated driver loopback descriptors");
    const auto spec = TestSpec();
    TransformConfig cfg{};
    auto& service = osupoint::DriverService::Instance();
    Check(service.StartForTesting(sockets[1], output[1], spec, cfg), "actual worker starts without opening hardware");
    uint8_t packet[10] = {2, 0x21, 0xE8, 3, 0xD0, 7, 0, 4, 0, 0};
    send(sockets[0], packet, sizeof(packet), 0);
    usleep(30000);
    osupoint::DebugSnapshot snapshot{};
    Check(!service.ReadDiagnostics(snapshot) && !snapshot.received, "actual worker has no diagnostic collector when OFF");
    const uint64_t off_revision = osupoint::DriverServiceTestAccess::DiagnosticRevision(service);
    usleep(250000);
    Check(osupoint::DriverServiceTestAccess::DiagnosticRevision(service) == off_revision,
          "diagnostic storage is not updated or polled by a periodic collector while OFF");
    cfg.debug_mode = 1;
    Check(service.UpdateConfig(cfg) && WaitCapture(service, true), "debug ON starts a fresh worker-owned capture");
    for (int i = 0; i < 3; ++i) { ++packet[2]; send(sockets[0], packet, sizeof(packet), 0); }
    Check(WaitCapture(service, true, 3), "actual HID decoder/output loop contributes diagnostic samples");
    cfg.mouse_mode = 0;
    Check(service.UpdateConfig(cfg), "mouse/stylus mode change is accepted");
    ++packet[2]; send(sockets[0], packet, sizeof(packet), 0);
    bool saw_pressure = false;
    const uint64_t mode_deadline = GetMonotonicTimeNs() + 250000000ULL;
    while (GetMonotonicTimeNs() < mode_deadline && !saw_pressure) {
        input_event events[32]; const ssize_t count = read(output[0], events, sizeof(events));
        for (ssize_t i = 0; i < count / static_cast<ssize_t>(sizeof(input_event)); ++i)
            saw_pressure |= events[i].type == EV_ABS && events[i].code == ABS_PRESSURE;
        usleep(1000);
    }
    Check(saw_pressure, "stylus mode takes effect immediately without waiting for reconnect backoff");
    cfg.debug_mode = 0;
    Check(service.UpdateConfig(cfg) && !service.ReadDiagnostics(snapshot) && !snapshot.packet_size,
          "OFF acknowledges actual cleanup before returning");
    bool clean = true;
    for (int i = 0; i < 100; ++i) {
        cfg.debug_mode = 1; clean &= service.UpdateConfig(cfg) && WaitCapture(service, true);
        service.ReadDiagnostics(snapshot); clean &= snapshot.received == 0;
        cfg.debug_mode = 0; clean &= service.UpdateConfig(cfg);
        service.ReadDiagnostics(snapshot); clean &= snapshot.received == 0 && !snapshot.enabled;
    }
    Check(clean, "100 Off-On-Off cycles retain no packets or old capture state");
    const uint64_t before = GetMonotonicTimeNs();
    service.Stop();
    Check(GetMonotonicTimeNs() - before < 100000000ULL, "idle worker stop is promptly woken without signals or stuck reads");
    Check(!service.IsConnected() && !service.IsRunning(), "stop leaves no running input worker");
    close(sockets[0]); close(sockets[1]); close(output[0]); close(output[1]);
}

static void TestDrainToLatestBacklogElimination() {
    int sockets[2], output[2];
    socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets);
    pipe2(output, O_NONBLOCK | O_CLOEXEC);
    auto& service = osupoint::DriverService::Instance();
    TransformConfig cfg{};
    cfg.mouse_mode = 1;
    cfg.pen_click = 1;
    cfg.debug_mode = 1;
    const auto spec = TestSpec();
    Check(service.StartForTesting(sockets[1], output[1], spec, cfg), "drain test worker starts cleanly");
    WaitCapture(service, true);

    for (int i = 0; i < 10; ++i) {
        uint8_t packet[10] = {2, 0x20, 0, 0, 0, 0, 0, 0, 0, 0};
        if (i == 4) {
            packet[1] = 0x21; // in-range + pressed
            packet[6] = 0xE8; // pressure = 1000
            packet[7] = 0x03;
        } else {
            packet[1] = 0x20; // in-range
            packet[6] = 0;
            packet[7] = 0;
        }
        const uint16_t x = static_cast<uint16_t>(1000 + i * 400);
        const uint16_t y = static_cast<uint16_t>(1000 + i * 200);
        packet[2] = static_cast<uint8_t>(x & 0xFF);
        packet[3] = static_cast<uint8_t>((x >> 8) & 0xFF);
        packet[4] = static_cast<uint8_t>(y & 0xFF);
        packet[5] = static_cast<uint8_t>((y >> 8) & 0xFF);
        send(sockets[0], packet, sizeof(packet), 0);
    }

    Check(WaitCapture(service, true, 10), "drain worker received all 10 packets in queue");

    bool saw_click_press = false;
    bool saw_click_release = false;
    int32_t final_x = -1;
    int syn_reports = 0;

    const uint64_t deadline = GetMonotonicTimeNs() + 300000000ULL;
    while (GetMonotonicTimeNs() < deadline) {
        input_event events[64];
        const ssize_t count = read(output[0], events, sizeof(events));
        if (count > 0) {
            const size_t n = count / sizeof(input_event);
            for (size_t i = 0; i < n; ++i) {
                if (events[i].type == EV_KEY && events[i].code == BTN_LEFT) {
                    if (events[i].value == 1) saw_click_press = true;
                    if (events[i].value == 0) saw_click_release = true;
                }
                if (events[i].type == EV_ABS && events[i].code == ABS_X) final_x = events[i].value;
                if (events[i].type == EV_SYN && events[i].code == SYN_REPORT) ++syn_reports;
            }
        } else {
            usleep(2000);
        }
    }

    Check(final_x > 15000, "drain-to-latest delivers latest coordinates from the queue");
    Check(saw_click_press, "micro-tap press edge synthesized from drained batch");
    Check(saw_click_release, "micro-tap release edge preserved after synthesis");
    Check(syn_reports <= 4, "drained batch eliminates redundant intermediate OS motion events");

    service.Stop();
    close(sockets[0]); close(sockets[1]); close(output[0]); close(output[1]);
}

static void TestInterruptedWriter() {
    int sockets[2], output[2];
    socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets);
    pipe2(output, O_NONBLOCK | O_CLOEXEC);
    auto& service = osupoint::DriverService::Instance();
    TransformConfig cfg; cfg.debug_mode = 1;
    service.StartForTesting(sockets[1], output[1], TestSpec(), cfg);
    WaitCapture(service, true);
    osupoint::DriverServiceTestAccess::InterruptConfigWriter(service);
    uint8_t packet[10] = {2, 0x21, 0xE8, 3, 0xD0, 7, 0, 4, 0, 0};
    send(sockets[0], packet, sizeof(packet), 0);
    Check(WaitCapture(service, true, 1), "an interrupted config writer cannot stall the input stream");
    cfg.debug_mode = 0;
    const uint64_t before = GetMonotonicTimeNs();
    const bool update = service.UpdateConfig(cfg);
    osupoint::DebugSnapshot snapshot{};
    Check(!update && errno == EBUSY && GetMonotonicTimeNs() - before < 100000000ULL,
          "interrupted writers return a bounded control error instead of hanging the GUI");
    Check(!service.ReadDiagnostics(snapshot) && !snapshot.received,
          "debug OFF still erases capture when configuration storage is interrupted");
    osupoint::DriverServiceTestAccess::ResumeConfigWriter(service);
    service.Stop();
    close(sockets[0]); close(sockets[1]); close(output[0]); close(output[1]);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    if (std::getenv("OSUPOINT_TEST_FROZEN_REPRO")) { TestInterruptedWriter(); return failures ? 1 : 0; }
    // A disabled filter must not retain an old trajectory when enabled again.
    TransformConfig cfg{};
    cfg.filter_1euro = 70;
    FilterCascade filters;
    filters.UpdateConfig(cfg);
    double x = 0.0, y = 0.0;
    filters.Process(0.0, 0.0, 0.004, x, y);
    filters.Process(20.0, 0.0, 0.004, x, y);
    cfg.filter_1euro = 0;
    filters.UpdateConfig(cfg);
    filters.Process(100.0, 0.0, 0.004, x, y);
    cfg.filter_1euro = 70;
    filters.UpdateConfig(cfg);
    filters.Process(100.0, 0.0, 0.004, x, y);
    Check(std::abs(x - 100.0) < 1e-9, "filter re-enable starts at the current position");

    filters.Reset();
    filters.Process(0.0, 0.0, 0.004, x, y);
    filters.Process(20.0, 0.0, 0.004, x, y);
    filters.Process(100.0, 0.0, 5.0, x, y);
    Check(std::abs(x - 100.0) < 1e-9, "idle gap resets smoothing instead of replaying old state");

    auto bad = ConfigManager::ParseIni("width=nan\nheight=inf\nrotation=nan\nextrapolation_mm=nan\n");
    Check(std::isfinite(bad.width) && std::isfinite(bad.height) &&
          std::isfinite(bad.rotation) && std::isfinite(bad.extrapolation_mm),
          "non-finite configuration cannot reach the input pipeline");
    auto misc = ConfigManager::ParseIni("active_tab=2\nbg_blur_type=3\n");
    Check(misc.active_tab == 2 && misc.bg_blur_type == 3,
          "Misc tab and fourth blur choice survive a config roundtrip");
    TestPipeline();
    TestDiagnostics();
    TestFailedWrite();
    TestWorkerLifecycle();
    TestDrainToLatestBacklogElimination();
    TestInterruptedWriter();
    TestVirtualSoak();
    return failures ? 1 : 0;
}
