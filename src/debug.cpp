// debug.cpp ─ HUD とデバッグ UI、パラメータの読み書き操作
#include "game.h"
#include "util.h"
#include <cstdio>
#include <cmath>

// ────────────────────────────────────────────── パラメータの書き出し

void SaveParams(const Game& g) {
    const char* path = g.paramsPath ? g.paramsPath : "params.txt";
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# astro_proto tuning params (F6 で上書き保存 / F5 で読み直し)\n");
    for (int i = 0; i < kParamCount; ++i) {
        fprintf(f, "%-18s %10.4f   # %s\n", kParamTable[i].name, ParamGet(g.p, i),
                kParamTable[i].doc);
    }
    fclose(f);
}

// 現在値を標準出力にも出す（ブラウザではコンソールにコピペ用として出る）
void DumpParams(const Game& g) {
    printf("---- params.txt ----\n");
    for (int i = 0; i < kParamCount; ++i) {
        printf("%-18s %10.4f   # %s\n", kParamTable[i].name, ParamGet(g.p, i),
               kParamTable[i].doc);
    }
    printf("--------------------\n");
    fflush(stdout);
}

// ────────────────────────────────────────────── デバッグ操作

void UpdateDebugKeys(Game& g, float rdt) {
    GameDebug& d = g.debug;

    if (IsKeyPressed(KEY_F1)) d.showDebug = !d.showDebug;
    if (IsKeyPressed(KEY_F2)) d.showCollision = !d.showCollision;
    if (IsKeyPressed(KEY_F3)) d.showParams = !d.showParams;
    if (IsKeyPressed(KEY_F5)) { LoadParams(g.p, &g.paramsPath); Toast(g, "PARAMS RELOADED"); }
    if (IsKeyPressed(KEY_F6)) { SaveParams(g); DumpParams(g); Toast(g, "PARAMS SAVED / DUMPED"); }
    if (IsKeyPressed(KEY_F9)) { BuildLevel(g); Toast(g, "LEVEL REBUILT"); }
    if (IsKeyPressed(KEY_R))  RespawnPlayer(g);
    if (IsKeyPressed(KEY_F12)) TakeScreenshot("shot.png");

    if (IsKeyPressed(KEY_PAGE_DOWN)) d.paramCursor = (d.paramCursor + 1) % kParamCount;
    if (IsKeyPressed(KEY_PAGE_UP))   d.paramCursor = (d.paramCursor + kParamCount - 1) % kParamCount;

    bool dec = IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT);
    bool inc = IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD);
    if (dec || inc) {
        const ParamEntry& e = kParamTable[d.paramCursor];
        float* v = ParamPtr(g.p, d.paramCursor);
        float fine = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) ? 0.2f : 1.0f;
        float delta = e.step * 4.0f * rdt * fine;   // 押しっぱなしで毎秒 4 ステップ
        *v += inc ? delta : -delta;
        if (*v < e.lo) *v = e.lo;
        if (*v > e.hi) *v = e.hi;
    }
}

// ────────────────────────────────────────────── HUD

static void DrawBar(int x, int y, int w, int h, float t, Color fill, const char* label) {
    DrawRectangle(x - 2, y - 2, w + 4, h + 4, Color{0, 0, 0, 110});
    DrawRectangle(x, y, w, h, Color{40, 44, 54, 220});
    DrawRectangle(x, y, (int)(w * Sat(t)), h, fill);
    DrawRectangleLines(x, y, w, h, Color{255, 255, 255, 90});
    if (label) DrawText(label, x, y - 18, 16, RAYWHITE);
}

void DrawHUD(Game& g) {
    const Player& pl = g.player;
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    int coinTotal = (int)g.level.coins.size();
    int botTotal  = (int)g.level.bots.size();

    DrawRectangle(0, 0, 300, 96, Color{0, 0, 0, 110});
    DrawText(TextFormat("BOTS  %d / %d", g.stats.botsSaved, botTotal), 14, 12, 22,
             g.stats.botsSaved == botTotal ? GOLD : RAYWHITE);
    DrawText(TextFormat("COINS %d / %d", g.stats.coinsTaken, coinTotal), 14, 38, 22, GOLD);
    DrawText(TextFormat("TIME  %5.1fs", g.time), 14, 64, 22, RAYWHITE);

    DrawBar(14, sh - 44, 220, 16, pl.hoverFuel / fmaxf(0.01f, g.p.hoverFuelMax),
            pl.hoverFuel > 0.35f ? Color{120, 220, 255, 255} : Color{255, 120, 90, 255},
            "HOVER FUEL");

    if (pl.ability.type != AbilityType::None) {
        DrawBar(260, sh - 44, 160, 16, AbilityFuelRatio(g),
                pl.ability.active ? Color{255, 230, 120, 255} : Color{240, 190, 80, 255},
                TextFormat("%s  (K / R2)", AbilityName(pl.ability.type)));
    }

    DrawText(TextFormat("%d FPS", GetFPS()), sw - 90, 12, 20, LIME);

    if (g.time < 16.0f) {
        const char* txt = g.in.usingPad
            ? "L-Stick: Move   X: Jump (hold = Hover)   SQUARE: Punch / Laser   R2: Ability"
            : "WASD: Move   SPACE: Jump (hold = Hover)   J: Punch / Laser   K: Ability";
        int w = MeasureText(txt, 20);
        unsigned char a = (unsigned char)(255 * Sat((16.0f - g.time) / 2.0f));
        DrawRectangle(sw / 2 - w / 2 - 12, sh - 92, w + 24, 32,
                      Color{0, 0, 0, (unsigned char)(a / 2)});
        DrawText(txt, sw / 2 - w / 2, sh - 84, 20, Color{255, 255, 255, a});
    }

    if (g.toastTimer > 0.0f) {
        unsigned char a = (unsigned char)(255 * Sat(g.toastTimer / 0.6f));
        int w = MeasureText(g.toast, 34);
        DrawText(g.toast, sw / 2 - w / 2, 120, 34, Color{120, 235, 255, a});
    }

    if (g.clearBanner > 0.0f) {
        const char* t = "STAGE CLEAR!";
        int w = MeasureText(t, 64);
        DrawRectangle(0, sh / 2 - 70, sw, 140, Color{0, 0, 0, 140});
        DrawText(t, sw / 2 - w / 2, sh / 2 - 46, 64, GOLD);
        const char* s = TextFormat("Bots %d/%d   Coins %d/%d   Enemies %d   Time %.1fs",
                                   g.stats.botsSaved, botTotal, g.stats.coinsTaken, coinTotal,
                                   g.stats.enemiesDefeated, g.time);
        int w2 = MeasureText(s, 24);
        DrawText(s, sw / 2 - w2 / 2, sh / 2 + 24, 24, RAYWHITE);
    }
}

