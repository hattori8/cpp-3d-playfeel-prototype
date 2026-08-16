/* web/glfw_compat.h
 *
 * apt で入る emscripten 3.1.6 の GLFW シムには、raylib 5.5 が web 版で呼ぶ
 * glfwSetWindowAttrib / glfwSetWindowContentScaleCallback が存在しない。
 * 宣言をここで補い、実体は glfw_compat.c の no-op スタブで満たす。
 * どちらもウィンドウのリサイズ属性と DPI 変更通知にしか使われないので、
 * ブラウザ版では何もしなくても支障がない。
 * 新しい emsdk（3.1.30 以降）でビルドする場合はこのファイルは不要。
 */
#ifndef ASTRO_GLFW_COMPAT_H
#define ASTRO_GLFW_COMPAT_H

/* 3.1.30 より古い Emscripten でだけ有効にする。新しい emsdk では本物が入っている */
#define ASTRO_NEEDS_GLFW_COMPAT                                                   \
    (defined(__EMSCRIPTEN_major__) &&                                             \
     (__EMSCRIPTEN_major__ < 3 ||                                                 \
      (__EMSCRIPTEN_major__ == 3 && __EMSCRIPTEN_minor__ == 1 &&                  \
       __EMSCRIPTEN_tiny__ < 30)))

#if ASTRO_NEEDS_GLFW_COMPAT

typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWwindowcontentscalefun)(GLFWwindow *, float, float);

void glfwSetWindowAttrib(GLFWwindow *window, int attrib, int value);
GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow *window,
                                                            GLFWwindowcontentscalefun cbfun);

#endif /* ASTRO_NEEDS_GLFW_COMPAT */

#endif /* ASTRO_GLFW_COMPAT_H */
