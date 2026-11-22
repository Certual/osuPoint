#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>
#include <unistd.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "ui_window.h"
#include "app_icon.h"
#include "system_tray.h"
#include "osupoint/driver_service.h"

static void GlfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[GLFW Error %d]: %s\n", error, description);
}

static void PrintHelp(const char* prog) {
    std::printf("osuPoint - Linux Ultra-Low Latency Tablet Driver & Configuration GUI\n");
    std::printf("Usage: %s [OPTIONS]\n\n", prog);
    std::printf("Options:\n");
    std::printf("  -d, --daemon, --headless   Run driver daemon in headless mode (no GUI)\n");
    std::printf("  --gui                      Force GUI mode (default when display is available)\n");
    std::printf("  --config <path>            Specify configuration file (default: config.ini)\n");
    std::printf("  --mouse                    Emulate Mouse input [default]\n");
    std::printf("  --stylus                   Emulate Stylus/Tablet input\n");
    std::printf("  --spin <percent>           Latency spin-wait percentage (0-100, default: 0)\n");
    std::printf("  --core-reader <core>       Pin raw reader thread to CPU core (default: 2)\n");
    std::printf("  --core-pipeline <core>     Pin transformation pipeline to CPU core (default: 3)\n");
    std::printf("  --version, -v              Show version information\n");
    std::printf("  --help, -h                 Show this help message\n");
}

