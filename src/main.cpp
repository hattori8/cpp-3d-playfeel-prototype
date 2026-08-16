// main.cpp ─ 起動 / 終了 / メインループだけ。
//
// 1フレームの中身は game.cpp の FrameStep にある。
#include "raylib.h"
#include "game.h"
#include <cstdio>
#include <cstdlib>

#if defined(PLATFORM_WEB)
  #include <emscripten/emscripten.h>
#endif

static const int kScreenW = 1280;
static const int kScreenH = 720;

int main(void) {
#if defined(PLATFORM_WEB)
    // ブラウザではリサイズ可にすると raylib がキャンバスの実解像度をウィンドウサイズに
    // 追従させてしまい、CSS 側の 16:9 と食い違って画が歪む。1280x720 で固定する。
    SetConfigFlags(FLAG_MSAA_4X_HINT);
#else
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
#endif
    InitWindow(kScreenW, kScreenH, "astro_proto - basic moveset prototype");
#if defined(PLATFORM_WEB)
    SetExitKey(0);            // ブラウザでは ESC でキャンバスを閉じない
#else
    SetTargetFPS(60);         // web は requestAnimationFrame が刻むので入れない
    SetExitKey(KEY_ESCAPE);
#endif

    Game& g = gGame;
    LoadParams(g.p, &g.paramsPath);
    BuildLevel(g);
    UpdateRigInput(g, 1.0f / 60.0f);
    UpdateRigFollow(g, 1.0f);      // 初期位置へ一気に寄せる

    if (getenv("ASTRO_TEST")) {
        int fails = RunSelfTest(g);
        CloseWindow();
        return fails == 0 ? 0 : 1;
    }
    if (getenv("ASTRO_TOUR")) {
        int r = RunTour(g);
        CloseWindow();
        return r;
    }

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(FrameStepCallback, 0, 1);
#else
    while (!WindowShouldClose()) {
        FrameStep(g, GetFrameTime());
    }
    CloseWindow();
#endif
    return 0;
}
