// tour.cpp ─ 各エリアの絵を撮る（ASTRO_TOUR=1）。見た目の確認用。
#include "sim.h"
#include "util.h"
#include <cstdio>

int RunTour(Game& g) {
    struct Spot {
        const char* name;
        Vector3     pos;
        float       yaw;
        AbilityType ability;
        int         frames;   // カメラを落ち着かせる（乗り物は進ませる）フレーム数
    };
    const Spot spots[] = {
        {"01_start",     {  0.0f, 1.2f, -4.0f}, 0.0f,  AbilityType::None,  42},
        {"02_steps",     {  0.0f, 1.2f,  8.0f}, 0.0f,  AbilityType::Dash,  42},
        {"03_plateau",   {  0.0f, 1.7f, 20.0f}, 0.0f,  AbilityType::Dash,  42},
        {"04_spring",    { -5.5f, 1.7f, 23.0f}, 0.0f,  AbilityType::Dash,  42},
        {"05_ledge",     { -5.5f, 6.2f, 20.5f}, 0.0f,  AbilityType::Dash,  30},
        {"06_slide_in",  { -5.5f, 6.2f, 22.2f}, 0.0f,  AbilityType::Dash,  30},
        {"07_slide_mid", { -3.0f, 6.3f, 22.6f}, 0.0f,  AbilityType::Dash, 100},
        {"08_slide_end", { -3.0f, 6.3f, 22.6f}, 0.0f,  AbilityType::Dash, 170},
        {"09_jumps",     {  0.0f, 1.7f, 28.5f}, 0.0f,  AbilityType::Dash,  42},
        {"10_hovergap",  {  0.0f, 2.0f, 45.0f}, 0.0f,  AbilityType::Dash,  42},
        {"11_gate",      { -5.0f, 1.6f, 55.5f}, 0.0f,  AbilityType::Dash,  42},
        {"12_tower",     {  0.0f, 1.6f, 57.0f}, 0.0f,  AbilityType::Dash,  42},
        {"13_subpath",   { 12.0f, 1.8f, 20.0f}, -1.2f, AbilityType::Dash,  42},
        {"14_goal",      {  0.0f, 9.6f, 59.5f}, 0.0f,  AbilityType::Dash,  42},
    };

    for (const Spot& s : spots) {
        BuildLevel(g);
        if (s.ability != AbilityType::None) GrantAbility(g, s.ability);
        PlacePlayer(g, s.pos, true);
        g.rig.yaw = s.yaw;
        g.time = 20.0f;                 // 操作説明を消す
        for (int i = 0; i < s.frames; ++i) {  // カメラを落ち着かせる（乗り物は進む）
            g.in = Input{};
            SimStep(g, 1.0f / 60.0f);
        }
        DrawGame(g);
        TakeScreenshot(TextFormat("tour_%s.png", s.name));
    }
    // ── 攻撃の見え方を撮る（静止ポーズでは分からないので入力を作って動かす）
    auto shootFrames = [&](const char* name, int frames, Input in) {
        for (int i = 0; i < frames; ++i) {
            g.in = in;
            g.in.jumpPressed  = (i == 0) && in.jumpHeld;
            g.in.punchPressed = (i == 0) && in.punchHeld;
            g.hitStop = 0.0f;             // 撮影中は止めない
            SimStep(g, 1.0f / 60.0f);
        }
        DrawGame(g);
        TakeScreenshot(TextFormat("tour_%s.png", name));
    };

    // スピンパンチ：振り始め → 命中の瞬間
    {
        Input punch{}; punch.punchHeld = true;
        BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 1.6f}, true); g.time = 20.0f;
        for (int i = 0; i < 20; ++i) { g.in = Input{}; SimStep(g, 1.0f / 60.0f); }
        shootFrames("15_punch_swing", 5, punch);
        Input none{};
        shootFrames("16_punch_hit", 4, none);
    }
    // ホバー：ジェットの見え方（控えめにしてあるので、粒と炎の量を確認する用）
    {
        Input hover{}; hover.jumpHeld = true; hover.move = Vector2{0, 1};
        BuildLevel(g); PlacePlayer(g, {0.0f, 1.2f, 4.0f}, true); g.time = 20.0f;
        shootFrames("25_hover_jet", 40, hover);
    }
    // レーザー：ホバーしながら真下を撃つ
    {
        Input laser{}; laser.jumpHeld = true; laser.punchHeld = true;
        BuildLevel(g); PlacePlayer(g, {0.0f, 2.2f, 41.5f}, false);
        g.player.timeSinceJump = 99.0f; g.time = 20.0f;
        shootFrames("17_laser", 13, laser);   // 的が生きているうちに撮る
    }
    // ワイヤー：ぶら下がって振っている所
    {
        Input wire{}; wire.abilityHeld = true; wire.move = Vector2{0, 1};
        BuildLevel(g); GrantAbility(g, AbilityType::Wire);
        PlacePlayer(g, {0.0f, 8.0f, 42.0f}, false); g.time = 20.0f;
        for (int i = 0; i < 45; ++i) {
            g.in = wire;
            g.in.abilityPressed = (i == 0);
            g.hitStop = 0.0f;
            SimStep(g, 1.0f / 60.0f);
        }
        DrawGame(g);
        TakeScreenshot("tour_19_wire_swing.png");
    }
    // 驚き：岩にワイヤーを引っ掛けた瞬間（岩の方が飛んでくる）
    {
        Input wire{}; wire.abilityHeld = true;
        BuildLevel(g); GrantAbility(g, AbilityType::Wire);
        PlacePlayer(g, {-11.0f, 1.6f, 21.0f}, true);
        g.rig.yaw = -PI * 0.5f; g.time = 20.0f;
        for (int i = 0; i < 60; ++i) { g.in = Input{}; SimStep(g, 1.0f / 60.0f); }  // カメラを向ける
        TakeScreenshot("tour_20_rock_before.png");
        for (int i = 0; i < 22; ++i) {
            g.in = wire;
            g.in.abilityPressed = (i == 0);
            g.hitStop = 0.0f;
            SimStep(g, 1.0f / 60.0f);
        }
        DrawGame(g);
        TakeScreenshot("tour_21_rock_pulled.png");
        for (int i = 0; i < 60; ++i) { g.in = Input{}; SimStep(g, 1.0f / 60.0f); }
        DrawGame(g);
        TakeScreenshot("tour_22_room_open.png");
    }
    // 破片：壊した直後（飛び散り）と、少し経った後（積もって静止）
    {
        Input punch{}; punch.punchHeld = true;
        BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 1.6f}, true); g.time = 20.0f;
        for (int i = 0; i < 20; ++i) { g.in = Input{}; SimStep(g, 1.0f / 60.0f); }
        shootFrames("23_debris_burst", 22, punch);
        Input none{};
        shootFrames("24_debris_settled", 100, none);
    }
    // ダッシュ体当たり
    {
        Input dash{}; dash.abilityHeld = true; dash.move = Vector2{0, 1};
        BuildLevel(g); GrantAbility(g, AbilityType::Dash);
        PlacePlayer(g, {3.5f, 1.0f, 1.0f}, true); g.time = 20.0f;
        shootFrames("18_dash_hit", 16, dash);
    }

    printf("[tour] %d shots + 8 action shots\n", (int)(sizeof(spots) / sizeof(spots[0])));
    return 0;
}
