#include "system_tray.h"
#include "ui_window.h"
#include "app_icon.h"

#include <systemd/sd-bus.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

namespace osupoint::gui {

static int sni_method_activate(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    auto* tray = static_cast<SystemTray*>(userdata);
    int x = 0, y = 0;
    sd_bus_message_read(m, "ii", &x, &y);
    if (tray) {
        tray->HandleSniActivate(x, y);
    }
    return sd_bus_reply_method_return(m, "");
}

static int sni_method_context_menu(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    auto* tray = static_cast<SystemTray*>(userdata);
    int x = 0, y = 0;
    sd_bus_message_read(m, "ii", &x, &y);
    if (tray) {
        tray->HandleSniContextMenu(x, y);
    }
    return sd_bus_reply_method_return(m, "");
}

static int sni_method_scroll(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata; (void)ret_error;
    return sd_bus_reply_method_return(m, "");
}

static int sni_property_get(sd_bus *bus, const char *path, const char *interface,
                            const char *property, sd_bus_message *reply, void *userdata,
                            sd_bus_error *ret_error) {
    (void)bus; (void)path; (void)interface; (void)userdata; (void)ret_error;
    if (std::strcmp(property, "Category") == 0) {
        return sd_bus_message_append(reply, "s", "ApplicationStatus");
    }
    if (std::strcmp(property, "Id") == 0) {
        return sd_bus_message_append(reply, "s", "osupoint");
    }
    if (std::strcmp(property, "Title") == 0) {
        return sd_bus_message_append(reply, "s", "osuPoint");
    }
    if (std::strcmp(property, "Status") == 0) {
        return sd_bus_message_append(reply, "s", "Active");
    }
    if (std::strcmp(property, "IconName") == 0) {
        return sd_bus_message_append(reply, "s", "osupoint");
    }
    if (std::strcmp(property, "ItemIsMenu") == 0) {
        return sd_bus_message_append(reply, "b", 0);
    }
    if (std::strcmp(property, "Menu") == 0) {
        return sd_bus_message_append(reply, "o", "/MenuBar");
    }
    return 0;
}

static int append_dbusmenu_item(sd_bus_message* reply, int id, const char* label, const char* type = nullptr,
                                const char* toggle_type = nullptr, int toggle_state = 0) {
    int r = sd_bus_message_open_container(reply, 'v', "(ia{sv}av)");
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'r', "ia{sv}av");
    if (r < 0) return r;

    sd_bus_message_append(reply, "i", id);

    // a{sv}
    sd_bus_message_open_container(reply, 'a', "{sv}");
    if (label) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "label");
        sd_bus_message_open_container(reply, 'v', "s");
        sd_bus_message_append(reply, "s", label);
        sd_bus_message_close_container(reply); // v
        sd_bus_message_close_container(reply); // e
    }
    if (type) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "type");
        sd_bus_message_open_container(reply, 'v', "s");
        sd_bus_message_append(reply, "s", type);
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }
    if (toggle_type) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "toggle-type");
        sd_bus_message_open_container(reply, 'v', "s");
        sd_bus_message_append(reply, "s", toggle_type);
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);

        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "toggle-state");
        sd_bus_message_open_container(reply, 'v', "i");
        sd_bus_message_append(reply, "i", toggle_state);
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }
    // "enabled"
    sd_bus_message_open_container(reply, 'e', "sv");
    sd_bus_message_append(reply, "s", "enabled");
    sd_bus_message_open_container(reply, 'v', "b");
    sd_bus_message_append(reply, "b", 1);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    sd_bus_message_close_container(reply); // a{sv}

    // av (empty child array)
    sd_bus_message_open_container(reply, 'a', "v");
    sd_bus_message_close_container(reply); // a

    sd_bus_message_close_container(reply); // r
    return sd_bus_message_close_container(reply); // v
}

