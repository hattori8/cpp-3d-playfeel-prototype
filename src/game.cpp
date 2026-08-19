// game.cpp ─ 1フレームの流れ。処理順を1箇所で読めるようにするためのファイル。
#include "game.h"
#include "util.h"
#include <cmath>

Game gGame;

static float s_reloadPoll = 0.0f;

// ────────────────────────────────────────────── 入力
//
// キーボード / マウス / ゲームパッドの差はここだけで吸収する。ゲームロジックは
// Input しか見ない。この分離のおかげで、テストでは g.in を直接組み立てられる。

static float Deadzone(float v, float dz) {
    if (fabsf(v) < dz) return 0.0f;
    float s = (fabsf(v) - dz) / (1.0f - dz);
    return (v > 0.0f ? s : -s);
}

void GatherInput(Game& g) {
    Input& in = g.in;
    in = Input{};

    if (IsKeyDown(KEY_W)) in.move.y += 1.0f;
    if (IsKeyDown(KEY_S)) in.move.y -= 1.0f;
    if (IsKeyDown(KEY_D)) in.move.x += 1.0f;
    if (IsKeyDown(KEY_A)) in.move.x -= 1.0f;

    if (IsKeyDown(KEY_RIGHT)) in.look.x += 1.0f;
    if (IsKeyDown(KEY_LEFT))  in.look.x -= 1.0f;
    if (IsKeyDown(KEY_E))     in.look.x += 1.0f;
    if (IsKeyDown(KEY_Q))     in.look.x -= 1.0f;
    if (IsKeyDown(KEY_UP))    in.look.y += 1.0f;
    if (IsKeyDown(KEY_DOWN))  in.look.y -= 1.0f;

    in.jumpPressed  = IsKeyPressed(KEY_SPACE);
    in.jumpHeld     = IsKeyDown(KEY_SPACE);
    in.punchPressed = IsKeyPressed(KEY_J) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.punchHeld    = IsKeyDown(KEY_J) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.abilityPressed = IsKeyPressed(KEY_K) || IsKeyPressed(KEY_LEFT_SHIFT);
    in.abilityHeld    = IsKeyDown(KEY_K) || IsKeyDown(KEY_LEFT_SHIFT);

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        in.look.x += d.x * 0.35f;
        in.look.y -= d.y * 0.25f;
    }

    if (IsGamepadAvailable(0)) {
        float lx = Deadzone(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), 0.18f);
        float ly = Deadzone(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y), 0.18f);
        float rx = Deadzone(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X), 0.20f);
        float ry = Deadzone(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y), 0.20f);
        if (fabsf(lx) > 0.0f || fabsf(ly) > 0.0f) {
            in.move.x = lx;
            in.move.y = -ly;
            in.usingPad = true;
        }
        if (fabsf(rx) > 0.0f) in.look.x += rx;
        if (fabsf(ry) > 0.0f) in.look.y -= ry;

        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) in.jumpPressed = true;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))    in.jumpHeld = true;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) in.punchPressed = true;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))    in.punchHeld = true;
        // R2 = 能力（ステージ限定能力は R2/L2 に載せる想定）
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) in.abilityPressed = true;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))    in.abilityHeld = true;
    }

    float mag = sqrtf(in.move.x * in.move.x + in.move.y * in.move.y);
    if (mag > 1.0f) { in.move.x /= mag; in.move.y /= mag; }
}

// ────────────────────────────────────────────── ヒットストップ

float ApplyHitStop(Game& g, float frameDt) {
    if (g.hitStop > 0.0f) {
        g.hitStop -= frameDt;
        return 0.0f;      // 世界を止める
    }
    return frameDt;
}

// ────────────────────────────────────────────── 描画

void DrawGame(Game& g) {
    BeginDrawing();
    ClearBackground(Color{182, 214, 238, 255});
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           Color{120, 170, 225, 255}, Color{214, 232, 240, 255});

    BeginMode3D(g.rig.cam);
    DrawLevel(g);
    DrawWorldPhysics(g);      // 破片。地形の後、主人公の前
    DrawPlayer(g);
    DrawAbilityFx(g);
    DrawParticles(g);
    DrawEffects(g);
    if (g.editor.on) DrawEditor3D(g);   // 編集用の線と印は地形の上に重ねる
    EndMode3D();

    if (g.editor.on) {
        DrawEditorUI(g);      // 編集中は HUD の代わりにエディタの表示だけ出す
    } else {
        DrawHUD(g);
        if (g.debug.showDebug)  DrawDebugPanel(g);
        if (g.debug.showParams) DrawParamEditor(g);
    }
    EndDrawing();
}

// ────────────────────────────────────────────── 1フレーム
//
// 処理順は意図を持って固定する。
//   ・カメラの回転はプレイヤーより前（移動が最新のカメラ方向を使う）
//   ・動く床はプレイヤーより前（床の移動量を受け取って運ばれる）
//   ・Interaction はプレイヤーより後（そのフレームに積まれた分をまとめて解決）
//   ・カメラの追従は最後（最新のプレイヤー位置を使う）
void FrameStep(Game& g, float frameDt) {
    if (frameDt > 1.0f / 20.0f) frameDt = 1.0f / 20.0f;   // 巨大な dt を潰す

    // レベルエディタ（F4）。編集中は世界を止めて、入力も描画もエディタ側に渡す。
    // 「止まっているものを触る」ようにすると、移動床やゲートが作者の置いた位置に
    // 留まるので、編集と保存の対象が食い違わない。
    if (IsKeyPressed(KEY_F4)) ToggleEditor(g);
    if (g.editor.on) {
        UpdateEditor(g, frameDt);
        if (g.toastTimer > 0.0f) g.toastTimer -= frameDt;
        DrawGame(g);
        return;
    }

    UpdateDebugKeys(g, frameDt);

    s_reloadPoll += frameDt;
    if (s_reloadPoll > 0.4f) {
        s_reloadPoll = 0.0f;
        if (ReloadParamsIfChanged(g.p)) Toast(g, "PARAMS HOT-RELOADED");
        // level.txt も同じ扱いにする。外のエディタで保存した瞬間に反映される。
        if (ReloadLevelIfChanged(g))    Toast(g, "LEVEL HOT-RELOADED");
    }

    GatherInput(g);

    float dt = ApplyHitStop(g, frameDt);
    if (dt > 0.0f) {
        g.time += dt;

        UpdateRigInput(g, dt);

        UpdateLevel(g, dt);
        UpdatePlayer(g, dt);

        // 破片を回す。主人公より後なのは、押す側の位置が確定してから運ぶため。
        // ここで出た接触は Event として積まれ、下の DispatchGameEvents で配られる。
        UpdateWorldPhysics(g, dt);

        ResolveInteractions(g);
        DispatchGameEvents(g);
    }

    UpdateRigFollow(g, frameDt);   // 揺れと追従はヒットストップ中も進める

    if (g.toastTimer  > 0.0f) g.toastTimer  -= frameDt;
    if (g.clearBanner > 0.0f) g.clearBanner -= frameDt;

    DrawGame(g);
}

void FrameStepCallback(void) {
    FrameStep(gGame, GetFrameTime());
}
