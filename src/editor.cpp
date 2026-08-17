// editor.cpp ─ ゲーム内レベルエディタ（F4）
//
// 方針
//   ・遊びのコードには触らない。Level を書き換えるだけの層として独立させる。
//   ・編集モード中はゲームを止める（dt を回さない）。止まっているので、
//     移動床やゲートが動いていない「作者が置いた形」のまま触れる。
//   ・保存先は level.txt（level_io.cpp）。params.txt と同じ運用にする。
//
// 種類ごとの処理は switch で並べる。ここも ECS にはしない。編集できる物を
// 増やすときに直すのは、この下の 6 つの関数（Count / Bounds / GetPos /
// Translate / Fields / Add / Remove）だけになるように書いている。
#include "game.h"
#include "util.h"
#include <cstdio>
#include <cmath>
#include <cstring>

// ────────────────────────────────────────────── 種類ごとの入口

static const char* kEdTypeName[ED_TYPE_COUNT] = {
    "BOX", "PLATFORM", "COIN", "BOT", "TARGET", "SPRING",
    "BUTTON", "SLIDE", "ANCHOR", "ENEMY", "PICKUP", "CHECKPOINT", "GOAL",
};

static const char* kBoxKindName[10] = {
    "FLOOR", "STEP", "WALL", "MOVING", "SUBPATH", "GATE", "?", "?", "?", "SCENERY",
};

static const char* kPickupName[7] = {
    "None", "Dash", "Wire", "Rocket", "Mouse", "Sponge", "Monkey",
};

static int EdCount(const Level& l, int t) {
    switch (t) {
        case ED_BOX:        return (int)l.boxes.size();
        case ED_PLATFORM:   return (int)l.platforms.size();
        case ED_COIN:       return (int)l.coins.size();
        case ED_BOT:        return (int)l.bots.size();
        case ED_TARGET:     return (int)l.targets.size();
        case ED_SPRING:     return (int)l.springs.size();
        case ED_BUTTON:     return (int)l.buttons.size();
        case ED_SLIDE:      return (int)l.slides.size();
        case ED_ANCHOR:     return (int)l.anchors.size();
        case ED_ENEMY:      return (int)l.enemies.size();
        case ED_PICKUP:     return (int)l.pickups.size();
        case ED_CHECKPOINT: return (int)l.checkpoints.size();
        case ED_GOAL:       return 1;
        default:            return 0;
    }
}

// 選択中の「部分」の数。移動床と敵は A / B の2点、スライダーは制御点の数。
static int EdPartCount(const Level& l, int t, int i) {
    if (i < 0 || i >= EdCount(l, t)) return 1;
    switch (t) {
        case ED_PLATFORM:
        case ED_ENEMY:  return 3;                                  // 全体 / A / B
        case ED_BUTTON: return 3;                                  // ボタン / ゲート / 開いた位置
        case ED_SLIDE:  return 1 + (int)l.slides[i].ctrl.size();   // 全体 / 制御点
        default:        return 1;
    }
}

static const char* EdPartName(int t, int part) {
    if (part == 0) return "whole";
    switch (t) {
        case ED_PLATFORM:
        case ED_ENEMY:  return (part == 1) ? "A" : "B";
        case ED_BUTTON: return (part == 1) ? "gate(closed)" : "gate(open)";
        case ED_SLIDE:  return "ctrl";
        default:        return "whole";
    }
}

// 選択やマウスピックに使う箱。位置と、当たり判定に使う大きさを返す。
static bool EdBounds(const Level& l, int t, int i, Vector3* c, Vector3* h) {
    if (i < 0 || i >= EdCount(l, t)) return false;
    switch (t) {
        case ED_BOX:      *c = l.boxes[i].c;      *h = l.boxes[i].h;                  break;
        case ED_PLATFORM: {
            *c = l.platforms[i].a;
            int bi = l.platforms[i].boxIndex;
            *h = (bi >= 0 && bi < (int)l.boxes.size()) ? l.boxes[bi].h : Vector3{1.0f, 0.5f, 1.0f};
            break;
        }
        case ED_COIN:     *c = l.coins[i].pos;    *h = Vector3{0.35f, 0.35f, 0.35f};  break;
        case ED_BOT:      *c = l.bots[i].pos;     *h = Vector3{0.40f, 0.40f, 0.40f};  break;
        case ED_TARGET:   *c = l.targets[i].home; *h = l.targets[i].half;             break;
        case ED_SPRING:   *c = l.springs[i].pos;  *h = l.springs[i].half;             break;
        case ED_BUTTON:   *c = l.buttons[i].pos;  *h = l.buttons[i].half;             break;
        case ED_SLIDE: {
            const WaterSlide& s = l.slides[i];
            if (s.ctrl.empty()) return false;
            *c = s.ctrl[0];
            *h = Vector3{s.radius, s.radius, s.radius};
            break;
        }
        case ED_ANCHOR:     *c = l.anchors[i].pos; *h = Vector3{0.45f, 0.45f, 0.45f}; break;
        case ED_ENEMY:      *c = l.enemies[i].a;   *h = l.enemies[i].half;            break;
        case ED_PICKUP:     *c = l.pickups[i].pos; *h = Vector3{0.45f, 0.45f, 0.45f}; break;
        case ED_CHECKPOINT: *c = l.checkpoints[i]; *h = Vector3{0.55f, 1.10f, 0.55f}; break;
        case ED_GOAL:       *c = l.goal;           *h = Vector3{0.80f, 0.80f, 0.80f}; break;
        default: return false;
    }
    return true;
}

static Vector3 EdGetPos(const Level& l, int t, int i, int part) {
    if (i < 0 || i >= EdCount(l, t)) return Vector3{0, 0, 0};
    switch (t) {
        case ED_PLATFORM:
            if (part == 2) return l.platforms[i].b;
            return l.platforms[i].a;
        case ED_ENEMY:
            if (part == 2) return l.enemies[i].b;
            return l.enemies[i].a;
        case ED_BUTTON:
            if (part == 1) return l.buttons[i].gateClosed;
            if (part == 2) return l.buttons[i].gateOpen;
            return l.buttons[i].pos;
        case ED_SLIDE: {
            const WaterSlide& s = l.slides[i];
            if (s.ctrl.empty()) return Vector3{0, 0, 0};
            if (part >= 1 && part - 1 < (int)s.ctrl.size()) return s.ctrl[part - 1];
            return s.ctrl[0];
        }
        default: {
            Vector3 c, h;
            if (EdBounds(l, t, i, &c, &h)) return c;
            return Vector3{0, 0, 0};
        }
    }
}