int main(int argc, char* argv[]) {
    std::string config_path = "config.ini";
    bool run_daemon = false;
    bool force_gui = false;
    bool mouse_mode = true;
    int spin_percent = 0;
    int core_reader = -1;
    int core_pipeline = -1;

    std::string prog_name = argv[0];
    if (prog_name.find("osupointd") != std::string::npos) {
        run_daemon = true;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintHelp(argv[0]);
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("osuPoint v0.6.1-linux (Unified Real-Time Driver & GUI)\n");
            return 0;
        } else if (arg == "--daemon" || arg == "-d" || arg == "--headless") {
            run_daemon = true;
        } else if (arg == "--gui") {
            force_gui = true;
            run_daemon = false;
        } else if (arg == "--mouse") {
            mouse_mode = true;
        } else if (arg == "--stylus") {
            mouse_mode = false;
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--spin" && i + 1 < argc) {
            spin_percent = std::clamp(std::atoi(argv[++i]), 0, 100);
        } else if (arg == "--core-reader" && i + 1 < argc) {
            core_reader = std::atoi(argv[++i]);
        } else if (arg == "--core-pipeline" && i + 1 < argc) {
            core_pipeline = std::atoi(argv[++i]);
        }
    }

    const char* disp = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if (!force_gui && (!disp && !wayland)) {
        run_daemon = true;
    }

    if (run_daemon) {
        std::printf("[osupoint] Starting unified driver in headless daemon mode...\n");
        struct sigaction sa{};
        sa.sa_handler = [](int) { osupoint::DriverService::Instance().Stop(); };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);

        if (!osupoint::DriverService::Instance().Start(config_path, mouse_mode, spin_percent, core_reader, core_pipeline)) {
            std::fprintf(stderr, "[osupoint] Failed to start driver service.\n");
            return 1;
        }

        std::printf("[osupoint] Driver service active. Press Ctrl+C to terminate.\n");
        while (osupoint::DriverService::Instance().IsRunning()) {
            pause();
        }
        std::printf("[osupoint] Driver service terminated cleanly.\n");
        return 0;
    }

    // GUI Mode: Driver pipeline and GUI run together in the same process
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW (check DISPLAY or WAYLAND_DISPLAY)\n");
        return 1;
    }

    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "osupoint");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "osupoint");

    // Exact Upstream Base Dimensions: 640x330
    GLFWwindow* window = glfwCreateWindow(640, 330, "osu!Point", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window (check DISPLAY or WAYLAND_DISPLAY)\n");
        glfwTerminate();
        return 1;
    }

    // Set authentic application icon
    osupoint::gui::SetAppWindowIcon(window);

    // Minimize / close to system tray
    glfwSetWindowIconifyCallback(window, [](GLFWwindow* w, int iconified) {
        if (iconified) {
            glfwHideWindow(w);
        }
    });

    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        glfwSetWindowShouldClose(w, GLFW_FALSE);
        glfwHideWindow(w);
    });

    glfwSetWindowSizeLimits(window, 640, 330, GLFW_DONT_CARE, GLFW_DONT_CARE);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable V-Sync

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load clean font
    const char* font_candidates[] = {
        "/home/certual/.fonts/Roboto-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf"
    };
    for (const char* fpath : font_candidates) {
        FILE* f = std::fopen(fpath, "rb");
        if (f) {
            std::fclose(f);
            io.Fonts->AddFontFromFileTTF(fpath, 14.5f);
            break;
        }
    }

    // Platform / Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    ImGuiContext* main_ctx = ImGui::GetCurrentContext();

    {
        // MainWindow initializes and owns DriverService so driver runs inside GUI
        osupoint::gui::MainWindow mainWindow(config_path, window);
        glfwSetWindowUserPointer(window, &mainWindow);
        glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
            auto* win = static_cast<osupoint::gui::MainWindow*>(glfwGetWindowUserPointer(w));
            if (win && count > 0 && paths && paths[0]) {
                std::string p = paths[0];
                std::string ext = "";
                size_t dot = p.find_last_of('.');
                if (dot != std::string::npos) {
                    ext = p.substr(dot);
                    for (char& c : ext) c = std::tolower(c);
                }
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") {
                    win->SetBackgroundImage(p);
                }
            }
        });
        osupoint::gui::SystemTray tray(window, &mainWindow);
        tray.Init();

        GLFWwindow* manual_win = nullptr;
        ImGuiContext* manual_ctx = nullptr;
        GLFWwindow* debug_win = nullptr;
        ImGuiContext* debug_ctx = nullptr;

        auto DestroySecondaryWindow = [&](GLFWwindow*& win, ImGuiContext*& ctx) {
            if (!win) return;
            glfwMakeContextCurrent(win);
            ImGui::SetCurrentContext(ctx);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(ctx);
            glfwDestroyWindow(win);
            win = nullptr;
            ctx = nullptr;
            glfwMakeContextCurrent(window);
            ImGui::SetCurrentContext(main_ctx);
        };

        std::string last_title = "";

        while (!glfwWindowShouldClose(window) && !mainWindow.ShouldClose()) {
            tray.PollEvents();

            // Manage Manual Secondary Window lifecycle
            if (mainWindow.IsManualOpen()) {
                if (!manual_win) {
                    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
                    manual_win = glfwCreateWindow(560, 600, "osu!Point Manual", nullptr, window);
                    if (manual_win) {
                        osupoint::gui::SetAppWindowIcon(manual_win);
                        glfwSetWindowCloseCallback(manual_win, [](GLFWwindow* w) {
                            glfwSetWindowShouldClose(w, GLFW_TRUE);
                        });
                        manual_ctx = ImGui::CreateContext(io.Fonts);
                        ImGui::SetCurrentContext(manual_ctx);
                        ImGui_ImplGlfw_InitForOpenGL(manual_win, true);
                        ImGui_ImplOpenGL3_Init("#version 130");
                        glfwMakeContextCurrent(window);
                        ImGui::SetCurrentContext(main_ctx);
                    }
                }
            }
            if (manual_win) {
                if (glfwWindowShouldClose(manual_win) || !mainWindow.IsManualOpen()) {
                    mainWindow.SetShowManual(false);
                    DestroySecondaryWindow(manual_win, manual_ctx);
                }
            }

            // Manage Debug Secondary Window lifecycle
            if (mainWindow.IsDebugOpen()) {
                if (!debug_win) {
                    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
                    debug_win = glfwCreateWindow(780, 760, "osu!Point Debug - Telemetry & Diagnostics", nullptr, window);
                    if (debug_win) {
                        osupoint::gui::SetAppWindowIcon(debug_win);
                        glfwSetWindowCloseCallback(debug_win, [](GLFWwindow* w) {
                            glfwSetWindowShouldClose(w, GLFW_TRUE);
                        });
                        debug_ctx = ImGui::CreateContext(io.Fonts);
                        ImGui::SetCurrentContext(debug_ctx);
                        ImGui_ImplGlfw_InitForOpenGL(debug_win, true);
                        ImGui_ImplOpenGL3_Init("#version 130");
                        glfwMakeContextCurrent(window);
                        ImGui::SetCurrentContext(main_ctx);
                    }
                }
            }
            if (debug_win) {
                if (glfwWindowShouldClose(debug_win) || !mainWindow.IsDebugOpen()) {
                    mainWindow.SetDebugOpen(false);
                    DestroySecondaryWindow(debug_win, debug_ctx);
                }
            }

            bool is_main_visible = (glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0) &&
                                   (glfwGetWindowAttrib(window, GLFW_ICONIFIED) == 0);
            bool has_active_windows = is_main_visible || (manual_win != nullptr) || (debug_win != nullptr);

            if (!has_active_windows) {
                tray.WaitEventsTimeout(50);
                glfwPollEvents();
                continue;
            }

            if (mainWindow.NeedsContinuousRedraw() || debug_win != nullptr) {
                glfwWaitEventsTimeout(0.03);
            } else {
                glfwWaitEventsTimeout(0.08);
            }

            tray.PollEvents();

            // Render Main Window if visible
            if (is_main_visible) {
                glfwMakeContextCurrent(window);
                ImGui::SetCurrentContext(main_ctx);

                std::string current_title = mainWindow.GetWindowTitle();
                if (current_title != last_title) {
                    glfwSetWindowTitle(window, current_title.c_str());
                    last_title = current_title;
                }

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                mainWindow.Render();
                mainWindow.ClearDirty();

                ImGui::Render();
                int display_w, display_h;
                glfwGetFramebufferSize(window, &display_w, &display_h);
                glViewport(0, 0, display_w, display_h);
                glClearColor(14.0f / 255.0f, 14.0f / 255.0f, 17.0f / 255.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(window);
            }

            // Render Manual Window if open
            if (manual_win && !glfwWindowShouldClose(manual_win)) {
                glfwMakeContextCurrent(manual_win);
                ImGui::SetCurrentContext(manual_ctx);

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                mainWindow.RenderManualWindowContent();

                ImGui::Render();
                int mw, mh;
                glfwGetFramebufferSize(manual_win, &mw, &mh);
                glViewport(0, 0, mw, mh);
                glClearColor(18.0f / 255.0f, 18.0f / 255.0f, 24.0f / 255.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(manual_win);
                glfwMakeContextCurrent(window);
                ImGui::SetCurrentContext(main_ctx);
            }

            // Render Debug Window if open
            if (debug_win && !glfwWindowShouldClose(debug_win)) {
                glfwMakeContextCurrent(debug_win);
                ImGui::SetCurrentContext(debug_ctx);

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                mainWindow.RenderDebugWindowContent();

                ImGui::Render();
                int dw, dh;
                glfwGetFramebufferSize(debug_win, &dw, &dh);
                glViewport(0, 0, dw, dh);
                glClearColor(16.0f / 255.0f, 16.0f / 255.0f, 22.0f / 255.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(debug_win);
                glfwMakeContextCurrent(window);
                ImGui::SetCurrentContext(main_ctx);
            }
        }

        // Cleanly destroy any open secondary windows
        DestroySecondaryWindow(manual_win, manual_ctx);
        DestroySecondaryWindow(debug_win, debug_ctx);

        std::printf("[osupoint-debug] Loop exited: glfwWindowShouldClose=%d, mainWindow.ShouldClose=%d\n",
                    glfwWindowShouldClose(window), mainWindow.ShouldClose() ? 1 : 0);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
