#pragma once

#include <GLFW/glfw3.h>

namespace osupoint::gui {

void SetAppWindowIcon(GLFWwindow* window);

const unsigned char* GetIcon16Rgba();
const unsigned char* GetIcon32Rgba();
const unsigned char* GetIcon48Rgba();
const unsigned char* GetIcon256Rgba();
const unsigned char* GetIcon24Bgra();
const unsigned char* GetIcon32Bgra();

} // namespace osupoint::gui