// 箱を動かすとき、それを持ち主にしている物（移動床・ゲート）も一緒に動かす。
// 「箱として掴んでも、移動床として掴んでも同じ結果になる」ようにするため。
static void MoveBoxAndOwners(Level& l, int boxIndex, Vector3 d) {
    if (boxIndex < 0 || boxIndex >= (int)l.boxes.size()) return;
    l.boxes[boxIndex].c = Vector3Add(l.boxes[boxIndex].c, d);
    for (MovingPlatform& pf : l.platforms) {
        if (pf.boxIndex != boxIndex) continue;
        pf.a = Vector3Add(pf.a, d);
        pf.b = Vector3Add(pf.b, d);
    }
    for (Button& b : l.buttons) {
        if (b.gateBoxIndex != boxIndex) continue;
        b.gateClosed = Vector3Add(b.gateClosed, d);
        b.gateOpen   = Vector3Add(b.gateOpen, d);
    }
    for (WireAnchor& a : l.anchors) {
        if (a.boxIndex != boxIndex) continue;
        a.pos = Vector3Add(a.pos, d);
    }
}

static void EdTranslate(Level& l, int t, int i, int part, Vector3 d) {
    if (i < 0 || i >= EdCount(l, t)) return;
    switch (t) {
        case ED_BOX:
            MoveBoxAndOwners(l, i, d);
            break;
        case ED_PLATFORM: {
            MovingPlatform& pf = l.platforms[i];
            if (part == 2) { pf.b = Vector3Add(pf.b, d); break; }
            if (part == 1) {
                pf.a = Vector3Add(pf.a, d);
                if (pf.boxIndex >= 0) l.boxes[pf.boxIndex].c = pf.a;
                break;
            }
            pf.a = Vector3Add(pf.a, d);
            pf.b = Vector3Add(pf.b, d);
            if (pf.boxIndex >= 0) l.boxes[pf.boxIndex].c = pf.a;
            break;
        }
        case ED_COIN:   l.coins[i].pos   = Vector3Add(l.coins[i].pos, d);   break;
        case ED_BOT:    l.bots[i].pos    = Vector3Add(l.bots[i].pos, d);    break;
        case ED_TARGET: l.targets[i].home = Vector3Add(l.targets[i].home, d);
                        l.targets[i].pos  = l.targets[i].home;              break;
        case ED_SPRING: l.springs[i].pos = Vector3Add(l.springs[i].pos, d); break;
        case ED_BUTTON: {
            Button& b = l.buttons[i];
            if (part == 1) {
                b.gateClosed = Vector3Add(b.gateClosed, d);
                b.gateOpen   = Vector3Add(b.gateOpen, d);
                if (b.gateBoxIndex >= 0) l.boxes[b.gateBoxIndex].c = b.gateClosed;
            } else if (part == 2) {
                b.gateOpen = Vector3Add(b.gateOpen, d);
            } else {
                b.pos = Vector3Add(b.pos, d);
            }
            break;
        }
        case ED_SLIDE: {
            WaterSlide& s = l.slides[i];
            if (part >= 1 && part - 1 < (int)s.ctrl.size()) {
                s.ctrl[part - 1] = Vector3Add(s.ctrl[part - 1], d);
            } else {
                for (Vector3& p : s.ctrl) p = Vector3Add(p, d);
            }
            RebuildSlide(s);
            break;
        }
        case ED_ANCHOR: {
            WireAnchor& a = l.anchors[i];
            a.pos = Vector3Add(a.pos, d);
            // 引っ張れる岩は輪と一緒に動かす（別々に置けても混乱するだけなので）
            if (a.boxIndex >= 0 && a.boxIndex < (int)l.boxes.size())
                l.boxes[a.boxIndex].c = Vector3Add(l.boxes[a.boxIndex].c, d);
            break;
        }
        case ED_ENEMY: {
            Enemy& e = l.enemies[i];
            if (part == 2)      e.b = Vector3Add(e.b, d);
            else if (part == 1) e.a = Vector3Add(e.a, d);
            else { e.a = Vector3Add(e.a, d); e.b = Vector3Add(e.b, d); }
            e.pos = Vector3Lerp(e.a, e.b, e.t);
            break;
        }
        case ED_PICKUP:     l.pickups[i].pos = Vector3Add(l.pickups[i].pos, d);      break;
        case ED_CHECKPOINT: l.checkpoints[i] = Vector3Add(l.checkpoints[i], d);      break;
        case ED_GOAL:       l.goal           = Vector3Add(l.goal, d);                break;
        default: break;
    }
}

// 大きさを変えられるのは箱とターゲットだけ。他は形を持たない点として扱う。
static Vector3* EdHalfPtr(Level& l, int t, int i) {
    if (i < 0 || i >= EdCount(l, t)) return nullptr;
    switch (t) {
        case ED_BOX:      return &l.boxes[i].h;
        case ED_TARGET:   return &l.targets[i].half;
        case ED_PLATFORM: return (l.platforms[i].boxIndex >= 0)
                               ? &l.boxes[l.platforms[i].boxIndex].h : nullptr;
        case ED_BUTTON:   return (l.buttons[i].gateBoxIndex >= 0)
                               ? &l.boxes[l.buttons[i].gateBoxIndex].h : nullptr;
        default:          return nullptr;
    }
}

// ────────────────────────────────────────────── 種類ごとの数値フィールド
//
// 「位置と大きさ以外」をここに集める。- / = で増減、, / . で選ぶ。
struct EdField {
    const char* name;
    float value;
    float step;
    float lo, hi;
    bool  isInt;
};