static int dbusmenu_get_layout(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    auto* tray = static_cast<SystemTray*>(userdata);
    int32_t parent_id = 0, depth = 0;
    sd_bus_message_read(m, "ii", &parent_id, &depth);

    sd_bus_message *reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    // Output: u(ia{sv}av)
    sd_bus_message_append(reply, "u", 1); // revision

    // Root struct (ia{sv}av)
    sd_bus_message_open_container(reply, 'r', "ia{sv}av");
    sd_bus_message_append(reply, "i", 0); // root id = 0

    // root a{sv} (children-display = submenu)
    sd_bus_message_open_container(reply, 'a', "{sv}");
    sd_bus_message_open_container(reply, 'e', "sv");
    sd_bus_message_append(reply, "s", "children-display");
    sd_bus_message_open_container(reply, 'v', "s");
    sd_bus_message_append(reply, "s", "submenu");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    // root av (children)
    sd_bus_message_open_container(reply, 'a', "v");
    append_dbusmenu_item(reply, 1, "Open osu!Point");
    bool pen_click = true;
    if (tray && tray->GetMainWindow()) {
        pen_click = tray->GetMainWindow()->GetPenClick();
    }
    append_dbusmenu_item(reply, 2, "Pen Click", nullptr, "checkmark", pen_click ? 1 : 0);
    append_dbusmenu_item(reply, 3, nullptr, "separator");
    append_dbusmenu_item(reply, 4, "Exit");
    sd_bus_message_close_container(reply); // av

    sd_bus_message_close_container(reply); // r

    r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
}

static void execute_menu_action(SystemTray* tray, int32_t id) {
    if (!tray) return;
    if (id == 1) {
        tray->RestoreMainWindow();
    } else if (id == 2) {
        if (tray->GetMainWindow()) {
            tray->GetMainWindow()->TogglePenClick();
            tray->NotifyMenuUpdated();
        }
    } else if (id == 4) {
        if (tray->GetMainWindow()) {
            tray->GetMainWindow()->RequestClose();
        }
        if (tray->GetGlfwWindow()) {
            glfwSetWindowShouldClose(tray->GetGlfwWindow(), GLFW_TRUE);
        }
    }
}

static int dbusmenu_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    auto* tray = static_cast<SystemTray*>(userdata);
    int32_t id = 0;
    const char* event_id = nullptr;
    int r = sd_bus_message_read(m, "is", &id, &event_id);
    if (r >= 0) {
        (void)sd_bus_message_skip(m, "v");
        (void)sd_bus_message_skip(m, "u");
        execute_menu_action(tray, id);
    }
    return sd_bus_reply_method_return(m, "");
}

static int dbusmenu_event_group(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    auto* tray = static_cast<SystemTray*>(userdata);
    int r = sd_bus_message_enter_container(m, 'a', "(isvu)");
    if (r >= 0) {
        while ((r = sd_bus_message_enter_container(m, 'r', "isvu")) > 0) {
            int32_t id = 0;
            const char* event_id = nullptr;
            if (sd_bus_message_read(m, "is", &id, &event_id) >= 0) {
                (void)sd_bus_message_skip(m, "v");
                (void)sd_bus_message_skip(m, "u");
                execute_menu_action(tray, id);
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }

    sd_bus_message *reply = nullptr;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "i");
    sd_bus_message_close_container(reply);

    r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
}

static int dbusmenu_get_group_properties(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    auto* tray = static_cast<SystemTray*>(userdata);
    bool pen_click = true;
    if (tray && tray->GetMainWindow()) {
        pen_click = tray->GetMainWindow()->GetPenClick();
    }

    int r = sd_bus_message_enter_container(m, 'a', "i");
    if (r < 0) return r;

    std::vector<int32_t> ids;
    int32_t req_id = 0;
    while (sd_bus_message_read(m, "i", &req_id) > 0) {
        ids.push_back(req_id);
    }
    sd_bus_message_exit_container(m);

    // Skip requested properties filter if present
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r >= 0) {
        sd_bus_message_exit_container(m);
    }

    sd_bus_message *reply = nullptr;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    // Output signature: a(ia{sv})
    sd_bus_message_open_container(reply, 'a', "(ia{sv})");

    for (int32_t id : ids) {
        sd_bus_message_open_container(reply, 'r', "ia{sv}");
        sd_bus_message_append(reply, "i", id);
        sd_bus_message_open_container(reply, 'a', "{sv}");

        if (id == 0) {
            // Root item
            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "children-display");
            sd_bus_message_open_container(reply, 'v', "s");
            sd_bus_message_append(reply, "s", "submenu");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        } else if (id == 1) {
            // Open osu!Point
            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "label");
            sd_bus_message_open_container(reply, 'v', "s");
            sd_bus_message_append(reply, "s", "Open osu!Point");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);

            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "enabled");
            sd_bus_message_open_container(reply, 'v', "b");
            sd_bus_message_append(reply, "b", 1);
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        } else if (id == 2) {
            // Pen Click (checkable)
            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "label");
            sd_bus_message_open_container(reply, 'v', "s");
            sd_bus_message_append(reply, "s", "Pen Click");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);

            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "toggle-type");
            sd_bus_message_open_container(reply, 'v', "s");
            sd_bus_message_append(reply, "s", "checkmark");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);

            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "toggle-state");
            sd_bus_message_open_container(reply, 'v', "i");
            sd_bus_message_append(reply, "i", pen_click ? 1 : 0);
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);

            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "enabled");
            sd_bus_message_open_container(reply, 'v', "b");
            sd_bus_message_append(reply, "b", 1);
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        } else if (id == 3) {
            // Separator
            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "type");
            sd_bus_message_open_container(reply, 'v', "s");
            sd_bus_message_append(reply, "s", "separator");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        } else if (id == 4) {
            // Exit
            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "label");
            sd_bus_message_open_container(reply, 'v', "s");
            sd_bus_message_append(reply, "s", "Exit");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);

            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "enabled");
            sd_bus_message_open_container(reply, 'v', "b");
            sd_bus_message_append(reply, "b", 1);
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        }

        sd_bus_message_close_container(reply); // a{sv}
        sd_bus_message_close_container(reply); // r
    }

    sd_bus_message_close_container(reply); // a
    r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
}

