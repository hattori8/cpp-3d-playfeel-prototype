#include "glfw_compat.h"

/* 古い emscripten の GLFW シムに存在しない関数の no-op 実装 */
#if ASTRO_NEEDS_GLFW_COMPAT

void glfwSetWindowAttrib(GLFWwindow *window, int attrib, int value) {
    (void)window; (void)attrib; (void)value;
}

GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow *window,
                                                            GLFWwindowcontentscalefun cbfun) {
    (void)window; (void)cbfun;
    return 0;
}
#endif /* ASTRO_NEEDS_GLFW_COMPAT */

/* 空の翻訳単位にならないようにするダミー */
typedef int astro_glfw_compat_dummy;