static int EdFields(const Level& l, int t, int i, EdField* out, int maxN) {
    int n = 0;
    if (i < 0 || i >= EdCount(l, t)) return 0;
    auto put = [&](const char* nm, float v, float st, float lo, float hi, bool ii) {
        if (n < maxN) out[n++] = EdField{nm, v, st, lo, hi, ii};
    };
    switch (t) {
        case ED_BOX:
            put("kind", (float)l.boxes[i].kind, 1.0f, 0.0f, 9.0f, true);
            break;
        case ED_PLATFORM:
            put("speed", l.platforms[i].speed, 0.25f, 0.0f, 30.0f, false);
            break;
        case ED_TARGET:
            put("floating", l.targets[i].floating ? 1.0f : 0.0f, 1.0f, 0.0f, 1.0f, true);
            put("hp",       (float)l.targets[i].hp, 1.0f, 1.0f, 9.0f, true);
            break;
        case ED_SPRING:
            put("power", l.springs[i].power, 0.5f, 0.0f, 40.0f, false);
            break;
        case ED_SLIDE:
            put("radius",    l.slides[i].radius,    0.1f, 0.4f, 6.0f, false);
            put("exitBoost", l.slides[i].exitBoost, 0.1f, 0.0f, 5.0f, false);
            put("sub",       (float)l.slides[i].sub, 1.0f, 1.0f, 32.0f, true);
            break;
        case ED_ANCHOR:
            put("kind(0=fix,1=heavy)", (float)l.anchors[i].kind, 1.0f, 0.0f, 1.0f, true);
            break;
        case ED_ENEMY:
            put("speed", l.enemies[i].speed, 0.25f, 0.0f, 20.0f, false);
            break;
        case ED_PICKUP:
            put("ability", (float)(int)l.pickups[i].type, 1.0f, 0.0f, 6.0f, true);
            break;
        default: break;
    }
    return n;
}

static void EdSetField(Level& l, int t, int i, int f, float v) {
    if (i < 0 || i >= EdCount(l, t)) return;
    switch (t) {
        case ED_BOX:      if (f == 0) l.boxes[i].kind = (int)v; break;
        case ED_PLATFORM: if (f == 0) l.platforms[i].speed = v; break;
        case ED_TARGET:
            if (f == 0) { l.targets[i].floating = (v != 0.0f);
                          l.targets[i].hp = l.targets[i].floating ? 2 : 1; }
            if (f == 1) l.targets[i].hp = (int)v;
            break;
        case ED_SPRING:   if (f == 0) l.springs[i].power = v; break;
        case ED_SLIDE:
            if (f == 0) l.slides[i].radius = v;
            if (f == 1) l.slides[i].exitBoost = v;
            if (f == 2) { l.slides[i].sub = (int)v; RebuildSlide(l.slides[i]); }
            break;
        case ED_ANCHOR:   if (f == 0) l.anchors[i].kind = (int)v; break;
        case ED_ENEMY:    if (f == 0) l.enemies[i].speed = v; break;
        case ED_PICKUP:   if (f == 0) l.pickups[i].type = (AbilityType)(int)v; break;
        default: break;
    }
}

// ────────────────────────────────────────────── 追加 / 削除

// 箱を消すと添字がずれる。参照している側（移動床・ゲート・岩）をここで直す。
// 直しきれないもの（持ち主を失った移動床）は道連れで消す。
static void RemoveBoxAt(Level& l, int idx) {
    if (idx < 0 || idx >= (int)l.boxes.size()) return;
    l.boxes.erase(l.boxes.begin() + idx);

    for (int i = (int)l.platforms.size() - 1; i >= 0; --i) {
        int& bi = l.platforms[i].boxIndex;
        if (bi == idx)      l.platforms.erase(l.platforms.begin() + i);
        else if (bi > idx)  --bi;
    }
    for (Button& b : l.buttons) {
        if (b.gateBoxIndex == idx)     b.gateBoxIndex = -1;
        else if (b.gateBoxIndex > idx) --b.gateBoxIndex;
    }
    for (WireAnchor& a : l.anchors) {
        if (a.boxIndex == idx)     { a.boxIndex = -1; a.kind = ANCHOR_FIXED; }
        else if (a.boxIndex > idx) --a.boxIndex;
    }
}

static void EdRemove(Level& l, int t, int i) {
    if (i < 0 || i >= EdCount(l, t)) return;
    switch (t) {
        case ED_BOX:      RemoveBoxAt(l, i); break;
        case ED_PLATFORM: {
            int bi = l.platforms[i].boxIndex;
            l.platforms.erase(l.platforms.begin() + i);
            if (bi >= 0) RemoveBoxAt(l, bi);      // 足場の箱も一緒に消す
            break;
        }
        case ED_BUTTON: {
            int bi = l.buttons[i].gateBoxIndex;
            l.buttons.erase(l.buttons.begin() + i);
            if (bi >= 0) RemoveBoxAt(l, bi);      // ゲートの箱も一緒に消す
            break;
        }
        case ED_COIN:       l.coins.erase(l.coins.begin() + i);             break;
        case ED_BOT:        l.bots.erase(l.bots.begin() + i);               break;
        case ED_TARGET:     l.targets.erase(l.targets.begin() + i);         break;
        case ED_SPRING:     l.springs.erase(l.springs.begin() + i);         break;
        case ED_SLIDE:      l.slides.erase(l.slides.begin() + i);           break;
        case ED_ANCHOR:     l.anchors.erase(l.anchors.begin() + i);         break;
        case ED_ENEMY:      l.enemies.erase(l.enemies.begin() + i);         break;
        case ED_PICKUP:     l.pickups.erase(l.pickups.begin() + i);         break;
        case ED_CHECKPOINT:
            if (l.checkpoints.size() > 1) l.checkpoints.erase(l.checkpoints.begin() + i);
            break;
        case ED_GOAL: break;    // ゴールは常に1つ。消せない
        default: break;
    }
}