static int dbusmenu_about_to_show(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata; (void)ret_error;
    return sd_bus_reply_method_return(m, "b", 0);
}

static int dbusmenu_property_get(sd_bus *bus, const char *path, const char *interface,
                                 const char *property, sd_bus_message *reply, void *userdata,
                                 sd_bus_error *ret_error) {
    (void)bus; (void)path; (void)interface; (void)userdata; (void)ret_error;
    if (std::strcmp(property, "Version") == 0) {
        return sd_bus_message_append(reply, "u", 3);
    }
    if (std::strcmp(property, "Status") == 0) {
        return sd_bus_message_append(reply, "s", "normal");
    }
    return 0;
}

static const sd_bus_vtable s_dbusmenu_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)", dbusmenu_get_layout, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetGroupProperties", "aias", "a(ia{sv})", dbusmenu_get_group_properties, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AboutToShow", "i", "b", dbusmenu_about_to_show, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Event", "isvu", "", dbusmenu_event, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("EventGroup", "a(isvu)", "ai", dbusmenu_event_group, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Version", "u", dbusmenu_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", dbusmenu_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("LayoutUpdated", "ui", 0),
    SD_BUS_VTABLE_END
};

static int sni_get_icon_pixmap(sd_bus *bus, const char *path, const char *interface,
                               const char *property, sd_bus_message *reply, void *userdata,
                               sd_bus_error *ret_error) {
    (void)bus; (void)path; (void)interface; (void)property; (void)userdata; (void)ret_error;
    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) return r;

    // 32x32 ARGB pixmap in network byte order
    r = sd_bus_message_open_container(reply, 'r', "iiay");
    if (r < 0) return r;

    sd_bus_message_append(reply, "ii", 32, 32);

    const unsigned char* rgba = GetIcon32Rgba();
    unsigned char argb[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; ++i) {
        argb[i * 4 + 0] = rgba[i * 4 + 3]; // Alpha
        argb[i * 4 + 1] = rgba[i * 4 + 0]; // Red
        argb[i * 4 + 2] = rgba[i * 4 + 1]; // Green
        argb[i * 4 + 3] = rgba[i * 4 + 2]; // Blue
    }

    sd_bus_message_append_array(reply, 'y', argb, sizeof(argb));
    sd_bus_message_close_container(reply);
    return sd_bus_message_close_container(reply);
}

