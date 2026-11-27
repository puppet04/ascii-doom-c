/*
 * Ponte de linkagem estatica para MSYS2 UCRT64 (libraylib.a + libglfw3.a).
 * A libraylib.a do MSYS2 pede os simbolos __imp_glfw* (como se fossem de DLL),
 * mas a libglfw3.a entrega os simbolos estaticos glfw*. Aqui a gente liga cada
 * __imp_glfw* direto na funcao estatica pra gerar um .exe portatil sem DLLs.
 */
#define GLFW_STATIC_IMP(fn) \
    extern void fn(void); \
    void (*const __imp_##fn)(void) = fn

/* Cada linha cria um ponteiro constante __imp_ para a funcao real da GLFW */
GLFW_STATIC_IMP(glfwCreateStandardCursor);
GLFW_STATIC_IMP(glfwCreateWindow);
GLFW_STATIC_IMP(glfwDefaultWindowHints);
GLFW_STATIC_IMP(glfwDestroyWindow);
GLFW_STATIC_IMP(glfwFocusWindow);
GLFW_STATIC_IMP(glfwGetClipboardString);
GLFW_STATIC_IMP(glfwGetError);
GLFW_STATIC_IMP(glfwGetFramebufferSize);
GLFW_STATIC_IMP(glfwGetGamepadState);
GLFW_STATIC_IMP(glfwGetJoystickName);
GLFW_STATIC_IMP(glfwGetKeyName);
GLFW_STATIC_IMP(glfwGetKeyScancode);
GLFW_STATIC_IMP(glfwGetMonitorName);
GLFW_STATIC_IMP(glfwGetMonitorPhysicalSize);
GLFW_STATIC_IMP(glfwGetMonitorPos);
GLFW_STATIC_IMP(glfwGetMonitors);
GLFW_STATIC_IMP(glfwGetMonitorWorkarea);
GLFW_STATIC_IMP(glfwGetPlatform);
GLFW_STATIC_IMP(glfwGetPrimaryMonitor);
GLFW_STATIC_IMP(glfwGetProcAddress);
GLFW_STATIC_IMP(glfwGetTime);
GLFW_STATIC_IMP(glfwGetVideoMode);
GLFW_STATIC_IMP(glfwGetVideoModes);
GLFW_STATIC_IMP(glfwGetWin32Window);
GLFW_STATIC_IMP(glfwGetWindowAttrib);
GLFW_STATIC_IMP(glfwGetWindowContentScale);
GLFW_STATIC_IMP(glfwGetWindowMonitor);
GLFW_STATIC_IMP(glfwGetWindowPos);
GLFW_STATIC_IMP(glfwHideWindow);
GLFW_STATIC_IMP(glfwIconifyWindow);
GLFW_STATIC_IMP(glfwInit);
GLFW_STATIC_IMP(glfwInitAllocator);
GLFW_STATIC_IMP(glfwJoystickPresent);
GLFW_STATIC_IMP(glfwMakeContextCurrent);
GLFW_STATIC_IMP(glfwMaximizeWindow);
GLFW_STATIC_IMP(glfwPollEvents);
GLFW_STATIC_IMP(glfwRawMouseMotionSupported);
GLFW_STATIC_IMP(glfwRestoreWindow);
GLFW_STATIC_IMP(glfwSetCharCallback);
GLFW_STATIC_IMP(glfwSetClipboardString);
GLFW_STATIC_IMP(glfwSetCursor);
GLFW_STATIC_IMP(glfwSetCursorEnterCallback);
GLFW_STATIC_IMP(glfwSetCursorPos);
GLFW_STATIC_IMP(glfwSetCursorPosCallback);
GLFW_STATIC_IMP(glfwSetDropCallback);
GLFW_STATIC_IMP(glfwSetErrorCallback);
GLFW_STATIC_IMP(glfwSetFramebufferSizeCallback);
GLFW_STATIC_IMP(glfwSetInputMode);
GLFW_STATIC_IMP(glfwSetJoystickCallback);
GLFW_STATIC_IMP(glfwSetKeyCallback);
GLFW_STATIC_IMP(glfwSetMouseButtonCallback);
GLFW_STATIC_IMP(glfwSetScrollCallback);
GLFW_STATIC_IMP(glfwSetWindowAttrib);
GLFW_STATIC_IMP(glfwSetWindowContentScaleCallback);
GLFW_STATIC_IMP(glfwSetWindowFocusCallback);
GLFW_STATIC_IMP(glfwSetWindowIcon);
GLFW_STATIC_IMP(glfwSetWindowIconifyCallback);
GLFW_STATIC_IMP(glfwSetWindowMaximizeCallback);
GLFW_STATIC_IMP(glfwSetWindowMonitor);
GLFW_STATIC_IMP(glfwSetWindowOpacity);
GLFW_STATIC_IMP(glfwSetWindowPos);
GLFW_STATIC_IMP(glfwSetWindowPosCallback);
GLFW_STATIC_IMP(glfwSetWindowShouldClose);
GLFW_STATIC_IMP(glfwSetWindowSize);
GLFW_STATIC_IMP(glfwSetWindowSizeCallback);
GLFW_STATIC_IMP(glfwSetWindowSizeLimits);
GLFW_STATIC_IMP(glfwSetWindowTitle);
GLFW_STATIC_IMP(glfwShowWindow);
GLFW_STATIC_IMP(glfwSwapBuffers);
GLFW_STATIC_IMP(glfwSwapInterval);
GLFW_STATIC_IMP(glfwTerminate);
GLFW_STATIC_IMP(glfwUpdateGamepadMappings);
GLFW_STATIC_IMP(glfwWaitEvents);
GLFW_STATIC_IMP(glfwWindowHint);
GLFW_STATIC_IMP(glfwWindowShouldClose);