// 新しく1つ置く。戻り値は新しい添字（置けなければ -1）。
static int EdAdd(Game& g, int t, Vector3 p) {
    Level& l = g.level;
    switch (t) {
        case ED_BOX: {
            Vector3 h{1.0f, 0.5f, 1.0f};
            l.boxes.push_back(Box{Vector3{p.x, p.y + h.y, p.z}, h, g.editor.boxKind, true});
            return (int)l.boxes.size() - 1;
        }
        case ED_PLATFORM: {
            Vector3 h{2.0f, 0.30f, 2.0f};
            Vector3 a{p.x, p.y + h.y + 0.6f, p.z};
            Vector3 b = Vector3Add(a, Vector3{6.0f, 0, 0});
            l.boxes.push_back(Box{a, h, BOX_MOVING, true});
            MovingPlatform pf{};
            pf.boxIndex = (int)l.boxes.size() - 1;
            pf.a = a; pf.b = b; pf.speed = 3.0f;
            pf.t = 0.0f; pf.dir = 1; pf.delta = Vector3{0, 0, 0};
            l.platforms.push_back(pf);
            return (int)l.platforms.size() - 1;
        }
        case ED_COIN: {
            Coin c; c.pos = Vector3{p.x, p.y + 0.8f, p.z};
            l.coins.push_back(c);
            return (int)l.coins.size() - 1;
        }
        case ED_BOT:
            l.bots.push_back(Bot{{p.x, p.y + 0.8f, p.z}});
            return (int)l.bots.size() - 1;
        case ED_TARGET: {
            Target tg;
            tg.pos = tg.home = Vector3{p.x, p.y + 0.5f, p.z};
            tg.vel = Vector3{0, 0, 0};
            tg.half = Vector3{0.5f, 0.5f, 0.5f};
            tg.hp = 1;
            l.targets.push_back(tg);
            return (int)l.targets.size() - 1;
        }
        case ED_SPRING: {
            Spring s;
            s.pos  = Vector3{p.x, p.y + 0.25f, p.z};
            s.half = Vector3{0.9f, 0.25f, 0.9f};
            l.springs.push_back(s);
            return (int)l.springs.size() - 1;
        }
        case ED_BUTTON: {
            Vector3 gateHalf{1.6f, 0.9f, 0.4f};
            Vector3 gateC = Vector3{p.x + 5.0f, p.y + gateHalf.y, p.z};
            l.boxes.push_back(Box{gateC, gateHalf, BOX_GATE, true});
            Button b;
            b.pos  = Vector3{p.x, p.y + 0.15f, p.z};
            b.half = Vector3{0.75f, 0.15f, 0.75f};
            b.gateBoxIndex = (int)l.boxes.size() - 1;
            b.gateClosed = gateC;
            b.gateOpen   = Vector3Add(gateC, Vector3{0, -2.2f, 0});
            l.buttons.push_back(b);
            return (int)l.buttons.size() - 1;
        }
        case ED_SLIDE: {
            WaterSlide s;
            s.radius = 1.6f;
            s.sub    = 8;
            s.ctrl.push_back(Vector3{p.x, p.y + 5.0f, p.z});
            s.ctrl.push_back(Vector3{p.x, p.y + 3.5f, p.z + 8.0f});
            s.ctrl.push_back(Vector3{p.x, p.y + 2.0f, p.z + 16.0f});
            RebuildSlide(s);
            l.slides.push_back(s);
            return (int)l.slides.size() - 1;
        }
        case ED_ANCHOR: {
            WireAnchor a;
            a.pos = Vector3{p.x, p.y + 6.0f, p.z};
            a.kind = ANCHOR_FIXED;
            l.anchors.push_back(a);
            return (int)l.anchors.size() - 1;
        }
        case ED_ENEMY: {
            Enemy e;
            e.a = Vector3{p.x, p.y + 0.45f, p.z};
            e.b = Vector3Add(e.a, Vector3{0, 0, 6.0f});
            e.pos = e.a;
            e.half = Vector3{0.45f, 0.45f, 0.45f};
            e.t = 0.0f; e.dir = 1;
            l.enemies.push_back(e);
            return (int)l.enemies.size() - 1;
        }
        case ED_PICKUP: {
            AbilityPickup a;
            a.pos = Vector3{p.x, p.y + 1.1f, p.z};
            a.type = (AbilityType)g.editor.pickupKind;
            l.pickups.push_back(a);
            return (int)l.pickups.size() - 1;
        }
        case ED_CHECKPOINT:
            l.checkpoints.push_back(Vector3{p.x, p.y + 1.2f, p.z});
            return (int)l.checkpoints.size() - 1;
        case ED_GOAL:
            l.goal = Vector3{p.x, p.y + 0.2f, p.z};
            return 0;
        default: return -1;
    }
}

// 選択中の物をそのままコピーする。位置は少しずらす（重ねると選び直せないため）。
static int EdDuplicate(Game& g, int t, int i) {
    Level& l = g.level;
    if (i < 0 || i >= EdCount(l, t)) return -1;
    int ni = -1;
    switch (t) {
        case ED_BOX:
            l.boxes.push_back(l.boxes[i]);
            ni = (int)l.boxes.size() - 1;
            break;
        case ED_PLATFORM: {
            MovingPlatform pf = l.platforms[i];
            if (pf.boxIndex >= 0) {
                l.boxes.push_back(l.boxes[pf.boxIndex]);
                pf.boxIndex = (int)l.boxes.size() - 1;
            }
            l.platforms.push_back(pf);
            ni = (int)l.platforms.size() - 1;
            break;
        }
        case ED_BUTTON: {
            Button b = l.buttons[i];
            if (b.gateBoxIndex >= 0) {
                l.boxes.push_back(l.boxes[b.gateBoxIndex]);
                b.gateBoxIndex = (int)l.boxes.size() - 1;
            }
            b.openTimer = 0.0f; b.openRatio = 0.0f; b.press = 0.0f;
            l.buttons.push_back(b);
            ni = (int)l.buttons.size() - 1;
            break;
        }
        case ED_COIN:   l.coins.push_back(l.coins[i]);     ni = (int)l.coins.size() - 1;   break;
        case ED_BOT:    l.bots.push_back(l.bots[i]);       ni = (int)l.bots.size() - 1;    break;
        case ED_TARGET: l.targets.push_back(l.targets[i]); ni = (int)l.targets.size() - 1; break;
        case ED_SPRING: l.springs.push_back(l.springs[i]); ni = (int)l.springs.size() - 1; break;
        case ED_SLIDE:  l.slides.push_back(l.slides[i]);   ni = (int)l.slides.size() - 1;  break;
        case ED_ANCHOR: {
            WireAnchor a = l.anchors[i];
            a.boxIndex = -1;              // 岩は共有しない。コピーは固定アンカーにする
            a.kind = ANCHOR_FIXED;
            l.anchors.push_back(a);
            ni = (int)l.anchors.size() - 1;
            break;
        }
        case ED_ENEMY:  l.enemies.push_back(l.enemies[i]); ni = (int)l.enemies.size() - 1; break;
        case ED_PICKUP: l.pickups.push_back(l.pickups[i]); ni = (int)l.pickups.size() - 1; break;
        case ED_CHECKPOINT:
            l.checkpoints.push_back(l.checkpoints[i]);
            ni = (int)l.checkpoints.size() - 1;
            break;
        default: return -1;
    }
    if (ni >= 0) EdTranslate(l, t, ni, 0, Vector3{2.0f, 0, 0});
    return ni;
}

// ────────────────────────────────────────────── カメラ / マウス

static void BuildEditorCamera(Game& g) {
    EditorState& e = g.editor;
    float cp = cosf(e.camPitch), sp = sinf(e.camPitch);
    Vector3 offset{-sinf(e.camYaw) * cp, sp, -cosf(e.camYaw) * cp};
    g.rig.cam.position   = Vector3Add(e.camTarget, Vector3Scale(offset, e.camDist));
    g.rig.cam.target     = e.camTarget;
    g.rig.cam.up         = Vector3{0, 1, 0};
    g.rig.cam.fovy       = 55.0f;
    g.rig.cam.projection = CAMERA_PERSPECTIVE;
}