static const sd_bus_vtable s_sni_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", sni_method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", sni_method_context_menu, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", sni_method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", "", sni_method_scroll, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_PROPERTY("Category", "s", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id", "s", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconName", "s", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", sni_get_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ItemIsMenu", "b", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Menu", "o", sni_property_get, 0, SD_BUS_VTABLE_PROPERTY_CONST),

    SD_BUS_SIGNAL("NewTitle", "", 0),
    SD_BUS_SIGNAL("NewIcon", "", 0),
    SD_BUS_SIGNAL("NewAttentionIcon", "", 0),
    SD_BUS_SIGNAL("NewOverlayIcon", "", 0),
    SD_BUS_SIGNAL("NewMenu", "", 0),
    SD_BUS_SIGNAL("NewToolTip", "", 0),
    SD_BUS_SIGNAL("NewStatus", "s", 0),
    SD_BUS_VTABLE_END
};

SystemTray::SystemTray(GLFWwindow* glfw_window, MainWindow* main_window)
    : m_glfw_window(glfw_window), m_main_window(main_window) {}

SystemTray::~SystemTray() {
    if (m_menu_slot) {
        sd_bus_slot_unref(static_cast<sd_bus_slot*>(m_menu_slot));
        m_menu_slot = nullptr;
    }
    if (m_sni_slot) {
        sd_bus_slot_unref(static_cast<sd_bus_slot*>(m_sni_slot));
        m_sni_slot = nullptr;
    }
    if (m_sni_slot_fd) {
        sd_bus_slot_unref(static_cast<sd_bus_slot*>(m_sni_slot_fd));
        m_sni_slot_fd = nullptr;
    }
    if (m_bus) {
        sd_bus_unref(static_cast<sd_bus*>(m_bus));
        m_bus = nullptr;
    }

    if (!m_dpy) return;
    Display* d = reinterpret_cast<Display*>(m_dpy);

    if (m_menu_visible) {
        HideContextMenu();
    }

    if (m_menu_win) {
        XDestroyWindow(d, static_cast<Window>(m_menu_win));
        m_menu_win = 0;
    }

    if (m_gc_menu) {
        XFreeGC(d, reinterpret_cast<GC>(m_gc_menu));
        m_gc_menu = nullptr;
    }

    if (m_font) {
        XFreeFont(d, reinterpret_cast<XFontStruct*>(m_font));
        m_font = nullptr;
    }

    if (m_ximg_24) {
        XDestroyImage(reinterpret_cast<XImage*>(m_ximg_24));
        m_ximg_24 = nullptr;
        m_img_data_24 = nullptr;
    }

    if (m_ximg_32) {
        XDestroyImage(reinterpret_cast<XImage*>(m_ximg_32));
        m_ximg_32 = nullptr;
        m_img_data_32 = nullptr;
    }

    if (m_tray_win) {
        XDestroyWindow(d, static_cast<Window>(m_tray_win));
        m_tray_win = 0;
    }

    if (m_gc_tray) {
        XFreeGC(d, reinterpret_cast<GC>(m_gc_tray));
        m_gc_tray = nullptr;
    }

    XCloseDisplay(d);
    m_dpy = nullptr;
}

