CXX ?= g++
ARCHFLAGS ?=
BASE_FLAGS = -std=c++20 -O3 $(ARCHFLAGS) -flto -fno-exceptions -fno-rtti -fomit-frame-pointer -fno-stack-protector -ffast-math -fno-finite-math-only -fno-math-errno -Wall -Wextra -Wpedantic -DNDEBUG -D_GNU_SOURCE
CXXFLAGS ?= $(BASE_FLAGS)
GUI_CXXFLAGS ?= -std=c++20 -O3 $(ARCHFLAGS) -flto -fomit-frame-pointer -fno-stack-protector -ffast-math -fno-math-errno -Wall -Wextra -DNDEBUG -D_GNU_SOURCE
TEST_CXXFLAGS ?= -std=c++20 -O3 -pthread -Wall -Wextra -Iinclude -Itests/e2e -D_GNU_SOURCE
INCLUDES = -Iinclude
GUI_INCLUDES = -Iinclude -Isrc/gui -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends
LDFLAGS = -lpthread -lrt
GUI_LDFLAGS = -lpthread -lrt -lglfw -lGL -ldl -lX11 -lsystemd

BUILD_DIR ?= build
DIST_DIR ?= dist
UNIFIED_TARGET ?= $(BUILD_DIR)/osupoint
DAEMON_TARGET ?= $(BUILD_DIR)/osupointd
GUI_TARGET ?= $(BUILD_DIR)/osupoint-gui
CORE_TEST_TARGET ?= $(BUILD_DIR)/test_core
GUI_TEST_TARGET ?= $(BUILD_DIR)/test_gui_math
REGRESSION_TARGET ?= $(BUILD_DIR)/test_regressions
E2E_TEST_TARGET = tests/e2e/test_runner

DAEMON_CORE_OBJS = \
    $(BUILD_DIR)/daemon/tablet_decoder.o \
    $(BUILD_DIR)/daemon/uinput_emitter.o \
    $(BUILD_DIR)/daemon/hidraw_reader.o \
    $(BUILD_DIR)/daemon/rt_scheduler.o \
    $(BUILD_DIR)/daemon/transform_pipeline.o \
    $(BUILD_DIR)/daemon/motion_filters.o \
    $(BUILD_DIR)/daemon/driver_service.o \
    $(BUILD_DIR)/common/config_manager.o

DAEMON_OBJS = \
    $(DAEMON_CORE_OBJS) \
    $(BUILD_DIR)/daemon/main.o

CORE_TEST_OBJS = \
    $(BUILD_DIR)/tests/test_core.o \
    $(BUILD_DIR)/daemon/tablet_decoder.o \
    $(BUILD_DIR)/daemon/rt_scheduler.o \
    $(BUILD_DIR)/daemon/transform_pipeline.o \
    $(BUILD_DIR)/daemon/motion_filters.o \
    $(BUILD_DIR)/common/config_manager.o

IMGUI_OBJS = \
    $(BUILD_DIR)/imgui/imgui.o \
    $(BUILD_DIR)/imgui/imgui_draw.o \
    $(BUILD_DIR)/imgui/imgui_tables.o \
    $(BUILD_DIR)/imgui/imgui_widgets.o \
    $(BUILD_DIR)/imgui/imgui_impl_glfw.o \
    $(BUILD_DIR)/imgui/imgui_impl_opengl3.o

GUI_OBJS = \
    $(IMGUI_OBJS) \
    $(BUILD_DIR)/gui/theme.o \
    $(BUILD_DIR)/gui/shm_client.o \
    $(BUILD_DIR)/gui/area_canvas.o \
    $(BUILD_DIR)/gui/ui_window.o \
    $(BUILD_DIR)/gui/app_icon.o \
    $(BUILD_DIR)/gui/system_tray.o \
    $(BUILD_DIR)/gui/stb_image_impl.o \
    $(DAEMON_CORE_OBJS) \
    $(BUILD_DIR)/gui/main.o