static Vector3 CamForwardFlat(const EditorState& e) {
    return Vector3{sinf(e.camYaw), 0.0f, cosf(e.camYaw)};
}
static Vector3 CamRightFlat(const EditorState& e) {
    return Vector3{-cosf(e.camYaw), 0.0f, sinf(e.camYaw)};
}

static void UpdateEditorCamera(Game& g, float dt) {
    EditorState& e = g.editor;

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        e.camYaw   -= d.x * 0.006f;
        e.camPitch += d.y * 0.005f;
        e.camPitch  = Clamp(e.camPitch, -0.30f, 1.45f);
    }

    float wheel = GetMouseWheelMove();
    if (fabsf(wheel) > 0.0f) e.camDist = Clamp(e.camDist * (1.0f - wheel * 0.12f), 3.0f, 180.0f);

    // 平行移動。速度は距離に比例させる（引いているときほど速く動く）
    float sp = e.camDist * 0.9f * dt * (IsKeyDown(KEY_LEFT_SHIFT) ? 3.0f : 1.0f);
    Vector3 f = CamForwardFlat(e), r = CamRightFlat(e);
    if (IsKeyDown(KEY_W)) e.camTarget = Vector3Add(e.camTarget, Vector3Scale(f, sp));
    if (IsKeyDown(KEY_S)) e.camTarget = Vector3Add(e.camTarget, Vector3Scale(f, -sp));
    if (IsKeyDown(KEY_D)) e.camTarget = Vector3Add(e.camTarget, Vector3Scale(r, sp));
    if (IsKeyDown(KEY_A)) e.camTarget = Vector3Add(e.camTarget, Vector3Scale(r, -sp));
    if (IsKeyDown(KEY_E)) e.camTarget.y += sp;
    if (IsKeyDown(KEY_Q)) e.camTarget.y -= sp;

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 d = GetMouseDelta();
        float k = e.camDist * 0.0016f;
        e.camTarget = Vector3Add(e.camTarget, Vector3Scale(r, -d.x * k));
        e.camTarget.y += d.y * k;
    }

    BuildEditorCamera(g);
}

// マウスの下にある「置ける場所」。箱の上に当たればその点、外れたら y=0 の床。
static Vector3 MouseGroundPoint(const Game& g, bool* hitSomething) {
    Ray ray = GetScreenToWorldRay(GetMousePosition(), g.rig.cam);
    float best = 1e18f;
    Vector3 point{0, 0, 0};
    bool hit = false;

    for (const Box& b : g.level.boxes) {
        BoundingBox bb{Vector3Subtract(b.c, b.h), Vector3Add(b.c, b.h)};
        RayCollision rc = GetRayCollisionBox(ray, bb);
        if (rc.hit && rc.distance < best) { best = rc.distance; point = rc.point; hit = true; }
    }
    if (!hit && fabsf(ray.direction.y) > 1e-4f) {
        float t = -ray.position.y / ray.direction.y;
        if (t > 0.0f) {
            point = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
            hit = true;
        }
    }
    if (hitSomething) *hitSomething = hit;
    return point;
}

// クリックで選ぶ。全種類を総当たりする（数百個の規模なので十分速い）。
static bool PickAtMouse(Game& g, int* outType, int* outIndex) {
    Ray ray = GetScreenToWorldRay(GetMousePosition(), g.rig.cam);
    float best = 1e18f;
    bool  found = false;

    for (int t = 0; t < ED_TYPE_COUNT; ++t) {
        int n = EdCount(g.level, t);
        for (int i = 0; i < n; ++i) {
            Vector3 c, h;
            if (!EdBounds(g.level, t, i, &c, &h)) continue;
            // 点物は掴みやすいように少し大きめの判定にする
            Vector3 hh = Vector3{fmaxf(h.x, 0.3f), fmaxf(h.y, 0.3f), fmaxf(h.z, 0.3f)};
            BoundingBox bb{Vector3Subtract(c, hh), Vector3Add(c, hh)};
            RayCollision rc = GetRayCollisionBox(ray, bb);
            if (!rc.hit || rc.distance >= best) continue;
            // 地形の箱は「他の物の下敷き」になりやすいので、同距離なら他を優先する
            best = rc.distance - ((t == ED_BOX) ? 0.0f : 0.001f);
            *outType = t; *outIndex = i;
            found = true;
        }
    }
    return found;
}

// ────────────────────────────────────────────── 入力

static Vector3 SnapV(Vector3 v, float grid) {
    if (grid <= 0.0f) return v;
    return Vector3{roundf(v.x / grid) * grid, roundf(v.y / grid) * grid,
                   roundf(v.z / grid) * grid};
}