bool SystemTray::Init() {
    // 1. Initialize modern StatusNotifierItem via DBus
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) >= 0) {
        m_bus = bus;
        char service_name_kde[64];
        char service_name_fd[64];
        std::snprintf(service_name_kde, sizeof(service_name_kde), "org.kde.StatusNotifierItem-%d-1", getpid());
        std::snprintf(service_name_fd, sizeof(service_name_fd), "org.freedesktop.StatusNotifierItem-%d-1", getpid());
        sd_bus_request_name(bus, service_name_kde, 0);
        sd_bus_request_name(bus, service_name_fd, 0);

        sd_bus_slot* slot_kde = nullptr;
        if (sd_bus_add_object_vtable(bus, &slot_kde, "/StatusNotifierItem",
                                     "org.kde.StatusNotifierItem", s_sni_vtable, this) >= 0) {
            m_sni_slot = slot_kde;
        }

        sd_bus_slot* slot_fd = nullptr;
        if (sd_bus_add_object_vtable(bus, &slot_fd, "/StatusNotifierItem",
                                     "org.freedesktop.StatusNotifierItem", s_sni_vtable, this) >= 0) {
            m_sni_slot_fd = slot_fd;
        }

        sd_bus_slot* menu_slot = nullptr;
        if (sd_bus_add_object_vtable(bus, &menu_slot, "/MenuBar",
                                     "com.canonical.dbusmenu", s_dbusmenu_vtable, this) >= 0) {
            m_menu_slot = menu_slot;
        }

        // Register with StatusNotifierWatcher
        sd_bus_message* m = nullptr;
        if (sd_bus_message_new_method_call(bus, &m, "org.kde.StatusNotifierWatcher",
                                           "/StatusNotifierWatcher",
                                           "org.kde.StatusNotifierWatcher",
                                           "RegisterStatusNotifierItem") >= 0) {
            sd_bus_message_append(m, "s", "/StatusNotifierItem");
            sd_bus_call(bus, m, 0, nullptr, nullptr);
            sd_bus_message_unref(m);
        }

        // Emit initial notification signals for watcher/host
        sd_bus_emit_signal(bus, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewMenu", "");
        sd_bus_emit_signal(bus, "/StatusNotifierItem", "org.freedesktop.StatusNotifierItem", "NewMenu", "");
        sd_bus_emit_signal(bus, "/MenuBar", "com.canonical.dbusmenu", "LayoutUpdated", "ui", 1, 0);

        m_supported = true;
    }

    // 2. Initialize X11 connection for popup menu and legacy XEmbed fallback
    Display* d = XOpenDisplay(nullptr);
    if (!d) return m_supported;
    m_dpy = d;

    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);

    // Create override-redirect context menu window
    XSetWindowAttributes mswa{};
    mswa.override_redirect = True;
    mswa.background_pixel = 0x1C1C22;
    mswa.border_pixel = 0x3E3E4E;
    mswa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | EnterWindowMask | LeaveWindowMask;

    Window menu_win = XCreateWindow(d, root, 0, 0, m_menu_w, m_menu_h, 1,
                                    DefaultDepth(d, screen), InputOutput, DefaultVisual(d, screen),
                                    CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &mswa);
    m_menu_win = menu_win;

    Atom pid_atom = XInternAtom(d, "_NET_WM_PID", False);
    unsigned long pid = getpid();
    XChangeProperty(d, menu_win, pid_atom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&pid), 1);
    m_gc_menu = XCreateGC(d, menu_win, 0, nullptr);

    const char* font_names[] = {
        "-*-dejavu sans-medium-r-*-*-12-*-*-*-*-*-*-*",
        "-misc-fixed-medium-r-normal--14-*-*-*-c-*-iso8859-1",
        "6x13",
        "fixed"
    };
    for (const char* fn : font_names) {
        XFontStruct* fs = XLoadQueryFont(d, fn);
        if (fs) {
            m_font = fs;
            XSetFont(d, reinterpret_cast<GC>(m_gc_menu), fs->fid);
            break;
        }
    }

    // 3. Setup XEmbed legacy docking ONLY as fallback when modern DBus SNI failed
    if (!m_sni_slot && !m_sni_slot_fd) {
        char tray_name[32];
    std::snprintf(tray_name, sizeof(tray_name), "_NET_SYSTEM_TRAY_S%d", screen);
    Atom tray_atom = XInternAtom(d, tray_name, False);
    Window tray_owner = XGetSelectionOwner(d, tray_atom);
    if (tray_owner != None) {
        m_tray_owner = tray_owner;

        Atom visual_atom = XInternAtom(d, "_NET_SYSTEM_TRAY_VISUAL", False);
        Atom actual_type;
        int actual_format;
        unsigned long nitems = 0, bytes_after = 0;
        unsigned char* prop = nullptr;
        Visual* visual = DefaultVisual(d, screen);
        int depth = DefaultDepth(d, screen);
        Colormap colormap = DefaultColormap(d, screen);

        if (XGetWindowProperty(d, tray_owner, visual_atom, 0, 1, False, AnyPropertyType,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop && nitems > 0) {
            VisualID vid = *reinterpret_cast<VisualID*>(prop);
            XVisualInfo vtemplate{};
            vtemplate.visualid = vid;
            int nvinfo = 0;
            XVisualInfo* vinfo = XGetVisualInfo(d, VisualIDMask, &vtemplate, &nvinfo);
            if (vinfo && nvinfo > 0) {
                visual = vinfo->visual;
                depth = vinfo->depth;
                colormap = XCreateColormap(d, root, visual, AllocNone);
                XFree(vinfo);
            }
            XFree(prop);
        }

        XSetWindowAttributes swa{};
        swa.colormap = colormap;
        swa.background_pixel = 0;
        swa.border_pixel = 0;
        swa.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask;

        Window tray_win = XCreateWindow(d, root, 0, 0, 24, 24, 0,
                                        depth, InputOutput, visual,
                                        CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &swa);
        if (tray_win) {
            m_tray_win = tray_win;

            XClassHint ch;
            char res_name[] = "osupoint";
            char res_class[] = "osuPoint";
            ch.res_name = res_name;
            ch.res_class = res_class;
            XSetClassHint(d, tray_win, &ch);
            XStoreName(d, tray_win, "osuPoint");

            Atom xembed_atom = XInternAtom(d, "_XEMBED_INFO", False);
            unsigned long xembed_info[2] = { 1, 1 }; // [version=1, XEMBED_MAPPED=1]
            XChangeProperty(d, tray_win, xembed_atom, xembed_atom, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*>(xembed_info), 2);
            XChangeProperty(d, tray_win, pid_atom, XA_CARDINAL, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*>(&pid), 1);

            XSizeHints hints{};
            hints.flags = PMinSize | PBaseSize;
            hints.min_width = 22;
            hints.min_height = 22;
            hints.base_width = 22;
            hints.base_height = 22;
            XSetWMNormalHints(d, tray_win, &hints);

            XMapWindow(d, tray_win);

            Atom opcode_atom = XInternAtom(d, "_NET_SYSTEM_TRAY_OPCODE", False);
            XClientMessageEvent ev{};
            ev.type = ClientMessage;
            ev.window = tray_owner;
            ev.message_type = opcode_atom;
            ev.format = 32;
            ev.data.l[0] = CurrentTime;
            ev.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
            ev.data.l[2] = tray_win;
            ev.data.l[3] = 0;
            ev.data.l[4] = 0;
            XSendEvent(d, tray_owner, False, NoEventMask, reinterpret_cast<XEvent*>(&ev));
            XSync(d, False);

            m_gc_tray = XCreateGC(d, tray_win, 0, nullptr);

            const unsigned char* bgra24 = GetIcon24Bgra();
            m_img_data_24 = std::malloc(24 * 24 * 4);
            std::memcpy(m_img_data_24, bgra24, 24 * 24 * 4);
            m_ximg_24 = XCreateImage(d, visual, depth, ZPixmap, 0,
                                     reinterpret_cast<char*>(m_img_data_24), 24, 24, 32, 24 * 4);

            const unsigned char* bgra32 = GetIcon32Bgra();
            m_img_data_32 = std::malloc(32 * 32 * 4);
            std::memcpy(m_img_data_32, bgra32, 32 * 32 * 4);
            m_ximg_32 = XCreateImage(d, visual, depth, ZPixmap, 0,
                                     reinterpret_cast<char*>(m_img_data_32), 32, 32, 32, 32 * 4);
        }
    }
    }

    m_supported = true;
    return true;
}

