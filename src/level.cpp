// level.cpp ─ 世界側。小さなデータ部品（型ごとの vector）で構成する層。
//
// ECS にはしない。動くものの種類が増えても std::vector<T> を1本足すだけにする。
// 更新も描画も型ごとに関数を切る。データが連続して並ぶので速く、デバッグしやすい。
//
// CEDEC 2025「テンポよく遊べる3Dレベルデザイン」から意識的に取り入れた点：
//  ・登れる段差(BOX_STEP)と登れない壁(BOX_WALL)を色と形で描き分ける
//  ・障害物(壁)を進行方向と直角に置く
//  ・移動パス上に自然に触れる位置へコインを置く
//  ・サブパスは行きと違う「帰り道」で本道に合流させる
//  ・遠景は彩度と形を変えて、進むべきエリアと区別する
#include "game.h"
#include "util.h"
#include "rlgl.h"
#include <cstdio>
#include <cmath>

// ────────────────────────────────────────────── 生成ヘルパー

static int AddBox(Level& l, Vector3 c, Vector3 h, int kind) {
    l.boxes.push_back(Box{c, h, kind, true});
    return (int)l.boxes.size() - 1;
}

// ワイヤーを引っ掛けるアンカー。boxIndex を持つと「動かせる物」になる。
static int AddAnchor(Level& l, Vector3 pos, int kind, int boxIndex = -1) {
    WireAnchor a;
    a.pos = pos;
    a.kind = kind;
    a.boxIndex = boxIndex;
    l.anchors.push_back(a);
    return (int)l.anchors.size() - 1;
}

static void AddPlatform(Level& l, Vector3 a, Vector3 b, Vector3 h, float speed) {
    int idx = AddBox(l, a, h, BOX_MOVING);
    MovingPlatform pf{};
    pf.boxIndex = idx;
    pf.a = a; pf.b = b;
    pf.speed = speed;
    pf.t = 0.0f; pf.dir = 1;
    pf.delta = Vector3{0, 0, 0};
    l.platforms.push_back(pf);
}

// 直線上にコインを並べる（移動パス上に置くのが基本）
static void AddCoinLine(Level& l, Vector3 from, Vector3 to, int n) {
    for (int i = 0; i < n; ++i) {
        float t = (n == 1) ? 0.5f : (float)i / (float)(n - 1);
        Coin c;
        c.pos = Vector3Lerp(from, to, t);
        c.spin = t * 3.0f;
        l.coins.push_back(c);
    }
}

static void AddTarget(Level& l, Vector3 pos, bool floating) {
    Target t;
    t.pos = pos; t.home = pos;
    t.vel = Vector3{0, 0, 0};
    t.half = Vector3{0.5f, 0.5f, 0.5f};
    t.hp = floating ? 2 : 1;
    t.floating = floating;
    l.targets.push_back(t);
}

static void AddSpring(Level& l, Vector3 pos, float power) {
    Spring s;
    s.pos = pos;
    s.half = Vector3{0.9f, 0.25f, 0.9f};
    s.power = power;
    l.springs.push_back(s);
}

// ボタンと、それが開けるゲートを一組で作る
static void AddButtonGate(Level& l, Vector3 buttonPos, Vector3 gateCenter, Vector3 gateHalf,
                          Vector3 openOffset) {
    int gateIdx = AddBox(l, gateCenter, gateHalf, BOX_GATE);
    Button b;
    b.pos  = buttonPos;
    b.half = Vector3{0.75f, 0.15f, 0.75f};
    b.gateBoxIndex = gateIdx;
    b.gateClosed = gateCenter;
    b.gateOpen   = Vector3Add(gateCenter, openOffset);
    l.buttons.push_back(b);
}

