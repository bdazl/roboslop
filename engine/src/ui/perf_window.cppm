module;

#include <imgui.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

export module roboslop.ui.perf_window;

import roboslop.time.frame_stats;
import roboslop.ui;

namespace roboslop {

namespace {

// One row of the summary table. The pointer-to-member is what the row
// reads out of every held sample.
struct StatRow {
    const char* label;
    double FrameSample::* field;
};

auto drawSummaryTable(const FrameStats& stats) -> void {
    static constexpr std::array<StatRow, 6> Rows{{
        {.label = "frame (cpu)", .field = &FrameSample::cpuFrameMs},
        {.label = "  fixed", .field = &FrameSample::fixedMs},
        {.label = "  render", .field = &FrameSample::renderMs},
        {.label = "gpu", .field = &FrameSample::gpuMs},
        {.label = "wait submit", .field = &FrameSample::waitSubmitMs},
        {.label = "wait render", .field = &FrameSample::waitRenderMs},
    }};

    if (!ImGui::BeginTable("perf", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("ms");
    ImGui::TableSetupColumn("p50");
    ImGui::TableSetupColumn("p95");
    ImGui::TableSetupColumn("p99");
    ImGui::TableSetupColumn("min");
    ImGui::TableSetupColumn("max");
    ImGui::TableHeadersRow();

    for (const auto& row : Rows) {
        const auto s = stats.summary(row.field);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.label);
        for (const double v : {s.p50, s.p95, s.p99, s.min, s.max}) {
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", v);
        }
    }
    ImGui::EndTable();
}

} // namespace

// The engine's own performance overlay. App registers it when the dev UI
// is enabled, so every application gets it without opting in. `mode`
// describes the bgfx threading configuration; it is fixed for the life of
// the context, so it is captured once rather than re-queried per frame.
export [[nodiscard]] auto makePerfWindow(const FrameStats& stats, std::string mode) -> DevWindow {
    return DevWindow{
        .id = "performance",
        .title = "Performance",
        .draw =
            [&stats, mode = std::move(mode)]() {
                if (stats.empty()) {
                    ImGui::TextUnformatted("no frames recorded yet");
                    return;
                }
                const auto& last = stats.latest();
                const auto frame = stats.summary(&FrameSample::cpuFrameMs);
                const auto gpu = stats.summary(&FrameSample::gpuMs);
                ImGui::Text(
                    "%.0f FPS | frame %.2f ms | GPU %.2f ms",
                    frame.mean > 0.0 ? 1000.0 / frame.mean : 0.0,
                    frame.mean,
                    gpu.mean
                );
                if (!ImGui::CollapsingHeader("Details")) {
                    return;
                }
                ImGui::Text(
                    "%u draws  %ux%u  %s",
                    last.drawCalls,
                    last.backbufferWidth,
                    last.backbufferHeight,
                    mode.c_str()
                );

                const auto frames = stats.collect(&FrameSample::cpuFrameMs);
                const auto summary = summarise(frames);
                // ImGui plots floats; the ring keeps doubles because the
                // percentiles are computed from them.
                std::vector<float> plot;
                plot.reserve(frames.size());
                for (const double v : frames) {
                    plot.push_back(static_cast<float>(v));
                }
                ImGui::PlotLines(
                    "##frametime",
                    plot.data(),
                    static_cast<int>(plot.size()),
                    0,
                    nullptr,
                    0.0F,
                    static_cast<float>(summary.max),
                    ImVec2(0.0F, 60.0F)
                );
                ImGui::Separator();
                drawSummaryTable(stats);
                ImGui::Text("window: %zu / %zu frames", stats.size(), stats.capacity());
            },
        .visible = false,
        .autoResize = true,
    };
}

} // namespace roboslop