void SystemTray::HandleSniActivate(int, int) {
    ToggleMainWindow();
}

void SystemTray::HandleSniContextMenu(int x, int y) {
    if (m_menu_visible) {
        HideContextMenu();
    } else {
        ShowContextMenu(x, y);
    }
}

void SystemTray::PollEvents() {
    // 1. Process DBus StatusNotifierItem events
    if (m_bus) {
        auto* bus = static_cast<sd_bus*>(m_bus);
        while (sd_bus_process(bus, nullptr) > 0) {}
    }

    // 2. Process X11 events
    if (!m_dpy) return;
    Display* d = reinterpret_cast<Display*>(m_dpy);
    Window tray_w = static_cast<Window>(m_tray_win);
    Window menu_w = static_cast<Window>(m_menu_win);

    while (XPending(d)) {
        XEvent ev;
        XNextEvent(d, &ev);

        if (tray_w && ev.xany.window == tray_w) {
            if (ev.type == ConfigureNotify) {
                m_tray_width = ev.xconfigure.width;
                m_tray_height = ev.xconfigure.height;
            } else if (ev.type == Expose) {
                RedrawTrayIcon(m_tray_width, m_tray_height);
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.button == 1) { // Left click
                    if (m_menu_visible) {
                        HideContextMenu();
                    } else {
                        ToggleMainWindow();
                    }
                } else if (ev.xbutton.button == 3) { // Right click
                    if (m_menu_visible) {
                        HideContextMenu();
                    } else {
                        ShowContextMenu(ev.xbutton.x_root, ev.xbutton.y_root);
                    }
                }
            }
        } else if (ev.xany.window == menu_w) {
            if (ev.type == Expose) {
                RedrawMenu();
            } else if (ev.type == MotionNotify) {
                int item = ev.xmotion.y / 25;
                if (item < 0 || item > 2) item = -1;
                if (item != m_menu_hover) {
                    m_menu_hover = item;
                    RedrawMenu();
                }
            } else if (ev.type == LeaveNotify) {
                if (m_menu_hover != -1) {
                    m_menu_hover = -1;
                    RedrawMenu();
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.x < 0 || ev.xbutton.x > m_menu_w ||
                    ev.xbutton.y < 0 || ev.xbutton.y > m_menu_h) {
                    HideContextMenu();
                }
            } else if (ev.type == ButtonRelease) {
                if (ev.xbutton.button == 1) {
                    if (ev.xbutton.x >= 0 && ev.xbutton.x <= m_menu_w &&
                        ev.xbutton.y >= 0 && ev.xbutton.y <= m_menu_h) {
                        int item = ev.xbutton.y / 25;
                        if (item == 0) {
                            HideContextMenu();
                            RestoreMainWindow();
                        } else if (item == 1) {
                            if (m_main_window) {
                                m_main_window->TogglePenClick();
                            }
                            HideContextMenu();
                        } else if (item == 2) {
                            HideContextMenu();
                            if (m_main_window) {
                                m_main_window->RequestClose();
                            }
                            if (m_glfw_window) {
                                glfwSetWindowShouldClose(m_glfw_window, GLFW_TRUE);
                            }
                        }
                    } else {
                        HideContextMenu();
                    }
                }
            }
        }
    }
}