// ────────────────────────────────────────────── 状態表示（F1）

void DrawDebugPanel(Game& g) {
    const Player& pl = g.player;
    int x = 14, y = 108;
    DrawRectangle(x - 10, y - 10, 430, 246, Color{0, 0, 0, 150});

    const char* state = pl.slide.active   ? "SLIDE"
                      : pl.ability.active ? "ABILITY"
                      : pl.hovering       ? "HOVER"
                      : (pl.grounded ? "GROUND" : (pl.vel.y > 0 ? "RISE" : "FALL"));
    DrawText(TextFormat("state      %s", state), x, y, 18, Color{160, 255, 190, 255}); y += 22;
    DrawText(TextFormat("pos     %6.1f %6.1f %6.1f", pl.pos.x, pl.pos.y, pl.pos.z), x, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("vel     %6.1f %6.1f %6.1f", pl.vel.x, pl.vel.y, pl.vel.z), x, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("speed(xz)  %5.2f", sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z)), x, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("hover %4.2f   coyote %5.3f", pl.hoverFuel, fmaxf(0.0f, pl.coyote)), x, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("ability %s  fuel %4.2f  %s", AbilityName(pl.ability.type),
                        pl.ability.value, pl.ability.active ? "ACTIVE" : ""), x, y, 18,
             Color{255, 230, 150, 255}); y += 22;
    DrawText(TextFormat("last reaction  %s", ReactionName((ReactionKind)g.debug.lastReaction)),
             x, y, 18, Color{200, 220, 255, 255}); y += 22;
    DrawText(TextFormat("broken %d  enemies %d  hits %d  dmg %d", g.stats.targetsBroken,
                        g.stats.enemiesDefeated, g.stats.buttonHits, g.stats.damageTaken),
             x, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("cp %d  particles %d  fx %d", pl.checkpoint,
                        (int)g.particles.size(), (int)g.effects.size()), x, y, 18, RAYWHITE); y += 22;
    if (pl.slide.active) {
        DrawText(TextFormat("slide  %.1f / %.1f m   speed %.1f   side %+.2f", pl.slide.dist,
                            g.level.slides[pl.slide.index].length, pl.slide.speed, pl.slide.side),
                 x, y, 18, Color{170, 235, 255, 255});
    } else {
        DrawText(TextFormat("rides %d", g.stats.slideRides), x, y, 18, RAYWHITE);
    }
    y += 22;
    DrawText("F1 info  F2 collision  F3 params  F4 EDITOR  R respawn  F9 rebuild", x, y, 15,
             Color{255, 220, 140, 255});
}

// ────────────────────────────────────────────── パラメータエディタ（F3）

void DrawParamEditor(Game& g) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int panelW = 430;
    int x = sw - panelW + 12, y = 44;
    DrawRectangle(sw - panelW, y - 12, panelW, sh - 60, Color{0, 0, 0, 175});

    DrawText(TextFormat("params: %s", g.paramsPath ? g.paramsPath : "(defaults)"), x, y, 15,
             Color{200, 200, 160, 255});
    y += 20;
    DrawText("PgUp/PgDn select   - / = adjust   Ctrl: fine", x, y, 15, Color{255, 220, 140, 255});
    y += 18;
    DrawText("F5 reload   F6 save to params.txt", x, y, 15, Color{255, 220, 140, 255});
    y += 24;

    const ParamEntry& sel = kParamTable[g.debug.paramCursor];
    DrawText(TextFormat("%s  [%g .. %g] step %g", sel.doc, sel.lo, sel.hi, sel.step), x, y, 15,
             Color{160, 220, 255, 255});
    y += 22;

    int rows = (sh - y - 30) / 20;
    if (rows < 4) rows = 4;
    int first = g.debug.paramCursor - rows / 2;
    if (first > kParamCount - rows) first = kParamCount - rows;
    if (first < 0) first = 0;
    int last = first + rows;
    if (last > kParamCount) last = kParamCount;

    for (int i = first; i < last; ++i) {
        float v = ParamGet(g.p, i);
        bool  isSel = (i == g.debug.paramCursor);
        if (isSel) DrawRectangle(x - 6, y - 2, panelW - 14, 20, Color{60, 90, 140, 210});
        // 既定値からどれだけ動かしたか分かるよう、変更済みは色を変える
        Color col = isSel ? Color{255, 240, 160, 255}
                          : (fabsf(v - kParamTable[i].def) > 1e-4f ? Color{150, 230, 190, 255}
                                                                  : Color{215, 220, 230, 255});
        DrawText(TextFormat("%-18s %9.3f", kParamTable[i].name, v), x, y, 17, col);
        y += 20;
    }
}