static bool Tapped(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

static void ClampSelection(Game& g) {
    EditorState& e = g.editor;
    int n = EdCount(g.level, e.type);
    if (n <= 0) { e.index = -1; e.part = 0; e.field = 0; return; }
    if (e.index >= n) e.index = n - 1;
    int pc = EdPartCount(g.level, e.type, e.index);
    if (e.part >= pc) e.part = 0;
    EdField fs[8];
    int fn = EdFields(g.level, e.type, e.index, fs, 8);
    if (e.field >= fn) e.field = 0;
}

void ToggleEditor(Game& g) {
    EditorState& e = g.editor;
    e.on = !e.on;
    if (e.on) {
        // プレイヤーの周りから始める（今見ている場所をそのまま引き継ぐ）
        e.camTarget = g.player.pos;
        e.camYaw    = g.rig.yaw;
        e.camPitch  = 0.55f;
        e.dirty     = false;
        ClampSelection(g);
        Toast(g, "EDITOR ON");
    } else {
        // 地形が変わっているなら、静的な物理世界を作り直してから遊びに戻す
        if (e.dirty) { ResetWorldPhysics(g); e.dirty = false; }
        Toast(g, "EDITOR OFF");
    }
}

void UpdateEditor(Game& g, float rdt) {
    EditorState& e = g.editor;
    Level&       l = g.level;

    UpdateEditorCamera(g, rdt);
    ClampSelection(g);

    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);

    // ── 選択
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int t = 0, i = 0;
        if (PickAtMouse(g, &t, &i)) {
            if (e.type != t || e.index != i) { e.part = 0; e.field = 0; }
            e.type = t; e.index = i;
        } else {
            e.index = -1;
        }
    }
    if (Tapped(KEY_TAB)) {
        int n = EdCount(l, e.type);
        if (n > 0) {
            e.index = shift ? (e.index - 1 + n) % n : (e.index + 1) % n;
            e.part = 0; e.field = 0;
        }
    }
    if (Tapped(KEY_RIGHT_BRACKET)) { e.type = (e.type + 1) % ED_TYPE_COUNT; e.index = -1; e.part = 0; e.field = 0; }
    if (Tapped(KEY_LEFT_BRACKET))  { e.type = (e.type + ED_TYPE_COUNT - 1) % ED_TYPE_COUNT; e.index = -1; e.part = 0; e.field = 0; }
    if (IsKeyPressed(KEY_T)) {
        int pc = EdPartCount(l, e.type, e.index);
        e.part = (e.part + 1) % pc;
    }
    if (IsKeyPressed(KEY_F)) {   // 選択中の物へカメラを寄せる
        Vector3 c, h;
        if (EdBounds(l, e.type, e.index, &c, &h)) e.camTarget = c;
    }
    if (IsKeyPressed(KEY_H)) e.showHelp = !e.showHelp;
    if (IsKeyPressed(KEY_G)) { e.snap = !e.snap; Toast(g, e.snap ? "SNAP ON" : "SNAP OFF"); }
    if (Tapped(KEY_LEFT_ALT)) {}   // 予約（何もしない）

    // グリッド幅：1 / 2 キーで 0.1 → 0.25 → 0.5 → 1.0 → 2.0 を行き来する
    static const float kGrids[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f};
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_TWO)) {
        int gi = 2;
        for (int k = 0; k < 5; ++k) if (fabsf(kGrids[k] - e.grid) < 1e-4f) gi = k;
        gi += IsKeyPressed(KEY_TWO) ? 1 : -1;
        if (gi < 0) gi = 0;
        if (gi > 4) gi = 4;
        e.grid = kGrids[gi];
    }

    // ── 追加 / 複製 / 削除
    if (IsKeyPressed(KEY_N)) {
        bool hit = false;
        Vector3 p = MouseGroundPoint(g, &hit);
        if (e.snap) p = SnapV(p, e.grid);
        int ni = EdAdd(g, e.type, p);
        if (ni >= 0) { e.index = ni; e.part = 0; e.field = 0; e.dirty = true; }
        Toast(g, TextFormat("ADD %s", kEdTypeName[e.type]));
    }
    if (IsKeyPressed(KEY_C) && e.index >= 0) {
        int ni = EdDuplicate(g, e.type, e.index);
        if (ni >= 0) { e.index = ni; e.part = 0; e.dirty = true; }
        Toast(g, "DUPLICATE");
    }
    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_X)) && e.index >= 0) {
        EdRemove(l, e.type, e.index);
        e.index = -1; e.part = 0; e.field = 0; e.dirty = true;
        Toast(g, "DELETE");
    }
    if (IsKeyPressed(KEY_B)) {   // 置く箱の種類を切り替える（選択中の箱にも反映）
        static const int kKinds[] = {BOX_FLOOR, BOX_STEP, BOX_WALL, BOX_SUBPATH, BOX_SCENERY};
        int ki = 0;
        for (int k = 0; k < 5; ++k) if (kKinds[k] == e.boxKind) ki = k;
        e.boxKind = kKinds[(ki + 1) % 5];
        if (e.type == ED_BOX && e.index >= 0) { l.boxes[e.index].kind = e.boxKind; e.dirty = true; }
        Toast(g, TextFormat("BOX KIND: %s", kBoxKindName[e.boxKind]));
    }

    // ── スライダーの制御点を増やす / 減らす
    if (e.type == ED_SLIDE && e.index >= 0 && !l.slides.empty()) {
        WaterSlide& s = l.slides[e.index];
        if (IsKeyPressed(KEY_I) && e.part >= 1) {
            int at = e.part - 1;
            Vector3 a = s.ctrl[at];
            Vector3 b = (at + 1 < (int)s.ctrl.size()) ? s.ctrl[at + 1]
                                                      : Vector3Add(a, Vector3{0, -1, 4});
            s.ctrl.insert(s.ctrl.begin() + at + 1, Vector3Lerp(a, b, 0.5f));
            RebuildSlide(s);
            e.part = at + 2;
            Toast(g, "CTRL POINT INSERTED");
        }
        if (IsKeyPressed(KEY_R) && e.part >= 1 && s.ctrl.size() > 2) {
            s.ctrl.erase(s.ctrl.begin() + (e.part - 1));
            RebuildSlide(s);
            if (e.part > (int)s.ctrl.size()) e.part = (int)s.ctrl.size();
            Toast(g, "CTRL POINT REMOVED");
        }
    }

    // ── 移動 / 大きさ（矢印 + PageUp/PageDown。Shift で大きさ、Ctrl で細かく）
    {
        float step = e.grid * (ctrl ? 0.2f : 1.0f);
        Vector3 f = CamForwardFlat(e), r = CamRightFlat(e);
        // 見た目に近い軸へ丸める（斜めから見ていても矢印の向きが素直になる）
        auto axis = [](Vector3 v) {
            return (fabsf(v.x) > fabsf(v.z)) ? Vector3{(v.x > 0 ? 1.0f : -1.0f), 0, 0}
                                             : Vector3{0, 0, (v.z > 0 ? 1.0f : -1.0f)};
        };
        f = axis(f); r = axis(r);

        Vector3 d{0, 0, 0};
        if (Tapped(KEY_UP))    d = Vector3Add(d, f);
        if (Tapped(KEY_DOWN))  d = Vector3Subtract(d, f);
        if (Tapped(KEY_RIGHT)) d = Vector3Add(d, r);
        if (Tapped(KEY_LEFT))  d = Vector3Subtract(d, r);
        if (Tapped(KEY_PAGE_UP))   d.y += 1.0f;
        if (Tapped(KEY_PAGE_DOWN)) d.y -= 1.0f;

        if ((fabsf(d.x) + fabsf(d.y) + fabsf(d.z)) > 0.0f && e.index >= 0) {
            if (shift) {
                Vector3* h = EdHalfPtr(l, e.type, e.index);
                if (h) {
                    h->x = fmaxf(0.05f, h->x + d.x * step);
                    h->y = fmaxf(0.05f, h->y + d.y * step);
                    h->z = fmaxf(0.05f, h->z + d.z * step);
                    e.dirty = true;
                }
            } else {
                Vector3 cur  = EdGetPos(l, e.type, e.index, e.part);
                Vector3 want = Vector3Add(cur, Vector3Scale(d, step));
                if (e.snap) want = SnapV(want, e.grid);
                EdTranslate(l, e.type, e.index, e.part, Vector3Subtract(want, cur));
                e.dirty = true;
            }
        }
    }

    // ── 数値フィールド（, / . で選び、- / = で増減）
    {
        EdField fs[8];
        int fn = EdFields(l, e.type, e.index, fs, 8);
        if (fn > 0) {
            if (Tapped(KEY_COMMA))  e.field = (e.field + fn - 1) % fn;
            if (Tapped(KEY_PERIOD)) e.field = (e.field + 1) % fn;
            if (e.field >= fn) e.field = 0;

            bool dec = Tapped(KEY_MINUS) || Tapped(KEY_KP_SUBTRACT);
            bool inc = Tapped(KEY_EQUAL) || Tapped(KEY_KP_ADD);
            if (dec || inc) {
                const EdField& fd = fs[e.field];
                float st = fd.isInt ? fd.step : (fd.step * (ctrl ? 0.2f : 1.0f));
                float v  = fd.value + (inc ? st : -st);
                v = Clamp(v, fd.lo, fd.hi);
                if (fd.isInt) v = roundf(v);
                EdSetField(l, e.type, e.index, e.field, v);
                e.dirty = true;
            }
        }
    }

    // ── プレイヤーをマウス位置へ（そのまま F4 で試遊できる）
    if (IsKeyPressed(KEY_P)) {
        bool hit = false;
        Vector3 p = MouseGroundPoint(g, &hit);
        if (hit) {
            g.player.pos = Vector3{p.x, p.y + g.player.half.y + 0.05f, p.z};
            g.player.vel = Vector3{0, 0, 0};
            Toast(g, "PLAYER MOVED");
        }
    }

    // ── ファイル操作
    if (IsKeyPressed(KEY_F5)) {
        if (LoadLevel(g, nullptr)) { e.index = -1; e.dirty = true; Toast(g, "LEVEL LOADED"); }
        else                        Toast(g, "level.txt NOT FOUND");
    }
    if (IsKeyPressed(KEY_F6)) {
        if (SaveLevel(g, nullptr)) Toast(g, "LEVEL SAVED");
        else                       Toast(g, "SAVE FAILED (see console)");
    }
    if (IsKeyPressed(KEY_F7)) { DumpLevel(g); Toast(g, "LEVEL DUMPED TO CONSOLE"); }
    if (IsKeyPressed(KEY_F9)) { BuildLevel(g); e.index = -1; e.dirty = false; Toast(g, "LEVEL REBUILT (code)"); }
}