void SystemTray::WaitEventsTimeout(int timeout_ms) {
    struct pollfd pfds[2];
    int nfds = 0;

    if (m_dpy) {
        Display* d = reinterpret_cast<Display*>(m_dpy);
        if (XPending(d)) return;
        pfds[nfds].fd = ConnectionNumber(d);
        pfds[nfds].events = POLLIN;
        nfds++;
    }

    if (m_bus) {
        auto* bus = static_cast<sd_bus*>(m_bus);
        int bus_fd = sd_bus_get_fd(bus);
        if (bus_fd >= 0) {
            pfds[nfds].fd = bus_fd;
            pfds[nfds].events = sd_bus_get_events(bus);
            nfds++;
        }
    }

    if (nfds > 0) {
        poll(pfds, nfds, timeout_ms);
    } else {
        usleep(timeout_ms * 1000);
    }
}

void SystemTray::RedrawTrayIcon(int width, int height) {
    if (!m_dpy || !m_tray_win || !m_gc_tray) return;
    Display* d = reinterpret_cast<Display*>(m_dpy);
    Window tray_w = static_cast<Window>(m_tray_win);
    GC gc = reinterpret_cast<GC>(m_gc_tray);

    int icon_size = 24;
    XImage* img = reinterpret_cast<XImage*>(m_ximg_24);
    if (width >= 32 && height >= 32 && m_ximg_32) {
        icon_size = 32;
        img = reinterpret_cast<XImage*>(m_ximg_32);
    }

    if (!img) return;

    int dx = (width - icon_size) / 2;
    int dy = (height - icon_size) / 2;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;

    XPutImage(d, tray_w, gc, img, 0, 0, dx, dy, icon_size, icon_size);
    XFlush(d);
}

