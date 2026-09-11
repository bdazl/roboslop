module;

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.ui.terminal;

import roboslop.shell;

namespace roboslop {

export struct TerminalConfig {
    std::size_t maxLines = 2000; // scrollback
};

// A line-based terminal widget over a Shell: scrollback, prompt, input
// with history (Up/Down) and completion (Tab), `clear`, and `tail -f`
// follow mode (stopped with Ctrl+C or by running another command). No
// VT emulation — the shell produces plain lines. Draw it from a
// registered DevWindow's callback.
export class TerminalWindow {
  public:
    explicit TerminalWindow(Shell& shell, TerminalConfig config = {}) : shell(&shell), cfg(config) {
        append("roboslop shell — type `help` for commands, Tab to complete.");
    }

    TerminalWindow(const TerminalWindow&) = delete;
    auto operator=(const TerminalWindow&) -> TerminalWindow& = delete;
    TerminalWindow(TerminalWindow&&) noexcept = default;
    auto operator=(TerminalWindow&&) noexcept -> TerminalWindow& = default;
    ~TerminalWindow() = default;

    // Runs a line as if typed (used by apps for startup commands).
    auto run(std::string_view line) -> void {
        stopFollowing();
        append(shell->prompt() + std::string{line});
        const ShellResult r = shell->execute(line);
        if (r.clear) {
            lines.clear();
        }
        appendBlock(r.output);
        if (r.followPath) {
            following = *r.followPath;
            const auto current = shell->vfs().readFile(*following);
            followedBytes = current ? current->size() : 0;
            append("-- following " + *following + " (Ctrl+C to stop) --");
        }
        scrollToBottom = true;
    }

    auto focus() noexcept -> void {
        focusInput = true;
    }

    auto draw() -> void {
        pollFollow();

        const float footer = ImGui::GetFrameHeightWithSpacing() +
                             (following ? ImGui::GetTextLineHeightWithSpacing() : 0.0F);
        ImGui::BeginChild(
            "scrollback",
            ImVec2(0.0F, -footer),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_HorizontalScrollbar
        );
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 1.0F));
        for (const auto& line : lines) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopStyleVar();
        if (scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F) {
            ImGui::SetScrollHereY(1.0F);
        }
        scrollToBottom = false;
        // Ctrl+C anywhere in the terminal stops a follow.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            if (following) {
                append("^C");
                stopFollowing();
            }
        }
        ImGui::EndChild();

        if (following) {
            ImGui::TextDisabled("following %s — Ctrl+C to stop", following->c_str());
        }

        ImGui::TextUnformatted(shell->prompt().c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0F);
        if (focusInput) {
            ImGui::SetKeyboardFocusHere();
            focusInput = false;
        }
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_CallbackCompletion |
                                          ImGuiInputTextFlags_CallbackHistory;
        if (ImGui::InputText(
                "##cmd", input.data(), input.size(), flags, &TerminalWindow::callback, this
            )) {
            const std::string line{input.data()};
            input.fill('\0');
            historyPos = -1;
            run(line);
            focusInput = true;
        }
    }

    [[nodiscard]] auto scrollback() const noexcept -> const std::deque<std::string>& {
        return lines;
    }

  private:
    auto append(std::string line) -> void {
        lines.push_back(std::move(line));
        while (lines.size() > cfg.maxLines) {
            lines.pop_front();
        }
    }

    auto appendBlock(std::string_view text) -> void {
        std::size_t i = 0;
        while (i < text.size()) {
            const auto j = text.find('\n', i);
            append(
                std::string{
                    text.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i)
                }
            );
            if (j == std::string_view::npos) {
                break;
            }
            i = j + 1;
        }
    }

    auto stopFollowing() -> void {
        following.reset();
        followedBytes = 0;
    }

    // tail -f: append whatever was written to the followed file since
    // the last frame. A file that shrank (rotated, cleared) restarts
    // from its new end.
    auto pollFollow() -> void {
        if (!following) {
            return;
        }
        const auto current = shell->vfs().readFile(*following);
        if (!current) {
            append("-- " + *following + " vanished --");
            stopFollowing();
            return;
        }
        if (current->size() < followedBytes) {
            followedBytes = current->size();
            return;
        }
        if (current->size() > followedBytes) {
            appendBlock(std::string_view{*current}.substr(followedBytes));
            followedBytes = current->size();
            scrollToBottom = true;
        }
    }

    static auto callback(ImGuiInputTextCallbackData* data) -> int {
        auto* self = static_cast<TerminalWindow*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            self->complete(data);
        } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            self->recall(data, data->EventKey == ImGuiKey_UpArrow ? -1 : +1);
        }
        return 0;
    }

    auto complete(ImGuiInputTextCallbackData* data) -> void {
        const std::string_view text{data->Buf, static_cast<std::size_t>(data->BufTextLen)};
        const auto candidates = shell->complete(text);
        if (candidates.empty()) {
            return;
        }
        const auto lastSpace = text.rfind(' ');
        const int wordStart =
            lastSpace == std::string_view::npos ? 0 : static_cast<int>(lastSpace) + 1;
        std::string replacement = candidates[0];
        if (candidates.size() > 1) {
            // Longest common prefix, and show the options.
            for (const auto& c : candidates) {
                const auto n = std::min(replacement.size(), c.size());
                std::size_t k = 0;
                while (k < n && replacement[k] == c[k]) {
                    ++k;
                }
                replacement.resize(k);
            }
            std::string shown;
            for (const auto& c : candidates) {
                shown += c + "  ";
            }
            append(shell->prompt() + std::string{text});
            append(shown);
            scrollToBottom = true;
        } else if (!replacement.ends_with('/')) {
            replacement += ' ';
        }
        data->DeleteChars(wordStart, data->BufTextLen - wordStart);
        data->InsertChars(data->CursorPos, replacement.c_str());
    }

    auto recall(ImGuiInputTextCallbackData* data, int direction) -> void {
        const auto& hist = shell->history();
        if (hist.empty()) {
            return;
        }
        const int count = static_cast<int>(hist.size());
        if (historyPos == -1) {
            if (direction > 0) {
                return;
            }
            saved = std::string{data->Buf, static_cast<std::size_t>(data->BufTextLen)};
            historyPos = count - 1;
        } else {
            historyPos += direction;
        }
        std::string text;
        if (historyPos < 0) {
            historyPos = 0;
            text = hist[0];
        } else if (historyPos >= count) {
            historyPos = -1;
            text = saved;
        } else {
            text = hist[static_cast<std::size_t>(historyPos)];
        }
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, text.c_str());
    }

    Shell* shell;
    TerminalConfig cfg;
    std::deque<std::string> lines;
    std::array<char, 512> input{};
    int historyPos = -1;
    std::string saved;
    std::optional<std::string> following;
    std::size_t followedBytes = 0;
    bool scrollToBottom = true;
    bool focusInput = true;
};

} // namespace roboslop