// Catmull-Rom（制御点を通る補間）。端は制御点を複製して扱う。
static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    Vector3 r;
    r.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                  (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                  (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
    r.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                  (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                  (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    r.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t +
                  (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
                  (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
    return r;
}

// 制御点(ctrl)から中心線(pts/cum/length)を作り直す。
// エディタで制御点を1つ動かすたびにこれを呼ぶ。派生データはいつでも捨てて作り直せる、
// という関係にしておくと「編集して保存する対象」が ctrl だけに絞れる。
void RebuildSlide(WaterSlide& s) {
    s.pts.clear();
    s.cum.clear();
    s.length = 0.0f;
    if (s.ctrl.size() < 2) return;
    if (s.sub < 1) s.sub = 1;

    const std::vector<Vector3>& ctrl = s.ctrl;
    for (int i = 0; i + 1 < (int)ctrl.size(); ++i) {
        Vector3 p0 = ctrl[(i - 1 < 0) ? 0 : i - 1];
        Vector3 p1 = ctrl[i];
        Vector3 p2 = ctrl[i + 1];
        Vector3 p3 = ctrl[(i + 2 >= (int)ctrl.size()) ? (int)ctrl.size() - 1 : i + 2];
        for (int k = 0; k < s.sub; ++k) {
            s.pts.push_back(CatmullRom(p0, p1, p2, p3, (float)k / (float)s.sub));
        }
    }
    s.pts.push_back(ctrl.back());

    s.cum.push_back(0.0f);
    for (int i = 0; i + 1 < (int)s.pts.size(); ++i) {
        s.cum.push_back(s.cum[i] + Vector3Distance(s.pts[i], s.pts[i + 1]));
    }
    s.length = s.cum.back();
}

// 制御点からウォータースライダーを作る。当たり判定は作らない（中心線に沿わせる方式）。
static int AddSlide(Level& l, const std::vector<Vector3>& ctrl, float radius, int sub = 8) {
    if (ctrl.size() < 2) return -1;
    WaterSlide s;
    s.radius = radius;
    s.ctrl   = ctrl;
    s.sub    = sub;
    RebuildSlide(s);

    l.slides.push_back(s);
    return (int)l.slides.size() - 1;
}

static void AddEnemy(Level& l, Vector3 a, Vector3 b) {
    Enemy e;
    e.a = a; e.b = b;
    e.pos = a;
    e.half = Vector3{0.45f, 0.45f, 0.45f};
    e.t = 0.0f; e.dir = 1;
    l.enemies.push_back(e);
}

static void AddPickup(Level& l, Vector3 pos, AbilityType type) {
    AbilityPickup a;
    a.pos = pos;
    a.type = type;
    l.pickups.push_back(a);
}

// ────────────────────────────────────────────── コース定義

void BuildLevel(Game& g) {
    Level& l = g.level;
    l = Level{};

    // ── 1. スタート広場（練習用の平地）
    AddBox(l, {0, -0.5f, 0}, {9, 0.5f, 9}, BOX_FLOOR);
    AddTarget(l, {3.5f, 0.5f, 4.0f}, false);
    AddTarget(l, {-3.5f, 0.5f, 6.0f}, false);
    AddCoinLine(l, {0, 0.8f, 2}, {0, 0.8f, 8}, 4);
    // 能力アイテム（1ステージ1能力の受け渡し口）
    AddPickup(l, {-6.5f, 1.1f, 3.0f}, AbilityType::Dash);

    // ── 2. 登れる段差 3 段（明るい色＋黄縁で「登れる」ことを示す）
    AddBox(l, {0, 0.175f, 10.5f}, {4, 0.175f, 1.2f}, BOX_STEP);
    AddBox(l, {0, 0.350f, 13.0f}, {4, 0.350f, 1.2f}, BOX_STEP);
    AddBox(l, {0, 0.525f, 15.5f}, {4, 0.525f, 1.2f}, BOX_STEP);
    AddCoinLine(l, {0, 1.2f, 10.5f}, {0, 1.9f, 15.5f}, 3);

    // ── 3. 高台（敵とバネを置いて、能力・踏みつけの練習場にする）
    AddBox(l, {0, 0.55f, 22.0f}, {8, 0.55f, 6}, BOX_FLOOR);
    AddTarget(l, {0, 1.6f, 19.0f}, false);
    AddTarget(l, {-5.0f, 1.6f, 24.0f}, false);
    // 本道（x=0 の直線）から外して、サブパスの分岐側を巡回させる
    AddEnemy(l, {6.0f, 1.55f, 17.5f}, {6.0f, 1.55f, 26.5f});

    // バネ → 高い秘密の棚（z 方向へ流されながら乗る）→ 棚の端からスライダーへ
    // バネの打ち上げ列とスライダーの入口は離す（打ち上げ途中で吸い込まれないように）
    AddSpring(l, {-5.5f, 1.35f, 25.2f}, 0.0f);
    AddBox(l, {-5.5f, 5.60f, 21.0f}, {2.0f, 0.30f, 2.0f}, BOX_SUBPATH);
    AddCoinLine(l, {-5.5f, 6.4f, 20.0f}, {-5.5f, 6.4f, 22.0f}, 3);
    l.bots.push_back(Bot{{-5.5f, 6.5f, 21.0f}});   // ボット #1（バネ専用ルート）

    // ウォータースライダー：秘密の棚から、壁のすき間を抜けて着地エリアまで一気に運ぶ。
    // バネ → 棚 → スライダー、という発見のご褒美ルートにしている。
    {
        // 本道（x≒0 のジャンプ列とホバーギャップ）の真上を通ると視界を塞ぐので、
        // 左側の空間へ迂回させる。壁の外側を越えて、最後に着地エリアへ戻る。
        int si = AddSlide(l, {
            {-5.5f, 6.60f, 22.6f},   // 入口（棚のすぐ横）
            {-6.5f, 6.50f, 25.5f},
            {-8.0f, 6.20f, 29.0f},   // 登れない壁（上端 5.1）の外側を越える
            {-10.0f, 5.30f, 34.0f},
            {-10.5f, 4.50f, 39.0f},
            {-9.0f, 3.95f, 44.0f},
            {-7.0f, 3.65f, 48.5f},   // ほぼ水平。ここで少し溜める
            {-5.0f, 3.70f, 52.5f},   // わずかに登り
            {-3.0f, 3.10f, 56.0f},
            {-1.0f, 2.40f, 58.5f},   // 最後だけ急にして出口で伸びるように
            { 0.0f, 2.00f, 60.0f},   // 出口（着地エリアの上）
        }, 1.6f);
        // 水路の上にコインを流しておく（乗っている間に自然に触れる）
        if (si >= 0) {
            const WaterSlide& s2 = l.slides[si];
            for (int i = 0; i < 10; ++i) {
                float d = s2.length * (0.06f + 0.09f * (float)i);
                Vector3 c = SlidePoint(s2, d);
                Coin coin;
                coin.pos = Vector3{c.x, c.y - s2.radius * 0.30f, c.z};
                coin.spin = (float)i * 0.7f;
                l.coins.push_back(coin);
            }
        }
    }

    // ── 3.5 ワイヤーの練習場と「驚き」の仕掛け（高台の左へ張り出す）
    //
    // 見えるのは「オレンジの岩に輪が付いている」だけ。ワイヤーを引っ掛けると
    // 岩の方が飛んできて、奥に隠れていた部屋が開く。
    AddPickup(l, {-7.0f, 1.7f, 21.0f}, AbilityType::Wire);       // 能力を持ち替える
    AddBox(l, {-11.5f, 0.80f, 21.0f}, {3.5f, 0.30f, 3.5f}, BOX_FLOOR);   // 張り出し
    AddBox(l, {-14.5f, 2.60f, 18.4f}, {0.6f, 1.50f, 1.8f}, BOX_WALL);    // 入口の左
    AddBox(l, {-14.5f, 2.60f, 23.6f}, {0.6f, 1.50f, 1.8f}, BOX_WALL);    // 入口の右
    {
        int rock = AddBox(l, {-14.5f, 2.10f, 21.0f}, {0.75f, 1.00f, 0.80f}, BOX_MOVING);
        AddAnchor(l, {-14.5f, 3.45f, 21.0f}, ANCHOR_HEAVY, rock);
    }
    // 岩の奥の隠し部屋
    AddBox(l, {-18.5f, 0.80f, 21.0f}, {3.5f, 0.30f, 3.0f}, BOX_FLOOR);
    AddBox(l, {-18.5f, 2.60f, 17.6f}, {3.5f, 1.50f, 0.6f}, BOX_WALL);
    AddBox(l, {-18.5f, 2.60f, 24.4f}, {3.5f, 1.50f, 0.6f}, BOX_WALL);
    AddBox(l, {-21.6f, 2.60f, 21.0f}, {0.6f, 1.50f, 3.0f}, BOX_WALL);
    l.bots.push_back(Bot{{-19.5f, 2.10f, 21.0f}});               // ボット #4（驚きのご褒美）
    AddCoinLine(l, {-17.5f, 1.9f, 21.0f}, {-20.5f, 1.9f, 21.0f}, 4);

    // ── 4. 登れない壁を進行方向と直角に。中央だけ通れる
    AddBox(l, {-5.0f, 3.1f, 27.0f}, {3, 2.0f, 0.5f}, BOX_WALL);
    AddBox(l, { 5.0f, 3.1f, 27.0f}, {3, 2.0f, 0.5f}, BOX_WALL);
    AddCoinLine(l, {0, 1.8f, 25.5f}, {0, 1.8f, 27.8f}, 2);
    l.bots.push_back(Bot{{6.5f, 1.7f, 27.85f}});   // ボット #2（壁の裏の細い帯）

    // ── 5. ジャンプ列（間隔を少しずつ広げてテンポを作る）
    AddBox(l, {0, 0.80f, 32.0f}, {1.6f, 0.30f, 1.6f}, BOX_FLOOR);
    AddBox(l, {0, 1.10f, 38.0f}, {1.6f, 0.30f, 1.6f}, BOX_FLOOR);
    AddBox(l, {0, 1.40f, 44.5f}, {1.6f, 0.30f, 1.6f}, BOX_FLOOR);
    AddCoinLine(l, {0, 2.0f, 30.5f}, {0, 2.2f, 33.5f}, 3);
    AddCoinLine(l, {0, 2.3f, 36.5f}, {0, 2.5f, 39.5f}, 3);
    AddTarget(l, {0, -1.2f, 35.0f}, true);    // レーザー用の浮遊ターゲット
    AddTarget(l, {0, -1.2f, 41.5f}, true);

    // ── 6. ホバー / ダッシュが要る長いギャップ。中央に動く床
    AddPlatform(l, {-4.5f, 1.5f, 51.5f}, {4.5f, 1.5f, 51.5f}, {2.0f, 0.30f, 2.0f}, 3.0f);
    AddTarget(l, {0, -1.2f, 48.0f}, true);
    AddCoinLine(l, {0, 2.6f, 47.0f}, {0, 2.6f, 49.5f}, 3);

    // ワイヤーで渡るルート（ホバー / ダッシュと並ぶ第三の渡り方）
    AddAnchor(l, {0.0f, 8.50f, 47.5f}, ANCHOR_FIXED);
    AddAnchor(l, {0.0f, 9.00f, 52.5f}, ANCHOR_FIXED);
    AddAnchor(l, {0.0f, 8.50f, 57.0f}, ANCHOR_FIXED);
    AddCoinLine(l, {0, 5.5f, 47.5f}, {0, 5.5f, 57.0f}, 4);

    // ── 7. 着地エリア。ボタンで開くゲートの奥にコイン、敵が1体巡回
    AddBox(l, {0, 0.5f, 60.0f}, {7, 0.5f, 6}, BOX_FLOOR);
    AddEnemy(l, {-3.5f, 1.45f, 56.0f}, {-3.5f, 1.45f, 64.0f});
    AddBox(l, {3.9f, 1.9f, 63.5f}, {0.4f, 0.9f, 2.0f}, BOX_WALL);          // 仕切り
    AddButtonGate(l, {-5.0f, 1.15f, 57.5f},                                 // ボタン
                  {5.5f, 1.9f, 61.6f}, {1.6f, 0.9f, 0.4f},                  // ゲート
                  {0, -2.2f, 0});                                           // 開くと沈む
    AddCoinLine(l, {5.5f, 1.6f, 63.0f}, {5.5f, 1.6f, 65.0f}, 4);

    // ── 8. タワー（螺旋の足場）とゴール
    AddBox(l, {0, 4.3f, 62.0f}, {1.5f, 4.3f, 1.5f}, BOX_WALL);   // 中心の柱＝登れない
    for (int i = 0; i < 10; ++i) {
        float ang = 0.72f * (float)i;
        float r   = 3.3f;
        float y   = 1.55f + 0.80f * (float)i;
        Vector3 c = {r * cosf(ang), y - 0.15f, 62.0f + r * sinf(ang)};
        AddBox(l, c, {1.2f, 0.15f, 1.2f}, BOX_STEP);
        Coin coin; coin.pos = Vector3{c.x, y + 0.9f, c.z}; coin.spin = ang; l.coins.push_back(coin);
    }
    AddBox(l, {0, 8.85f, 62.0f}, {3, 0.20f, 3}, BOX_FLOOR);
    l.goal = Vector3{0, 9.05f, 62.0f};

    // ── 9. サブパス（高台の右へ枝分かれ → 登る → 別ルートで下って合流）
    AddBox(l, {11.0f, 1.00f, 20.0f}, {1.5f, 0.25f, 1.5f}, BOX_SUBPATH);
    AddBox(l, {15.0f, 1.80f, 22.5f}, {1.5f, 0.25f, 1.5f}, BOX_SUBPATH);
    AddBox(l, {18.5f, 2.60f, 19.5f}, {1.5f, 0.25f, 1.5f}, BOX_SUBPATH);
    AddBox(l, {22.0f, 3.40f, 22.0f}, {3.0f, 0.40f, 3.0f}, BOX_SUBPATH);
    l.bots.push_back(Bot{{22.0f, 4.5f, 22.0f}});   // ボット #3
    AddCoinLine(l, {11.0f, 2.0f, 20.0f}, {22.0f, 4.4f, 22.0f}, 6);
    AddBox(l, {20.0f, 2.90f, 30.0f}, {2.0f, 0.30f, 2.0f}, BOX_SUBPATH);
    AddBox(l, {15.5f, 2.30f, 35.5f}, {2.0f, 0.30f, 2.0f}, BOX_SUBPATH);
    AddBox(l, {10.0f, 1.85f, 40.5f}, {2.0f, 0.30f, 2.0f}, BOX_SUBPATH);
    AddBox(l, { 5.0f, 1.55f, 44.5f}, {2.0f, 0.30f, 2.0f}, BOX_SUBPATH);
    AddCoinLine(l, {20.0f, 3.8f, 30.0f}, {5.0f, 2.5f, 44.5f}, 6);

    // ── 10. 遠景（当たり判定なし。彩度と形を落として本道と区別する）
    AddBox(l, {-46, 6, 30}, {6, 14, 24}, BOX_SCENERY);
    AddBox(l, { 52, 4, 44}, {8, 12, 30}, BOX_SCENERY);
    AddBox(l, {-20, 2, 95}, {24, 10, 8}, BOX_SCENERY);
    AddBox(l, { 26, 8, 110}, {14, 20, 8}, BOX_SCENERY);
    AddBox(l, {  0, -1, 130}, {70, 2, 20}, BOX_SCENERY);

    l.checkpoints = {
        Vector3{0, 1.2f, 0},
        Vector3{0, 2.0f, 20.0f},
        Vector3{0, 2.2f, 32.0f},
        Vector3{0, 2.5f, 44.5f},
        Vector3{0, 2.0f, 58.0f},
    };

    // プレイヤーと集計をまとめて初期化（テストで毎回同じ状態から始められるように）
    g.player = Player{};
    g.player.pos = l.checkpoints[0];
    g.player.hoverFuel = g.p.hoverFuelMax;
    g.stats = GameStats{};
    g.interactions.items.clear();
    g.events.items.clear();
    g.particles.clear();
    g.effects.clear();
    ResetWorldPhysics(g);      // 地形が確定してから物理世界を作り直す
    g.time = 0.0f;
    g.hitStop = 0.0f;
    g.clearBanner = 0.0f;
}

// ────────────────────────────────────────────── 当たり判定ユーティリティ

bool BoxOverlap(Vector3 ca, Vector3 ha, Vector3 cb, Vector3 hb) {
    return fabsf(ca.x - cb.x) < (ha.x + hb.x) &&
           fabsf(ca.y - cb.y) < (ha.y + hb.y) &&
           fabsf(ca.z - cb.z) < (ha.z + hb.z);
}

bool AnyOverlapSolid(const Level& l, Vector3 c, Vector3 h) {
    for (const Box& b : l.boxes) {
        if (b.kind == BOX_SCENERY || !b.solid) continue;
        if (BoxOverlap(c, h, b.c, b.h)) return true;
    }
    return false;
}

// 真下へのレイ。戻り値は接触点の Y（当たらなければ from.y - maxDist）
float RaycastDown(const Level& l, Vector3 from, float maxDist, int* hitTargetOut) {
    float best = from.y - maxDist;
    if (hitTargetOut) *hitTargetOut = -1;

    for (const Box& b : l.boxes) {
        if (b.kind == BOX_SCENERY || !b.solid) continue;
        if (fabsf(from.x - b.c.x) > b.h.x) continue;
        if (fabsf(from.z - b.c.z) > b.h.z) continue;
        float top = b.c.y + b.h.y;
        if (top <= from.y && top > best) best = top;
    }
    for (int i = 0; i < (int)l.targets.size(); ++i) {
        const Target& t = l.targets[i];
        if (!t.alive) continue;
        if (fabsf(from.x - t.pos.x) > t.half.x) continue;
        if (fabsf(from.z - t.pos.z) > t.half.z) continue;
        float top = t.pos.y + t.half.y;
        if (top <= from.y && top > best) { best = top; if (hitTargetOut) *hitTargetOut = i; }
    }
    return best;
}

// ────────────────────────────────────────────── スライダーの中心線

Vector3 SlidePoint(const WaterSlide& s, float dist) {
    if (s.pts.empty()) return Vector3{0, 0, 0};
    if (dist <= 0.0f) return s.pts.front();
    if (dist >= s.length) return s.pts.back();
    for (int i = 0; i + 1 < (int)s.pts.size(); ++i) {
        if (dist <= s.cum[i + 1]) {
            float segLen = s.cum[i + 1] - s.cum[i];
            float t = (segLen > 1e-5f) ? (dist - s.cum[i]) / segLen : 0.0f;
            return Vector3Lerp(s.pts[i], s.pts[i + 1], t);
        }
    }
    return s.pts.back();
}

Vector3 SlideTangent(const WaterSlide& s, float dist) {
    if (s.pts.size() < 2) return Vector3{0, 0, 1};
    int i = 0;
    for (; i + 2 < (int)s.pts.size(); ++i) {
        if (dist <= s.cum[i + 1]) break;
    }
    Vector3 d = Vector3Subtract(s.pts[i + 1], s.pts[i]);
    if (Vector3Length(d) < 1e-5f) return Vector3{0, 0, 1};
    return Vector3Normalize(d);
}

// 中心線に一番近い位置（距離）を返す。乗り込む地点を決めるのに使う。
float SlideNearestDist(const WaterSlide& s, Vector3 from, float* outDistToLine) {
    float bestDist = 0.0f, bestD2 = 1e18f;
    for (int i = 0; i + 1 < (int)s.pts.size(); ++i) {
        Vector3 a = s.pts[i], b = s.pts[i + 1];
        Vector3 ab = Vector3Subtract(b, a);
        float len2 = Vector3DotProduct(ab, ab);
        float t = (len2 > 1e-6f) ? Vector3DotProduct(Vector3Subtract(from, a), ab) / len2 : 0.0f;
        t = Clamp(t, 0.0f, 1.0f);
        Vector3 q = Vector3Add(a, Vector3Scale(ab, t));
        float d2 = Vector3DistanceSqr(q, from);
        if (d2 < bestD2) {
            bestD2 = d2;
            bestDist = s.cum[i] + t * (s.cum[i + 1] - s.cum[i]);
        }
    }
    if (outDistToLine) *outDistToLine = sqrtf(bestD2);
    return bestDist;
}

// ────────────────────────────────────────────── 更新（型ごとに分ける）

static void UpdateMovingPlatforms(Game& g, float dt) {
    Level& l = g.level;
    for (MovingPlatform& pf : l.platforms) {
        Box& b = l.boxes[pf.boxIndex];
        Vector3 prev = b.c;

        float len = Vector3Distance(pf.a, pf.b);
        if (len < 0.001f) { pf.delta = Vector3{0, 0, 0}; continue; }
        pf.t += (float)pf.dir * (pf.speed / len) * dt;
        if (pf.t >= 1.0f) { pf.t = 1.0f; pf.dir = -1; }
        if (pf.t <= 0.0f) { pf.t = 0.0f; pf.dir = 1; }

        float e = pf.t * pf.t * (3.0f - 2.0f * pf.t);   // 端で少し溜める
        b.c = Vector3Lerp(pf.a, pf.b, e);
        pf.delta = Vector3Subtract(b.c, prev);
    }
}

static void UpdateTargets(Game& g, float dt) {
    Level& l = g.level;
    for (Target& t : l.targets) {
        t.flash = fmaxf(0.0f, t.flash - dt * 4.0f);
        t.spin += dt * (t.floating ? 1.2f : 0.4f);

        if (!t.alive) {
            t.respawn -= dt;
            if (t.respawn <= 0.0f) {
                t.alive = true;
                t.hp = t.floating ? 2 : 1;
                t.pos = t.home;
                t.vel = Vector3{0, 0, 0};
                SpawnBurst(g, t.home, 10, SKYBLUE, 4.0f, 0.22f);
            }
            continue;
        }
        if (t.floating) {
            t.pos.y = t.home.y + sinf(g.time * 1.6f + t.home.z) * 0.25f;
            continue;
        }

        // 殴られて飛んでいる箱の簡易物理
        t.vel.y -= 30.0f * dt;
        Vector3 next = Vector3Add(t.pos, Vector3Scale(t.vel, dt));
        Vector3 probe = Vector3{t.pos.x, next.y, t.pos.z};
        bool landed = false;
        for (const Box& b : l.boxes) {
            if (b.kind == BOX_SCENERY || !b.solid) continue;
            if (!BoxOverlap(probe, t.half, b.c, b.h)) continue;
            if (t.vel.y <= 0.0f) {
                probe.y = b.c.y + b.h.y + t.half.y + 0.001f;
                if (fabsf(t.vel.y) > 4.0f) t.vel.y *= -0.35f;
                else { t.vel.y = 0.0f; landed = true; }
            } else {
                probe.y = b.c.y - b.h.y - t.half.y - 0.001f;
                t.vel.y = 0.0f;
            }
        }
        t.pos.y = probe.y;
        t.pos.x = next.x;
        t.pos.z = next.z;
        if (landed) { t.vel.x *= 0.82f; t.vel.z *= 0.82f; }

        if (t.pos.y < -20.0f) { t.alive = false; t.respawn = 2.0f; }
    }
}

// 収集物は見た目だけ更新する。取得は Interaction → Reaction で処理される。
static void UpdateCollectibles(Game& g, float dt) {
    for (Coin& c : g.level.coins) c.spin += dt * 3.0f;
    for (Bot& b : g.level.bots)   b.bob  += dt;
    for (AbilityPickup& a : g.level.pickups) a.bob += dt * 2.0f;
}

static void UpdateGimmicks(Game& g, float dt) {
    Level& l = g.level;

    for (Spring& s : l.springs) {
        s.compress = ExpSmooth(s.compress, 0.0f, 9.0f, dt);
        s.cooldown = fmaxf(0.0f, s.cooldown - dt);
    }

    for (Button& b : l.buttons) {
        b.press = ExpSmooth(b.press, 0.0f, 6.0f, dt);
        b.openTimer = fmaxf(0.0f, b.openTimer - dt);

        float want = (b.openTimer > 0.0f) ? 1.0f : 0.0f;
        float rate = g.p.gateMoveSpeed * dt / fmaxf(0.01f, Vector3Distance(b.gateClosed, b.gateOpen));
        b.openRatio = MoveTowardsF(b.openRatio, want, rate);

        if (b.gateBoxIndex >= 0 && b.gateBoxIndex < (int)l.boxes.size()) {
            l.boxes[b.gateBoxIndex].c = Vector3Lerp(b.gateClosed, b.gateOpen, b.openRatio);
        }
    }
}

static void UpdateAnchors(Game& g, float dt) {
    for (WireAnchor& a : g.level.anchors) {
        a.bob   += dt * 2.2f;
        a.flash  = fmaxf(0.0f, a.flash - dt * 3.0f);
        if (!a.pulled) continue;

        // 引き抜かれた物は当たり判定を切って、そのまま飛んで落ちていく
        a.vel.y -= 26.0f * dt;
        Vector3 step = Vector3Scale(a.vel, dt);
        a.pos = Vector3Add(a.pos, step);
        if (a.boxIndex >= 0 && a.boxIndex < (int)g.level.boxes.size()) {
            Box& b = g.level.boxes[a.boxIndex];
            b.solid = false;
            b.c = Vector3Add(b.c, step);
            if (b.c.y < -30.0f) b.kind = BOX_SCENERY;
        }
    }
}

static void UpdateEnemies(Game& g, float dt) {
    for (Enemy& e : g.level.enemies) {
        e.flash = fmaxf(0.0f, e.flash - dt * 3.0f);
        e.bob  += dt * 3.0f;

        if (!e.alive) {
            e.respawn -= dt;
            if (e.respawn <= 0.0f) {
                e.alive = true;
                e.pos = Vector3Lerp(e.a, e.b, e.t);
                SpawnBurst(g, e.pos, 10, Color{200, 130, 255, 255}, 4.0f, 0.2f);
            }
            continue;
        }
        float len = Vector3Distance(e.a, e.b);
        if (len < 0.001f) continue;
        float sp = (e.speed > 0.0f) ? e.speed : g.p.enemySpeed;
        e.t += (float)e.dir * (sp / len) * dt;
        if (e.t >= 1.0f) { e.t = 1.0f; e.dir = -1; }
        if (e.t <= 0.0f) { e.t = 0.0f; e.dir = 1; }
        e.pos = Vector3Lerp(e.a, e.b, e.t);
        e.pos.y += sinf(e.bob) * 0.06f;
    }
}

void UpdateLevel(Game& g, float dt) {
    UpdateMovingPlatforms(g, dt);
    UpdateTargets(g, dt);
    UpdateCollectibles(g, dt);
    UpdateGimmicks(g, dt);
    UpdateAnchors(g, dt);
    UpdateEnemies(g, dt);
    UpdateParticles(g, dt);
    UpdateEffects(g, dt);
}

// ────────────────────────────────────────────── 描画（型ごとに分ける）

static Color KindColor(int kind) {
    switch (kind) {
        case BOX_FLOOR:   return Color{206, 210, 196, 255};  // 歩ける床
        case BOX_STEP:    return Color{170, 205, 140, 255};  // 登れる段差
        case BOX_WALL:    return Color{ 68,  78, 100, 255};  // 登れない壁
        case BOX_MOVING:  return Color{240, 150,  60, 255};  // 動く床
        case BOX_SUBPATH: return Color{200, 175, 225, 255};  // サブパス
        case BOX_GATE:    return Color{110, 210, 190, 255};  // ゲート
        default:          return Color{150, 172, 196, 255};  // 遠景
    }
}

static void DrawTerrain(Game& g) {
    for (const Box& b : g.level.boxes) {
        Vector3 size = Vector3Scale(b.h, 2.0f);
        DrawCubeV(b.c, size, KindColor(b.kind));
        if (b.kind == BOX_SCENERY) continue;

        Color edge = (b.kind == BOX_STEP)    ? Color{255, 220, 90, 255}
                   : (b.kind == BOX_WALL)    ? Color{30, 34, 48, 255}
                   : (b.kind == BOX_MOVING)  ? Color{255, 240, 200, 255}
                   : (b.kind == BOX_SUBPATH) ? Color{140, 100, 190, 255}
                   : (b.kind == BOX_GATE)    ? Color{40, 120, 110, 255}
                                             : Color{140, 146, 132, 255};
        DrawCubeWiresV(b.c, size, edge);

        if (b.kind != BOX_WALL) {
            Vector3 top = Vector3{b.c.x, b.c.y + b.h.y + 0.012f, b.c.z};
            DrawCubeV(top, Vector3{b.h.x * 2.0f * 0.94f, 0.02f, b.h.z * 2.0f * 0.94f},
                      Color{255, 255, 255, 40});
        }
    }
}

static void DrawCollectibles(Game& g) {
    for (const Coin& c : g.level.coins) {
        if (c.taken) continue;
        rlPushMatrix();
        rlTranslatef(c.pos.x, c.pos.y + sinf(c.spin) * 0.12f, c.pos.z);
        rlRotatef(c.spin * 90.0f, 0, 1, 0);
        DrawCube(Vector3{0, 0, 0}, 0.52f, 0.52f, 0.09f, GOLD);
        DrawCubeWires(Vector3{0, 0, 0}, 0.52f, 0.52f, 0.09f, Color{140, 100, 0, 255});
        rlPopMatrix();
    }

    for (const Bot& b : g.level.bots) {
        if (b.saved) continue;
        float y = b.pos.y + sinf(b.bob * 2.2f) * 0.14f;
        DrawSphere(Vector3{b.pos.x, y, b.pos.z}, 0.34f, Color{110, 215, 255, 255});
        DrawSphereWires(Vector3{b.pos.x, y, b.pos.z}, 0.36f, 8, 8, Color{20, 90, 130, 255});
        DrawCylinderEx(Vector3{b.pos.x, y - 0.3f, b.pos.z}, Vector3{b.pos.x, y + 3.0f, b.pos.z},
                       0.05f, 0.02f, 6, Color{110, 215, 255, 70});
    }

    for (const AbilityPickup& a : g.level.pickups) {
        if (a.taken) continue;
        float y = a.pos.y + sinf(a.bob) * 0.16f;
        rlPushMatrix();
        rlTranslatef(a.pos.x, y, a.pos.z);
        rlRotatef(a.bob * 40.0f, 0.2f, 1.0f, 0.0f);
        DrawCube(Vector3{0, 0, 0}, 0.62f, 0.62f, 0.62f, Color{255, 205, 90, 255});
        DrawCubeWires(Vector3{0, 0, 0}, 0.64f, 0.64f, 0.64f, Color{150, 100, 20, 255});
        rlPopMatrix();
        DrawCylinderEx(Vector3{a.pos.x, y - 0.4f, a.pos.z}, Vector3{a.pos.x, y + 3.5f, a.pos.z},
                       0.06f, 0.02f, 6, Color{255, 205, 90, 80});
    }
}

static void DrawGimmicks(Game& g) {
    for (const Spring& s : g.level.springs) {
        float squash = 1.0f - 0.55f * s.compress;
        Vector3 size = Vector3{s.half.x * 2.0f, s.half.y * 2.0f * squash, s.half.z * 2.0f};
        Vector3 c = Vector3{s.pos.x, s.pos.y - s.half.y * (1.0f - squash), s.pos.z};
        DrawCubeV(c, size, Color{255, 210, 80, 255});
        DrawCubeWiresV(c, size, Color{160, 110, 20, 255});
        // 上向きの矢印（跳べる場所だと分かるように）
        float top = c.y + size.y * 0.5f;
        DrawCylinderEx(Vector3{s.pos.x, top, s.pos.z}, Vector3{s.pos.x, top + 0.9f, s.pos.z},
                       0.16f, 0.0f, 8, Color{255, 240, 160, 200});
    }

    for (const Button& b : g.level.buttons) {
        float squash = 1.0f - 0.5f * b.press;
        Vector3 size = Vector3{b.half.x * 2.0f, b.half.y * 2.0f * squash, b.half.z * 2.0f};
        Vector3 c = Vector3{b.pos.x, b.pos.y - b.half.y * (1.0f - squash), b.pos.z};
        Color col = (b.openTimer > 0.0f) ? Color{120, 255, 180, 255} : Color{235, 120, 120, 255};
        DrawCubeV(c, size, col);
        DrawCubeWiresV(c, size, Color{50, 60, 70, 255});
        // 残り時間のリング
        if (b.openTimer > 0.0f) {
            float k = Sat(b.openTimer / fmaxf(0.01f, g.p.buttonOpenTime));
            DrawCircle3D(Vector3{b.pos.x, b.pos.y + 0.3f, b.pos.z}, 0.5f + 0.5f * k,
                         Vector3{1, 0, 0}, 90.0f, Color{120, 255, 180, 180});
        }
    }
}

// 四角形を「両面」描く。raylib は形状をバッチに溜めてから描くので、
// rlDisableBackfaceCulling() を挟んでも実際に描かれる時の状態が効いてしまう。
// 表裏の2通りを出しておけば、どちらから見ても消えない。
static void DrawQuadBothSides(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
    DrawTriangle3D(c, b, a, col);
    DrawTriangle3D(d, c, a, col);
}

// 水路。閉じた管にすると中のプレイヤーが隠れるので、U 字の樋として描く。
static void DrawSlides(Game& g) {
    for (const WaterSlide& s : g.level.slides) {
        if (s.pts.size() < 2) continue;
        float w    = s.radius * 0.95f;
        float drop = -s.radius * 0.55f;

        float wallH = s.radius * 0.95f;

        for (int i = 0; i + 1 < (int)s.pts.size(); ++i) {
            Vector3 a = s.pts[i], b = s.pts[i + 1];
            Vector3 tan = Vector3Normalize(Vector3Subtract(b, a));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(tan, Vector3{0, 1, 0}));

            Vector3 a0 = Vector3Add(Vector3Add(a, Vector3Scale(right,  w)), Vector3{0, drop, 0});
            Vector3 a1 = Vector3Add(Vector3Add(a, Vector3Scale(right, -w)), Vector3{0, drop, 0});
            Vector3 b0 = Vector3Add(Vector3Add(b, Vector3Scale(right,  w)), Vector3{0, drop, 0});
            Vector3 b1 = Vector3Add(Vector3Add(b, Vector3Scale(right, -w)), Vector3{0, drop, 0});

            Vector3 f0 = Vector3Add(a0, Vector3{0, -0.12f, 0});
            Vector3 f1 = Vector3Add(a1, Vector3{0, -0.12f, 0});
            Vector3 g0 = Vector3Add(b0, Vector3{0, -0.12f, 0});
            Vector3 g1 = Vector3Add(b1, Vector3{0, -0.12f, 0});
            Vector3 a0u = Vector3Add(a0, Vector3{0, wallH, 0});
            Vector3 b0u = Vector3Add(b0, Vector3{0, wallH, 0});
            Vector3 a1u = Vector3Add(a1, Vector3{0, wallH, 0});
            Vector3 b1u = Vector3Add(b1, Vector3{0, wallH, 0});

            // 樋の底（不透明にして「物」として読めるようにする）
            DrawQuadBothSides(f0, f1, g1, g0, Color{ 56,  98, 142, 255});
            // 水面（底の少し上）
            DrawQuadBothSides(a0, a1, b1, b0, Color{ 62, 178, 240, 215});
            // 側面（プレイヤーが隠れないよう半透明）
            DrawQuadBothSides(f0, g0, b0u, a0u, Color{118, 205, 248, 130});
            DrawQuadBothSides(f1, a1u, b1u, g1, Color{118, 205, 248, 130});
            // 縁のレール
            Color rail = Color{ 30, 104, 165, 255};
            DrawCylinderEx(a0u, b0u, 0.08f, 0.08f, 6, rail);
            DrawCylinderEx(a1u, b1u, 0.08f, 0.08f, 6, rail);
        }

        // 断面のリブ。どの角度から見ても「筒」だと分かるようにする。
        for (int i = 0; i + 1 < (int)s.pts.size(); i += 6) {
            Vector3 a = s.pts[i];
            Vector3 tan = SlideTangent(s, s.cum[i]);
            Vector3 right = Vector3Normalize(Vector3CrossProduct(tan, Vector3{0, 1, 0}));
            Vector3 lo = Vector3Add(a, Vector3{0, drop - 0.10f, 0});
            for (int k = 0; k < 6; ++k) {
                float t0 = -1.0f + 2.0f * (float)k / 6.0f;
                float t1 = -1.0f + 2.0f * (float)(k + 1) / 6.0f;
                Vector3 p0 = Vector3Add(Vector3Add(lo, Vector3Scale(right, t0 * w)),
                                        Vector3{0, t0 * t0 * wallH, 0});
                Vector3 p1 = Vector3Add(Vector3Add(lo, Vector3Scale(right, t1 * w)),
                                        Vector3{0, t1 * t1 * wallH, 0});
                DrawCylinderEx(p0, p1, 0.06f, 0.06f, 5, Color{40, 120, 180, 235});
            }
        }

        // 流れているように見せる小さな泡（進行方向を伝える）
        for (int k = 0; k < 26; ++k) {
            float d = fmodf(g.time * 9.0f + (float)k * (s.length / 26.0f), s.length);
            Vector3 c = SlidePoint(s, d);
            Vector3 t = SlideTangent(s, d);
            Vector3 r = Vector3Normalize(Vector3CrossProduct(t, Vector3{0, 1, 0}));
            float off = sinf((float)k * 2.3f) * s.radius * 0.45f;
            Vector3 q = Vector3Add(Vector3Add(c, Vector3Scale(r, off)),
                                   Vector3{0, drop + 0.07f, 0});
            DrawCube(q, 0.13f, 0.04f, 0.55f, Color{225, 250, 255, 230});
        }

        // 入口と出口の目印
        Vector3 in0 = s.pts.front(), out0 = s.pts.back();
        Vector3 tin = SlideTangent(s, 0.0f), tout = SlideTangent(s, s.length);
        for (int i = 0; i < 2; ++i) {
            DrawCircle3D(in0,  s.radius * (0.9f + 0.1f * i), tin,  90.0f, Color{140, 255, 210, 220});
            DrawCircle3D(out0, s.radius * (0.9f + 0.1f * i), tout, 90.0f, Color{255, 220, 140, 220});
        }
    }
}

// 任意の向きの輪を描く（raylib の DrawCircle3D は XY 平面の円を回して描く）
static void DrawRingFacing(Vector3 c, float r, Vector3 normal, Color col) {
    Vector3 n0 = Vector3{0, 0, 1};
    Vector3 n  = Vector3Normalize(normal);
    Vector3 axis = Vector3CrossProduct(n0, n);
    float   d = Clamp(Vector3DotProduct(n0, n), -1.0f, 1.0f);
    float   ang = acosf(d) * RAD2DEG;
    if (Vector3Length(axis) < 1e-4f) { axis = Vector3{1, 0, 0}; ang = (d > 0.0f) ? 0.0f : 180.0f; }
    DrawCircle3D(c, r, axis, ang, col);
}

// アンカー：引っ掛けられる場所だと一目で分かる輪。常にカメラを向くので形が崩れない。
static void DrawAnchors(Game& g) {
    const Player& pl = g.player;
    for (const WireAnchor& a : g.level.anchors) {
        if (a.pulled) continue;
        float dist = Vector3Distance(a.pos, pl.pos);
        float near = Sat(1.0f - dist / fmaxf(1.0f, g.p.wireRange));
        float y = a.pos.y + sinf(a.bob) * 0.10f;
        Vector3 c = Vector3{a.pos.x, y, a.pos.z};

        Color col = (a.flash > 0.0f) ? WHITE : Color{255, 225, 130, 255};
        unsigned char al = (unsigned char)(150 + 105 * near);
        col.a = al;

        Vector3 toCam = Vector3Subtract(g.rig.cam.position, c);
        DrawRingFacing(c, 0.55f, toCam, col);
        DrawRingFacing(c, 0.46f, toCam, col);
        DrawRingFacing(c, 0.70f + 0.10f * sinf(a.bob * 1.7f), toCam,
                       Color{col.r, col.g, col.b, (unsigned char)(al / 3)});
        DrawSphere(c, 0.10f + 0.05f * near, col);
        // 届く範囲に入ったら下向きの光を出して知らせる
        if (near > 0.001f) {
            DrawCylinderEx(Vector3{c.x, c.y - 0.55f, c.z}, Vector3{c.x, c.y - 0.55f - 1.2f * near, c.z},
                           0.06f, 0.0f, 6, Color{255, 235, 160, (unsigned char)(120 * near)});
        }
    }
}

static void DrawEnemies(Game& g) {
    for (const Enemy& e : g.level.enemies) {
        if (!e.alive) continue;
        Color col = e.flash > 0.0f ? WHITE : Color{190, 120, 245, 255};
        Vector3 size = Vector3Scale(e.half, 2.0f);
        DrawCubeV(e.pos, size, col);
        DrawCubeWiresV(e.pos, size, Color{80, 40, 120, 255});
        // 目（進行方向）
        Vector3 dir = Vector3Normalize(Vector3Subtract(e.b, e.a));
        if (e.dir < 0) dir = Vector3Negate(dir);
        Vector3 f = Vector3Add(e.pos, Vector3Scale(dir, e.half.x + 0.02f));
        DrawSphere(Vector3{f.x, e.pos.y + 0.12f, f.z}, 0.10f, Color{40, 20, 60, 255});
        // 踏める場所だと示す上面のライン
        DrawCubeV(Vector3{e.pos.x, e.pos.y + e.half.y + 0.02f, e.pos.z},
                  Vector3{size.x * 0.9f, 0.03f, size.z * 0.9f}, Color{255, 240, 120, 200});
    }
}

static void DrawGoal(Game& g) {
    Level& l = g.level;
    Color goalCol = l.cleared ? GOLD : Color{120, 255, 170, 255};
    for (int i = 0; i < 3; ++i) {
        float r = 1.1f + 0.12f * i;
        DrawCircle3D(Vector3{l.goal.x, l.goal.y + 1.0f, l.goal.z}, r,
                     Vector3{1, 0, 0}, 90.0f + sinf(g.time * 2.0f) * 6.0f, goalCol);
    }
    DrawCylinderEx(l.goal, Vector3{l.goal.x, l.goal.y + 6.0f, l.goal.z}, 0.35f, 0.05f, 10,
                   Color{120, 255, 170, 90});
}

void DrawLevel(Game& g) {
    DrawTerrain(g);
    DrawCollectibles(g);
    DrawGimmicks(g);
    DrawSlides(g);
    DrawAnchors(g);
    DrawEnemies(g);
    DrawGoal(g);

    if (g.debug.showCollision) {
        for (const Box& b : g.level.boxes) {
            if (b.kind == BOX_SCENERY) continue;
            DrawCubeWiresV(b.c, Vector3Scale(b.h, 2.0f), Color{255, 0, 128, 200});
        }
        for (const Enemy& e : g.level.enemies)
            if (e.alive) DrawCubeWiresV(e.pos, Vector3Scale(e.half, 2.0f), Color{255, 0, 128, 200});
        for (const Spring& s : g.level.springs)
            DrawCubeWiresV(s.pos, Vector3Scale(s.half, 2.0f), Color{255, 0, 128, 200});
    }
}