void SystemTray::ShowContextMenu(int root_x, int root_y) {
    if (!m_dpy || !m_menu_win) return;
    Display* d = reinterpret_cast<Display*>(m_dpy);
    Window menu = static_cast<Window>(m_menu_win);
    int screen = DefaultScreen(d);
    int sw = DisplayWidth(d, screen);
    int sh = DisplayHeight(d, screen);

    m_menu_x = root_x;
    m_menu_y = root_y;

    if (m_menu_x + m_menu_w > sw) m_menu_x = sw - m_menu_w - 4;
    if (m_menu_x < 0) m_menu_x = 0;

    if (m_menu_y + m_menu_h > sh) {
        m_menu_y = root_y - m_menu_h;
    }
    if (m_menu_y < 0) m_menu_y = 0;

    XMoveWindow(d, menu, m_menu_x, m_menu_y);
    XMapRaised(d, menu);
    XSync(d, False);

    XUngrabPointer(d, CurrentTime);
    XGrabPointer(d, menu, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    m_menu_visible = true;
    m_menu_hover = -1;
    RedrawMenu();
}

void SystemTray::RedrawMenu() {
    if (!m_dpy || !m_menu_win || !m_gc_menu || !m_menu_visible) return;
    Display* d = reinterpret_cast<Display*>(m_dpy);
    Window menu = static_cast<Window>(m_menu_win);
    GC gc = reinterpret_cast<GC>(m_gc_menu);

    bool pen_click = m_main_window ? m_main_window->GetPenClick() : true;

    // Background
    XSetForeground(d, gc, 0x1C1C22);
    XFillRectangle(d, menu, gc, 0, 0, m_menu_w, m_menu_h);

    for (int i = 0; i < 3; ++i) {
        int y = i * 25;
        if (i == m_menu_hover) {
            XSetForeground(d, gc, 0x2A2A38);
            XFillRectangle(d, menu, gc, 1, y + 1, m_menu_w - 2, 23);
        }

        if (i == 0) {
            XSetForeground(d, gc, (i == m_menu_hover) ? 0xFFFFFF : 0xDDDDDD);
            XDrawString(d, menu, gc, 16, y + 17, "Open osu!Point", 14);
        } else if (i == 1) {
            // Checkbox
            XSetForeground(d, gc, 0x555566);
            XDrawRectangle(d, menu, gc, 14, y + 6, 12, 12);
            if (pen_click) {
                XSetForeground(d, gc, 0xFF66AA); // osu! pink
                XFillRectangle(d, menu, gc, 16, y + 8, 9, 9);
            }
            XSetForeground(d, gc, (i == m_menu_hover) ? 0xFFFFFF : 0xDDDDDD);
            XDrawString(d, menu, gc, 36, y + 17, "Pen Click", 9);
        } else if (i == 2) {
            XSetForeground(d, gc, (i == m_menu_hover) ? 0xFF6677 : 0xDDDDDD);
            XDrawString(d, menu, gc, 16, y + 17, "Exit", 4);
        }

        if (i < 2) {
            XSetForeground(d, gc, 0x282832);
            XDrawLine(d, menu, gc, 6, y + 24, m_menu_w - 6, y + 24);
        }
    }
    XFlush(d);
}

void SystemTray::HideContextMenu() {
    if (!m_dpy || !m_menu_win || !m_menu_visible) return;
    Display* d = reinterpret_cast<Display*>(m_dpy);
    Window menu = static_cast<Window>(m_menu_win);
    XUngrabPointer(d, CurrentTime);
    XUnmapWindow(d, menu);
    XSync(d, False);
    m_menu_visible = false;
    m_menu_hover = -1;
}

void SystemTray::RestoreMainWindow() {
    if (!m_glfw_window) return;
    glfwShowWindow(m_glfw_window);
    glfwRestoreWindow(m_glfw_window);
    glfwFocusWindow(m_glfw_window);
}

void SystemTray::HideMainWindow() {
    if (!m_glfw_window) return;
    glfwHideWindow(m_glfw_window);
}

void SystemTray::ToggleMainWindow() {
    if (!m_glfw_window) return;
    if (glfwGetWindowAttrib(m_glfw_window, GLFW_VISIBLE) && !glfwGetWindowAttrib(m_glfw_window, GLFW_ICONIFIED)) {
        HideMainWindow();
    } else {
        RestoreMainWindow();
    }
}

void SystemTray::NotifyMenuUpdated() {
    if (!m_bus) return;
    auto* bus = static_cast<sd_bus*>(m_bus);
    ++m_menu_revision;
    sd_bus_emit_signal(bus, "/MenuBar", "com.canonical.dbusmenu",
                       "LayoutUpdated", "ui", m_menu_revision, 0);
    sd_bus_emit_signal(bus, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewMenu", "");
    sd_bus_emit_signal(bus, "/StatusNotifierItem", "org.freedesktop.StatusNotifierItem", "NewMenu", "");
}

} // namespace osupoint::gui