E2E_SRCS = \
    tests/e2e/test_math.cpp \
    tests/e2e/test_filters.cpp \
    tests/e2e/test_config.cpp \
    tests/e2e/test_uinput_loopback.cpp \
    tests/e2e/test_runner.cpp

.PHONY: all clean osupoint osupointd gui test test-core test-e2e test-gui test-regressions benchmark check install uninstall sse avx avx2 all-targets package-all

all: osupoint osupointd gui $(E2E_TEST_TARGET) $(GUI_TEST_TARGET)

osupoint: $(UNIFIED_TARGET)
osupointd: $(DAEMON_TARGET)
gui: $(GUI_TARGET)

$(BUILD_DIR)/daemon/%.o: src/daemon/%.cpp
	@mkdir -p $(BUILD_DIR)/daemon
	$(CXX) $(CXXFLAGS) -MMD -MP $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/common/%.o: src/common/%.cpp
	@mkdir -p $(BUILD_DIR)/common
	$(CXX) $(CXXFLAGS) -MMD -MP $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/gui/%.o: src/gui/%.cpp
	@mkdir -p $(BUILD_DIR)/gui
	$(CXX) $(GUI_CXXFLAGS) -MMD -MP $(GUI_INCLUDES) -c $< -o $@

$(BUILD_DIR)/imgui/%.o: third_party/imgui/%.cpp
	@mkdir -p $(BUILD_DIR)/imgui
	$(CXX) $(GUI_CXXFLAGS) -MMD -MP $(GUI_INCLUDES) -c $< -o $@

$(BUILD_DIR)/imgui/%.o: third_party/imgui/backends/%.cpp
	@mkdir -p $(BUILD_DIR)/imgui
	$(CXX) $(GUI_CXXFLAGS) -MMD -MP $(GUI_INCLUDES) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(BUILD_DIR)/tests
	$(CXX) $(CXXFLAGS) -MMD -MP $(INCLUDES) -c $< -o $@