// ────────────────────────────────────────────── 描画（3D）

static void DrawMarker(Vector3 p, float r, Color col) {
    DrawSphere(p, r, col);
    DrawSphereWires(p, r * 1.05f, 6, 6, Color{20, 20, 30, 160});
}

void DrawEditor3D(Game& g) {
    const Level& l = g.level;
    const EditorState& e = g.editor;

    DrawGrid(120, 1.0f);

    // 移動床・敵の経路、スライダーの制御点。遊んでいるときは見えない情報を出す。
    for (const MovingPlatform& pf : l.platforms) {
        DrawLine3D(pf.a, pf.b, Color{255, 180, 60, 220});
        DrawMarker(pf.a, 0.18f, Color{255, 200, 90, 255});
        DrawMarker(pf.b, 0.18f, Color{255, 140, 40, 255});
    }
    for (const Enemy& en : l.enemies) {
        DrawLine3D(en.a, en.b, Color{210, 120, 255, 200});
        DrawMarker(en.a, 0.16f, Color{220, 150, 255, 255});
        DrawMarker(en.b, 0.16f, Color{170, 90, 220, 255});
    }
    for (const WaterSlide& s : l.slides) {
        for (int i = 0; i + 1 < (int)s.ctrl.size(); ++i)
            DrawLine3D(s.ctrl[i], s.ctrl[i + 1], Color{120, 220, 255, 160});
        for (const Vector3& p : s.ctrl) DrawMarker(p, 0.22f, Color{150, 235, 255, 255});
    }
    for (const Button& b : l.buttons) {
        DrawLine3D(b.pos, b.gateClosed, Color{120, 255, 220, 140});
        DrawLine3D(b.gateClosed, b.gateOpen, Color{60, 200, 170, 220});
        DrawCubeWiresV(b.gateOpen,
                       (b.gateBoxIndex >= 0 && b.gateBoxIndex < (int)l.boxes.size())
                           ? Vector3Scale(l.boxes[b.gateBoxIndex].h, 2.0f)
                           : Vector3{1, 1, 1},
                       Color{60, 200, 170, 200});
    }
    for (int i = 0; i < (int)l.checkpoints.size(); ++i) {
        Vector3 p = l.checkpoints[i];
        DrawLine3D(p, Vector3Add(p, Vector3{0, 2.2f, 0}), Color{255, 240, 120, 230});
        DrawMarker(Vector3Add(p, Vector3{0, 2.2f, 0}), 0.22f,
                   (i == 0) ? Color{160, 255, 160, 255} : Color{255, 240, 120, 255});
    }
    DrawMarker(l.goal, 0.35f, Color{255, 120, 200, 255});

    // ワイヤーアンカーは遊び中も見えるが、種類の違いを色で出しておく
    for (const WireAnchor& a : l.anchors)
        DrawMarker(a.pos, 0.20f, (a.kind == ANCHOR_HEAVY) ? Color{255, 150, 60, 255}
                                                          : Color{200, 230, 255, 255});

    // 選択中の枠。点滅させて、他の線と区別する。
    Vector3 c, h;
    if (EdBounds(l, e.type, e.index, &c, &h)) {
        float k = 0.5f + 0.5f * sinf((float)GetTime() * 7.0f);
        Color hi{(unsigned char)(80 + 175 * k), 255, (unsigned char)(120 + 100 * k), 255};
        Vector3 size = Vector3Scale(h, 2.0f);
        DrawCubeWiresV(c, Vector3AddValue(size, 0.10f), hi);
        DrawCubeWiresV(c, Vector3AddValue(size, 0.16f), Color{hi.r, hi.g, hi.b, 90});

        // 掴んでいる「部分」の位置に印を出す
        Vector3 pp = EdGetPos(l, e.type, e.index, e.part);
        DrawMarker(pp, 0.26f, Color{255, 255, 160, 255});
        DrawLine3D(Vector3Subtract(pp, Vector3{2, 0, 0}), Vector3Add(pp, Vector3{2, 0, 0}), Color{255, 90, 90, 200});
        DrawLine3D(Vector3Subtract(pp, Vector3{0, 2, 0}), Vector3Add(pp, Vector3{0, 2, 0}), Color{120, 255, 120, 200});
        DrawLine3D(Vector3Subtract(pp, Vector3{0, 0, 2}), Vector3Add(pp, Vector3{0, 0, 2}), Color{110, 160, 255, 200});
    }

    // マウス位置のゴースト（N でここに置かれる）
    bool hit = false;
    Vector3 p = MouseGroundPoint(g, &hit);
    if (hit) {
        if (e.snap) p = SnapV(p, e.grid);
        DrawCubeWiresV(Vector3Add(p, Vector3{0, 0.05f, 0}), Vector3{e.grid, 0.02f, e.grid},
                       Color{255, 255, 255, 200});
        DrawMarker(p, 0.10f, Color{255, 255, 255, 220});
    }

    // プレイヤーの現在地（試遊の開始位置）
    DrawCubeWiresV(g.player.pos, Vector3Scale(g.player.half, 2.0f), Color{255, 255, 255, 180});
}

