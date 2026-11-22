#pragma once

#include <GLFW/glfw3.h>

namespace osupoint::gui {

class MainWindow;

class SystemTray {
public:
    SystemTray(GLFWwindow* glfw_window, MainWindow* main_window);
    ~SystemTray();

    bool Init();
    void PollEvents();
    void WaitEventsTimeout(int timeout_ms);
    void RestoreMainWindow();
    void HideMainWindow();
    void ToggleMainWindow();

    bool IsSupported() const noexcept { return m_supported; }
    bool IsMenuVisible() const noexcept { return m_menu_visible; }
    MainWindow* GetMainWindow() const noexcept { return m_main_window; }
    GLFWwindow* GetGlfwWindow() const noexcept { return m_glfw_window; }

    void HandleSniActivate(int x, int y);
    void HandleSniContextMenu(int x, int y);

    void ShowContextMenu(int root_x, int root_y);
    void RedrawMenu();
    void HideContextMenu();
    void NotifyMenuUpdated();

private:
    void RedrawTrayIcon(int width, int height);

    GLFWwindow* m_glfw_window = nullptr;
    MainWindow* m_main_window = nullptr;

    bool m_supported = false;
    void* m_bus = nullptr;          // sd_bus*
    void* m_sni_slot = nullptr;     // sd_bus_slot* (org.kde.StatusNotifierItem)
    void* m_sni_slot_fd = nullptr;  // sd_bus_slot* (org.freedesktop.StatusNotifierItem)
    void* m_menu_slot = nullptr;    // sd_bus_slot* for DBusMenu
    uint32_t m_menu_revision = 1;
    void* m_dpy = nullptr;          // Display*
    unsigned long m_tray_owner = 0; // Window
    unsigned long m_tray_win = 0;   // Window
    unsigned long m_menu_win = 0;   // Window
    void* m_gc_tray = nullptr;       // GC
    void* m_gc_menu = nullptr;       // GC
    void* m_font = nullptr;          // XFontStruct*
    void* m_ximg_24 = nullptr;       // XImage*
    void* m_ximg_32 = nullptr;       // XImage*
    void* m_img_data_24 = nullptr;
    void* m_img_data_32 = nullptr;

    int m_tray_width = 24;
    int m_tray_height = 24;
    bool m_menu_visible = false;
    int m_menu_hover = -1;
    int m_menu_x = 0;
    int m_menu_y = 0;
    int m_menu_w = 160;
    int m_menu_h = 75;
};

} // namespace osupoint::gui