$(UNIFIED_TARGET): $(GUI_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(GUI_CXXFLAGS) $(GUI_OBJS) -o $@ $(GUI_LDFLAGS)

$(DAEMON_TARGET): $(DAEMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DAEMON_OBJS) -o $@ $(LDFLAGS)

$(GUI_TARGET): $(UNIFIED_TARGET)
	@mkdir -p $(dir $@)
	ln -sf osupoint $@

$(CORE_TEST_TARGET): $(CORE_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CORE_TEST_OBJS) -o $@ $(LDFLAGS)

$(GUI_TEST_TARGET): tests/test_gui_math.cpp $(BUILD_DIR)/common/config_manager.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_gui_math.cpp $(BUILD_DIR)/common/config_manager.o -o $@ $(LDFLAGS)

$(E2E_TEST_TARGET): $(E2E_SRCS) tests/e2e/test_framework.h tests/e2e/test_vectors.h tests/e2e/test_pipeline.h tests/e2e/test_filter_impl.h tests/e2e/test_config_impl.h
	$(CXX) $(TEST_CXXFLAGS) $(E2E_SRCS) -o $(E2E_TEST_TARGET)

$(REGRESSION_TARGET): tests/test_regressions.cpp $(DAEMON_CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(TEST_CXXFLAGS) $< $(DAEMON_CORE_OBJS) -o $@ $(LDFLAGS)

# --- Multi-Target Architecture Builds ---
sse:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sse ARCHFLAGS="-msse2" UNIFIED_TARGET=$(BUILD_DIR)/sse/osupoint DAEMON_TARGET=$(BUILD_DIR)/sse/osupointd GUI_TARGET=$(BUILD_DIR)/sse/osupoint-gui osupoint osupointd gui
	@mkdir -p $(BUILD_DIR)
	@cp -f $(BUILD_DIR)/sse/osupoint $(BUILD_DIR)/osupoint-sse
	@cp -f $(BUILD_DIR)/sse/osupointd $(BUILD_DIR)/osupointd-sse
	@ln -sf osupoint-sse $(BUILD_DIR)/osupoint-gui-sse

avx:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/avx ARCHFLAGS="-mavx" UNIFIED_TARGET=$(BUILD_DIR)/avx/osupoint DAEMON_TARGET=$(BUILD_DIR)/avx/osupointd GUI_TARGET=$(BUILD_DIR)/avx/osupoint-gui osupoint osupointd gui
	@mkdir -p $(BUILD_DIR)
	@cp -f $(BUILD_DIR)/avx/osupoint $(BUILD_DIR)/osupoint-avx
	@cp -f $(BUILD_DIR)/avx/osupointd $(BUILD_DIR)/osupointd-avx
	@ln -sf osupoint-avx $(BUILD_DIR)/osupoint-gui-avx

avx2:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/avx2 ARCHFLAGS="-mavx2 -mfma" UNIFIED_TARGET=$(BUILD_DIR)/avx2/osupoint DAEMON_TARGET=$(BUILD_DIR)/avx2/osupointd GUI_TARGET=$(BUILD_DIR)/avx2/osupoint-gui osupoint osupointd gui
	@mkdir -p $(BUILD_DIR)
	@cp -f $(BUILD_DIR)/avx2/osupoint $(BUILD_DIR)/osupoint-avx2
	@cp -f $(BUILD_DIR)/avx2/osupointd $(BUILD_DIR)/osupointd-avx2
	@ln -sf osupoint-avx2 $(BUILD_DIR)/osupoint-gui-avx2
	@cp -f $(BUILD_DIR)/avx2/osupoint $(BUILD_DIR)/osupoint
	@cp -f $(BUILD_DIR)/avx2/osupointd $(BUILD_DIR)/osupointd
	@ln -sf osupoint $(BUILD_DIR)/osupoint-gui

all-targets: sse avx avx2 $(E2E_TEST_TARGET) $(CORE_TEST_TARGET) $(GUI_TEST_TARGET) $(REGRESSION_TARGET)

# --- Packaging Pipeline ---
package-all: all-targets test
	@echo "===> Packaging multi-target release archives..."
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)/sse $(DIST_DIR)/avx $(DIST_DIR)/avx2 $(DIST_DIR)/releases
	# Stage SSE
	@cp -f $(BUILD_DIR)/sse/osupoint $(DIST_DIR)/sse/osupoint
	@cp -f $(BUILD_DIR)/sse/osupointd $(DIST_DIR)/sse/osupointd
	@ln -sf osupoint $(DIST_DIR)/sse/osupoint-gui
	@cp -f packaging/start.sh $(DIST_DIR)/sse/start.sh
	@cp -f packaging/install.sh $(DIST_DIR)/sse/install.sh
	@cp -f packaging/uninstall.sh $(DIST_DIR)/sse/uninstall.sh
	@cp -f packaging/99-osupoint.rules $(DIST_DIR)/sse/99-osupoint.rules
	@cp -f packaging/osupoint.desktop $(DIST_DIR)/sse/osupoint.desktop
	@cp -f packaging/osupoint.service $(DIST_DIR)/sse/osupoint.service
	@cp -f packaging/*.png packaging/*.ico $(DIST_DIR)/sse/ 2>/dev/null || true
	@cp -f config.ini $(DIST_DIR)/sse/config.ini 2>/dev/null || true
	@cp -f README.md $(DIST_DIR)/sse/README.md 2>/dev/null || cp -f README_LINUX.md $(DIST_DIR)/sse/README.md 2>/dev/null || true
	@cp -f LICENSE $(DIST_DIR)/sse/LICENSE 2>/dev/null || true
	@chmod +x $(DIST_DIR)/sse/osupoint $(DIST_DIR)/sse/osupointd $(DIST_DIR)/sse/start.sh $(DIST_DIR)/sse/install.sh $(DIST_DIR)/sse/uninstall.sh
	# Stage AVX
	@cp -f $(BUILD_DIR)/avx/osupoint $(DIST_DIR)/avx/osupoint
	@cp -f $(BUILD_DIR)/avx/osupointd $(DIST_DIR)/avx/osupointd
	@ln -sf osupoint $(DIST_DIR)/avx/osupoint-gui
	@cp -f packaging/start.sh $(DIST_DIR)/avx/start.sh
	@cp -f packaging/install.sh $(DIST_DIR)/avx/install.sh
	@cp -f packaging/uninstall.sh $(DIST_DIR)/avx/uninstall.sh
	@cp -f packaging/99-osupoint.rules $(DIST_DIR)/avx/99-osupoint.rules
	@cp -f packaging/osupoint.desktop $(DIST_DIR)/avx/osupoint.desktop
	@cp -f packaging/osupoint.service $(DIST_DIR)/avx/osupoint.service
	@cp -f packaging/*.png packaging/*.ico $(DIST_DIR)/avx/ 2>/dev/null || true
	@cp -f config.ini $(DIST_DIR)/avx/config.ini 2>/dev/null || true
	@cp -f README.md $(DIST_DIR)/avx/README.md 2>/dev/null || cp -f README_LINUX.md $(DIST_DIR)/avx/README.md 2>/dev/null || true
	@cp -f LICENSE $(DIST_DIR)/avx/LICENSE 2>/dev/null || true
	@chmod +x $(DIST_DIR)/avx/osupoint $(DIST_DIR)/avx/osupointd $(DIST_DIR)/avx/start.sh $(DIST_DIR)/avx/install.sh $(DIST_DIR)/avx/uninstall.sh
	# Stage AVX2
	@cp -f $(BUILD_DIR)/avx2/osupoint $(DIST_DIR)/avx2/osupoint
	@cp -f $(BUILD_DIR)/avx2/osupointd $(DIST_DIR)/avx2/osupointd
	@ln -sf osupoint $(DIST_DIR)/avx2/osupoint-gui
	@cp -f packaging/start.sh $(DIST_DIR)/avx2/start.sh
	@cp -f packaging/install.sh $(DIST_DIR)/avx2/install.sh
	@cp -f packaging/uninstall.sh $(DIST_DIR)/avx2/uninstall.sh
	@cp -f packaging/99-osupoint.rules $(DIST_DIR)/avx2/99-osupoint.rules
	@cp -f packaging/osupoint.desktop $(DIST_DIR)/avx2/osupoint.desktop
	@cp -f packaging/osupoint.service $(DIST_DIR)/avx2/osupoint.service
	@cp -f packaging/*.png packaging/*.ico $(DIST_DIR)/avx2/ 2>/dev/null || true
	@cp -f config.ini $(DIST_DIR)/avx2/config.ini 2>/dev/null || true
	@cp -f README.md $(DIST_DIR)/avx2/README.md 2>/dev/null || cp -f README_LINUX.md $(DIST_DIR)/avx2/README.md 2>/dev/null || true
	@cp -f LICENSE $(DIST_DIR)/avx2/LICENSE 2>/dev/null || true
	@chmod +x $(DIST_DIR)/avx2/osupoint $(DIST_DIR)/avx2/osupointd $(DIST_DIR)/avx2/start.sh $(DIST_DIR)/avx2/install.sh $(DIST_DIR)/avx2/uninstall.sh
	# Create tar.gz and zip archives
	@tar -czf $(DIST_DIR)/releases/osuPoint-linux-sse.tar.gz -C $(DIST_DIR)/sse .
	@(cd $(DIST_DIR)/sse && zip -r -q ../releases/osuPoint-linux-sse.zip .)
	@tar -czf $(DIST_DIR)/releases/osuPoint-linux-avx.tar.gz -C $(DIST_DIR)/avx .
	@(cd $(DIST_DIR)/avx && zip -r -q ../releases/osuPoint-linux-avx.zip .)
	@tar -czf $(DIST_DIR)/releases/osuPoint-linux-avx2.tar.gz -C $(DIST_DIR)/avx2 .
	@(cd $(DIST_DIR)/avx2 && zip -r -q ../releases/osuPoint-linux-avx2.zip .)
	@echo "===> Multi-target release packages generated in $(DIST_DIR)/releases/:"
	@ls -lh $(DIST_DIR)/releases/

test: $(E2E_TEST_TARGET) $(CORE_TEST_TARGET) $(GUI_TEST_TARGET) $(REGRESSION_TARGET)
	@echo "=== Running core verification test harness ==="
	@$(CORE_TEST_TARGET)
	@echo "=== Running GUI headless verification test harness ==="
	@$(GUI_TEST_TARGET)
	@$(REGRESSION_TARGET)
	@echo "=== Running E2E verification test harness ==="
	@$(E2E_TEST_TARGET) --all

test-core: $(CORE_TEST_TARGET)
	@$(CORE_TEST_TARGET)

test-gui: $(GUI_TEST_TARGET)
	@$(GUI_TEST_TARGET)

.PHONY: test-gui-smoke
test-gui-smoke: $(UNIFIED_TARGET)
	python3 tests/gui_smoke.py $(UNIFIED_TARGET)

test-regressions: $(REGRESSION_TARGET)
	@$(REGRESSION_TARGET)

test-e2e: $(E2E_TEST_TARGET)
	@$(E2E_TEST_TARGET) --all

benchmark: $(E2E_TEST_TARGET)
	@$(E2E_TEST_TARGET) --benchmark

check: test

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(UNIFIED_TARGET) $(DESTDIR)$(BINDIR)/osupoint
	ln -sf osupoint $(DESTDIR)$(BINDIR)/osupointd
	ln -sf osupoint $(DESTDIR)$(BINDIR)/osupoint-gui
	install -d $(DESTDIR)/etc/udev/rules.d
	install -m 644 packaging/99-osupoint.rules $(DESTDIR)/etc/udev/rules.d/99-osupoint.rules
	install -d $(DESTDIR)/usr/share/applications
	install -m 644 packaging/osupoint.desktop $(DESTDIR)/usr/share/applications/osupoint.desktop
	rm -f $(DESTDIR)/usr/share/applications/osupoint-gui.desktop
	install -d $(DESTDIR)/usr/share/icons/hicolor/16x16/apps
	install -m 644 packaging/osupoint_1_16x16x32.png $(DESTDIR)/usr/share/icons/hicolor/16x16/apps/osupoint.png
	install -d $(DESTDIR)/usr/share/icons/hicolor/24x24/apps
	install -m 644 packaging/osupoint_24x24.png $(DESTDIR)/usr/share/icons/hicolor/24x24/apps/osupoint.png
	install -d $(DESTDIR)/usr/share/icons/hicolor/32x32/apps
	install -m 644 packaging/osupoint_2_32x32x32.png $(DESTDIR)/usr/share/icons/hicolor/32x32/apps/osupoint.png
	install -d $(DESTDIR)/usr/share/icons/hicolor/48x48/apps
	install -m 644 packaging/osupoint_3_48x48x32.png $(DESTDIR)/usr/share/icons/hicolor/48x48/apps/osupoint.png
	install -d $(DESTDIR)/usr/share/icons/hicolor/256x256/apps
	install -m 644 packaging/osupoint_4_256x256x32.png $(DESTDIR)/usr/share/icons/hicolor/256x256/apps/osupoint.png
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(DESTDIR)/usr/share/icons/hicolor || true
	@echo "osuPoint installed successfully to $(DESTDIR)$(BINDIR)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/osupoint
	rm -f $(DESTDIR)$(BINDIR)/osupointd
	rm -f $(DESTDIR)$(BINDIR)/osupoint-gui
	rm -f $(DESTDIR)/etc/udev/rules.d/99-osupoint.rules
	rm -f $(DESTDIR)/usr/share/applications/osupoint.desktop
	rm -f $(DESTDIR)/usr/share/applications/osupoint-gui.desktop
	@echo "osuPoint uninstalled."

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(E2E_TEST_TARGET)

-include $(wildcard $(BUILD_DIR)/daemon/*.d $(BUILD_DIR)/common/*.d $(BUILD_DIR)/gui/*.d $(BUILD_DIR)/imgui/*.d $(BUILD_DIR)/tests/*.d)