// ────────────────────────────────────────────── 描画（2D）

void DrawEditorUI(Game& g) {
    const Level& l = g.level;
    const EditorState& e = g.editor;
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    // 編集モードだと一目で分かる枠
    DrawRectangleLinesEx(Rectangle{2, 2, (float)sw - 4, (float)sh - 4}, 3,
                         Color{255, 200, 80, 200});

    // ── 左上：選択中の情報
    int x = 14, y = 14;
    DrawRectangle(x - 10, y - 8, 580, e.showHelp ? 250 : 176, Color{0, 0, 0, 170});
    DrawText("LEVEL EDITOR   F4 = back to play", x, y, 20, Color{255, 210, 100, 255}); y += 26;

    DrawText(TextFormat("type   %s  [%d/%d]   ([ ] kind   Tab next)",
                        kEdTypeName[e.type], e.index + 1, EdCount(l, e.type)),
             x, y, 17, Color{180, 230, 255, 255}); y += 21;

    if (e.index >= 0) {
        Vector3 p = EdGetPos(l, e.type, e.index, e.part);
        DrawText(TextFormat("pos    %7.2f %7.2f %7.2f   part: %s  (T)",
                            p.x, p.y, p.z, EdPartName(e.type, e.part)),
                 x, y, 17, RAYWHITE); y += 21;
        Vector3 c, h;
        const Vector3* hp = EdHalfPtr(const_cast<Level&>(l), e.type, e.index);
        if (hp && EdBounds(l, e.type, e.index, &c, &h)) {
            DrawText(TextFormat("half   %7.2f %7.2f %7.2f   (Shift+arrows = resize)",
                                hp->x, hp->y, hp->z), x, y, 17, Color{215, 225, 235, 255});
        } else {
            DrawText("half   -", x, y, 17, Color{160, 165, 175, 255});
        }
        y += 21;

        EdField fs[8];
        int fn = EdFields(l, e.type, e.index, fs, 8);
        if (fn == 0) {
            DrawText("field  -", x, y, 17, Color{160, 165, 175, 255});
        } else {
            const EdField& fd = fs[e.field];
            const char* extra = "";
            if (e.type == ED_BOX && e.field == 0)
                extra = kBoxKindName[((int)fd.value >= 0 && (int)fd.value < 10) ? (int)fd.value : 6];
            if (e.type == ED_PICKUP && e.field == 0)
                extra = kPickupName[((int)fd.value >= 0 && (int)fd.value < 7) ? (int)fd.value : 0];
            DrawText(TextFormat("field  %s = %.3f %s   [%d/%d]  (, . select   - = adjust)",
                                fd.name, fd.value, extra, e.field + 1, fn),
                     x, y, 17, Color{255, 235, 150, 255});
        }
        y += 21;
    } else {
        DrawText("pos    (nothing selected -- left click to pick, N to add)", x, y, 17,
                 Color{200, 200, 210, 255}); y += 63;
    }

    DrawText(TextFormat("grid %.2f (1/2)   snap %s (G)   new box: %s (B)   file: %s",
                        e.grid, e.snap ? "ON" : "off", kBoxKindName[e.boxKind],
                        e.path ? e.path : "level.txt (not saved yet)"),
             x, y, 16, Color{170, 220, 190, 255}); y += 22;

    if (e.showHelp) {
        DrawText("move: arrows / PgUp / PgDn    Ctrl = fine    Shift+arrows = resize", x, y, 16,
                 Color{255, 220, 140, 255}); y += 19;
        DrawText("N add   C duplicate   X delete   T part   F focus   P move player here", x, y, 16,
                 Color{255, 220, 140, 255}); y += 19;
        DrawText("camera: right-drag orbit    wheel zoom    WASD/Q/E pan", x, y, 16,
                 Color{255, 220, 140, 255}); y += 19;
        DrawText("F5 load   F6 save level.txt   F7 dump   F9 rebuild from code   H help", x, y, 16,
                 Color{255, 220, 140, 255});
    }

    // ── 右下：中身の数
    const char* counts = TextFormat("box %d  plat %d  coin %d  bot %d  target %d  spring %d  "
                                    "button %d  slide %d  anchor %d  enemy %d  pickup %d  cp %d",
                                    (int)l.boxes.size(), (int)l.platforms.size(),
                                    (int)l.coins.size(), (int)l.bots.size(),
                                    (int)l.targets.size(), (int)l.springs.size(),
                                    (int)l.buttons.size(), (int)l.slides.size(),
                                    (int)l.anchors.size(), (int)l.enemies.size(),
                                    (int)l.pickups.size(), (int)l.checkpoints.size());
    int w = MeasureText(counts, 16);
    DrawRectangle(sw - w - 24, sh - 34, w + 20, 26, Color{0, 0, 0, 150});
    DrawText(counts, sw - w - 14, sh - 28, 16, Color{200, 215, 230, 255});

    if (g.toastTimer > 0.0f) {
        unsigned char a = (unsigned char)(255 * Sat(g.toastTimer / 0.6f));
        int tw = MeasureText(g.toast, 30);
        DrawText(g.toast, sw / 2 - tw / 2, 40, 30, Color{120, 235, 255, a});
    }
}
