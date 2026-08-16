// tests.cpp ─ 自動テスト（ASTRO_TEST=1）
//
// 「機能が動くか」だけでなく「その遊びに意味があるか」を数値で固定する。
// 後者を Design Intent Test と呼び、[intent] を付けて区別している。
// パラメータを気持ちよくしたつもりで設計を壊した時に、ここが落ちて気づける。
#include "sim.h"
#include "util.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

static int  s_fails = 0;
static void Check(const char* tag, const char* name, bool ok, const char* detail) {
    printf("  %-8s %-26s %s   %s\n", tag, name, ok ? "PASS" : "FAIL", detail);
    if (!ok) s_fails++;
}
#define CHECK(name, ok, detail)  Check("[func]",   name, ok, detail)
#define INTENT(name, ok, detail) Check("[intent]", name, ok, detail)

// 敵を止めて置き直す（メカニクス単体を決定的に検証するため）
static void FreezeEnemyAt(Game& g, int i, Vector3 pos) {
    if (i < 0 || i >= (int)g.level.enemies.size()) return;
    Enemy& e = g.level.enemies[i];
    e.a = e.b = pos;
    e.pos = pos;
    e.speed = 0.0f;
    e.alive = true;
    e.t = 0.0f;
}

int RunSelfTest(Game& g) {
    s_fails = 0;
    printf("\n=== astro_proto self test ===\n");

    // ── 1. 歩行
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    Sim(g, 1.0f, SimCfg{{0, 1}});
    CHECK("walk forward", g.player.pos.z > 6.0f && g.player.grounded,
          TextFormat("z=%.2f grounded=%d", g.player.pos.z, (int)g.player.grounded));

    // ── 2. コイン（Touch → Reaction → Event → Stats の一連が通るか）
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    Sim(g, 1.2f, SimCfg{{0, 1}});
    CHECK("coin pickup", g.stats.coinsTaken > 0, TextFormat("coins=%d", g.stats.coinsTaken));

    // ── 3. 段差の自動乗り越え
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 8.0f}, true);
    Sim(g, 2.0f, SimCfg{{0, 1}});
    CHECK("auto step up", g.player.pos.y > 1.4f && g.player.pos.z > 16.0f,
          TextFormat("y=%.2f z=%.2f", g.player.pos.y, g.player.pos.z));

    // ── 4. 登れない壁で止まる
    BuildLevel(g); PlacePlayer(g, {5.0f, 1.65f, 23.0f}, true);
    Sim(g, 1.5f, SimCfg{{0, 1}});
    CHECK("wall blocks", g.player.pos.z < 26.4f, TextFormat("z=%.2f", g.player.pos.z));

    // ── 5. 中央のすき間は通れる
    BuildLevel(g); PlacePlayer(g, {0, 1.65f, 24.0f}, true);
    Sim(g, 0.55f, SimCfg{{0, 1}});
    CHECK("wall gap passable", g.player.pos.z > 27.6f, TextFormat("z=%.2f", g.player.pos.z));

    // ── 6. 可変ジャンプ高度（押した長さで高さが変わる）
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    SimOut tap = Sim(g, 2.0f, SimCfg{{0, 0}, 0.06f, false, 0.0f, false, true});
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    SimOut full = Sim(g, 2.0f, SimCfg{{0, 0}, 0.26f, false, 0.0f, false, true});
    INTENT("variable jump height", (full.maxY - tap.maxY) > 0.6f && (full.maxY - 1.2f) > 2.0f,
           TextFormat("tap=+%.2fm full=+%.2fm", tap.maxY - 1.2f, full.maxY - 1.2f));

    // ── 7. ホバーは「長く浮く」手段として意味があるか
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, -6.0f}, true);
    SimOut noHover = Sim(g, 4.0f, SimCfg{{0, 1}, 0.26f, false, 0.0f, false, true});
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, -6.0f}, true);
    SimOut hover = Sim(g, 4.0f, SimCfg{{0, 1}, 2.5f, false, 0.0f, false, true});
    INTENT("hover extends air",
           hover.airTime > noHover.airTime + 1.0f &&
               (hover.horizontalDistance - noHover.horizontalDistance) > 5.0f,
           TextFormat("air %.2f->%.2fs  dist %.1f->%.1fm", noHover.airTime, hover.airTime,
                      noHover.horizontalDistance, hover.horizontalDistance));

    // ── 8. 左右の向き（右手系なので +X は画面左。ここが反転すると「左右逆」になる）
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    Sim(g, 0.6f, SimCfg{{1, 0}});
    float strafeX = g.player.pos.x;
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    g.in = Input{}; g.in.look.x = 1.0f;
    UpdateRigInput(g, 0.5f);
    INTENT("right input goes right", strafeX < -1.0f && g.rig.yaw < 0.0f,
           TextFormat("strafe x=%.2f camYaw=%.3f", strafeX, g.rig.yaw));

    // ── 9. 地上パンチ（Punch → Break → TargetBroken → Stats）
    BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 2.0f}, true);
    Sim(g, 1.0f, SimCfg{{0, 0}, 0.0f, false, 0.4f});
    CHECK("ground punch breaks", g.stats.targetsBroken >= 1,
          TextFormat("broken=%d", g.stats.targetsBroken));

    // ── 10. ホバー中のレーザー
    BuildLevel(g); PlacePlayer(g, {0, 4.0f, 35.0f}, false);
    g.player.timeSinceJump = 99.0f;
    Sim(g, 1.4f, SimCfg{{0, 0}, 2.0f, true});
    CHECK("hover laser breaks", g.stats.targetsBroken >= 1,
          TextFormat("broken=%d", g.stats.targetsBroken));

    // ── 11. 動く床に運ばれる
    BuildLevel(g); PlacePlayer(g, {-4.5f, 2.35f, 51.5f}, true);
    Sim(g, 1.5f, SimCfg{{0, 0}});
    CHECK("ride moving platform", g.player.pos.x > -3.5f && g.player.pos.y > 1.5f,
          TextFormat("x=%.2f y=%.2f", g.player.pos.x, g.player.pos.y));

    // ── 12. 落下復帰
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 35.0f}, false);
    Sim(g, 3.0f, SimCfg{{0, 0}});
    CHECK("fall respawn", g.player.pos.y > -1.0f, TextFormat("y=%.2f", g.player.pos.y));

    // ══════════════════ 能力（Ability）

    // ── 13. 能力アイテムを拾うと能力が入る
    BuildLevel(g); PlacePlayer(g, {-6.5f, 1.2f, 1.9f}, true);
    Sim(g, 0.2f, SimCfg{});
    CHECK("pickup grants ability", g.player.ability.type == AbilityType::Dash,
          TextFormat("ability=%s", AbilityName(g.player.ability.type)));

    // ── 14. ダッシュは通常移動より速い
    BuildLevel(g); GrantAbility(g, AbilityType::Dash); PlacePlayer(g, {0, 1.2f, -7.0f}, true);
    SimOut run = Sim(g, 0.6f, SimCfg{{0, 1}});
    BuildLevel(g); GrantAbility(g, AbilityType::Dash); PlacePlayer(g, {0, 1.2f, -7.0f}, true);
    SimOut dash = Sim(g, 0.6f, SimCfg{{0, 1}, 0.0f, false, 0.0f, true});
    INTENT("dash is faster", dash.maxSpeed > run.maxSpeed * 1.4f,
           TextFormat("run=%.1f dash=%.1f m/s", run.maxSpeed, dash.maxSpeed));

    // ── 15. ダッシュを使う意味（同じ時間で稼げる距離）
    INTENT("dash covers distance", (dash.horizontalDistance - run.horizontalDistance) > 2.5f,
           TextFormat("run=%.1fm dash=%.1fm", run.horizontalDistance, dash.horizontalDistance));

    // ── 16. 押しっぱなしにできない（燃料が尽きて必ず途切れる）
    BuildLevel(g); GrantAbility(g, AbilityType::Dash); PlacePlayer(g, {0, 1.2f, -8.0f}, true);
    Sim(g, 0.75f, SimCfg{{0, 1}, 0.0f, false, 0.0f, true});
    bool broke = !g.player.ability.active;
    Sim(g, 0.40f, SimCfg{{0, 1}, 0.0f, false, 0.0f, true});   // クールダウン後は再開できる
    INTENT("dash cannot be held", broke && g.player.ability.active,
           TextFormat("stopped=%d resumed=%d", (int)broke, (int)g.player.ability.active));

    // ══════════════════ ギミック（Interaction / Reaction）

    // ── 17. バネ（Stomp → Bounce）はジャンプより高く飛べる
    BuildLevel(g); PlacePlayer(g, {-5.5f, 3.0f, 25.2f}, false);
    SimOut spring = Sim(g, 2.5f, SimCfg{{0, 0}});
    INTENT("spring beats jump", spring.maxY > 6.0f && g.stats.springBounces >= 1,
           TextFormat("maxY=%.2f bounces=%d", spring.maxY, g.stats.springBounces));

    // ── 18. ゲートは閉じている間は通れない
    BuildLevel(g); PlacePlayer(g, {5.5f, 1.6f, 59.0f}, true);
    Sim(g, 1.5f, SimCfg{{0, 1}});
    float blockedZ = g.player.pos.z;
    CHECK("gate blocks", blockedZ < 61.2f, TextFormat("z=%.2f", blockedZ));

    // ── 19. ボタンを叩くとゲートが開き、通れるようになる
    BuildLevel(g); PlacePlayer(g, {-5.0f, 1.5f, 56.3f}, true);
    Sim(g, 0.6f, SimCfg{{0, 0}, 0.0f, false, 0.3f});
    bool pressed = g.stats.buttonHits >= 1;
    Sim(g, 1.0f, SimCfg{});                     // ゲートが動く時間
    int   gateIdx  = g.level.buttons.empty() ? -1 : g.level.buttons[0].gateBoxIndex;
    float gateDrop = (gateIdx >= 0) ? g.level.buttons[0].gateClosed.y - g.level.boxes[gateIdx].c.y
                                    : 0.0f;
    PlacePlayer(g, {5.5f, 1.6f, 59.0f}, true);
    Sim(g, 1.2f, SimCfg{{0, 1}});
    INTENT("button opens gate", pressed && gateDrop > 1.0f && g.player.pos.z > 62.0f,
           TextFormat("hits=%d drop=%.2f z=%.2f", g.stats.buttonHits, gateDrop, g.player.pos.z));

    // ══════════════════ ウォータースライダー（Ride）

    // 入口に置いて、乗るまで少し待つヘルパー
    auto boardSlide = [&](Game& gg) {
        BuildLevel(gg);
        const WaterSlide& s = gg.level.slides[0];
        Vector3 c = SlidePoint(s, 0.6f);
        PlacePlayer(gg, Vector3{c.x, c.y - s.radius * 0.55f + gg.player.half.y + 0.02f, c.z}, false);
        Sim(gg, 0.10f, SimCfg{});
        return gg.player.slide.active;
    };

    // ── 20. 入口に触れると乗る
    bool boarded = boardSlide(g);
    CHECK("slide boards on touch", boarded && g.stats.slideRides >= 1,
          TextFormat("active=%d rides=%d", (int)boarded, g.stats.slideRides));

    // ── 21. 入力なしで最後まで運ばれる（=「乗り物」として成立している）
    float slideLen = g.level.slides[0].length;
    SimOut ride = Sim(g, 8.0f, SimCfg{});
    INTENT("slide needs no input",
           !g.player.slide.active && ride.horizontalDistance > 25.0f && g.player.pos.z > 55.0f,
           TextFormat("len=%.1fm moved=%.1fm z=%.1f", slideLen, ride.horizontalDistance,
                      g.player.pos.z));

    // ── 22. 歩くより明確に速い（乗り物として成立しているか）
    INTENT("slide beats walking", ride.maxSpeed > g.p.moveSpeed * 1.7f,
           TextFormat("ride %.1f m/s vs walk %.1f", ride.maxSpeed, g.p.moveSpeed));

    // ── 23. 勾配が効いている（水流だけのベルトコンベアになっていないか）
    //        どこかで基準速度を超えていれば、坂で稼いだ分がある。
    INTENT("slope adds speed", ride.maxSpeed > g.p.slideFlowSpeed * 1.05f,
           TextFormat("peak %.1f > flow %.1f", ride.maxSpeed, g.p.slideFlowSpeed));

    // ── 23. 出口で勢いが残る（落とされるのではなく射出される）
    //        降りた瞬間に測る。時間が経つと減速するので、そこは測る対象ではない。
    boardSlide(g);
    for (int i = 0; i < 600 && g.player.slide.active; ++i) Sim(g, 1.0f / 60.0f, SimCfg{});
    float exitSpeed = sqrtf(g.player.vel.x * g.player.vel.x + g.player.vel.z * g.player.vel.z);
    INTENT("slide keeps momentum", exitSpeed > g.p.moveSpeed * 1.3f,
           TextFormat("exit %.1f m/s (walk %.1f)", exitSpeed, g.p.moveSpeed));

    // ── 24. 左右に寄せられる
    boardSlide(g);
    SimOut leftRun = Sim(g, 1.2f, SimCfg{{-1, 0}});
    boardSlide(g);
    SimOut rightRun = Sim(g, 1.2f, SimCfg{{1, 0}});
    float lateral = Vector3Distance(leftRun.endPos, rightRun.endPos);
    INTENT("slide steering works", lateral > 1.2f, TextFormat("gap=%.2fm", lateral));

    // ── 25. 途中で飛び降りられる（押した直後に測る）
    boardSlide(g);
    Sim(g, 1.0f, SimCfg{});
    Sim(g, 0.05f, SimCfg{{0, 0}, 0.20f});
    CHECK("slide jump off", !g.player.slide.active && g.player.vel.y > 4.0f,
          TextFormat("active=%d vy=%.1f", (int)g.player.slide.active, g.player.vel.y));

    // ══════════════════ ワイヤーアクション（Pull）

    // 固定アンカーの番号（先頭は岩の Heavy アンカー）
    auto firstFixedAnchor = [](const Game& gg) {
        for (int i = 0; i < (int)gg.level.anchors.size(); ++i)
            if (gg.level.anchors[i].kind == ANCHOR_FIXED) return i;
        return -1;
    };

    // ── 26. 狙って掴める
    BuildLevel(g); GrantAbility(g, AbilityType::Wire);
    PlacePlayer(g, {0, 2.2f, 44.5f}, true);
    Sim(g, 0.20f, SimCfg{{0, 0}, 0.0f, false, 0.0f, true});
    bool grabbed = g.player.ability.active;
    CHECK("wire grabs anchor", grabbed && g.stats.wireGrabs >= 1,
          TextFormat("active=%d grabs=%d anchor=%d", (int)grabbed, g.stats.wireGrabs,
                     g.player.ability.phase));

    // ── 27. 振り子として加速する（高さが速さに変わる）
    //        足場の上から掴むと壁に擦って測れないので、空中から掴んで単体で見る。
    BuildLevel(g); GrantAbility(g, AbilityType::Wire);
    PlacePlayer(g, {0, 8.0f, 42.0f}, false);
    SimOut swing = Sim(g, 1.5f, SimCfg{{0, 1}, 0.0f, false, 0.0f, true});
    INTENT("wire swings", swing.maxSpeed > g.p.moveSpeed * 1.25f,
           TextFormat("peak %.1f m/s (walk %.1f)", swing.maxSpeed, g.p.moveSpeed));

    // ── 28. ワイヤーはジャンプより高い所へ行ける（登る手段として意味がある）
    BuildLevel(g); PlacePlayer(g, {0, 2.2f, 44.5f}, true);
    SimOut plainJump = Sim(g, 2.5f, SimCfg{{0, 1}, 0.26f, false, 0.0f, false, true});
    BuildLevel(g); GrantAbility(g, AbilityType::Wire);
    PlacePlayer(g, {0, 2.2f, 44.5f}, true);
    SimOut wireUp = Sim(g, 3.0f, SimCfg{{0, 1}, 0.0f, false, 0.0f, true});
    INTENT("wire climbs higher", (wireUp.maxY - plainJump.maxY) > 1.5f,
           TextFormat("jump=%.1fm wire=%.1fm", plainJump.maxY, wireUp.maxY));

    // ── 29. 【驚き】同じ操作でも、相手が動かせる物なら "向こうが飛んでくる"
    BuildLevel(g); GrantAbility(g, AbilityType::Wire);
    PlacePlayer(g, {-11.5f, 1.6f, 21.0f}, true);
    g.rig.yaw = -PI * 0.5f;                       // 岩の方を向く
    int   rockBox = -1;
    for (const WireAnchor& a : g.level.anchors)
        if (a.kind == ANCHOR_HEAVY) rockBox = a.boxIndex;
    float rockX0 = (rockBox >= 0) ? g.level.boxes[rockBox].c.x : 0.0f;
    Vector3 pl0  = g.player.pos;
    Sim(g, 0.8f, SimCfg{{0, 0}, 0.0f, false, 0.0f, true});
    float rockMoved   = (rockBox >= 0) ? (g.level.boxes[rockBox].c.x - rockX0) : 0.0f;
    float playerMoved = Vector3Distance(g.player.pos, pl0);
    INTENT("heavy anchor comes to you",
           g.stats.surprises >= 1 && rockMoved > 2.0f && playerMoved < 2.0f,
           TextFormat("rock+%.1fm player+%.1fm surprises=%d", rockMoved, playerMoved,
                      g.stats.surprises));

    // ── 30. 岩が退いた跡は通れる（驚きがご褒美につながっている）
    PlacePlayer(g, {-13.0f, 1.6f, 21.0f}, true);
    g.rig.yaw = -PI * 0.5f;
    Sim(g, 1.6f, SimCfg{{0, 1}});
    CHECK("surprise opens the way", g.player.pos.x < -16.0f,
          TextFormat("x=%.1f", g.player.pos.x));

    // ── 31. 掴んでいない時に撃っても何も起きない（誤爆しない）
    BuildLevel(g); GrantAbility(g, AbilityType::Wire);
    PlacePlayer(g, {0, 1.2f, 0}, true);
    Sim(g, 0.5f, SimCfg{{0, 0}, 0.0f, false, 0.0f, true});
    CHECK("wire misses safely", !g.player.ability.active && g.player.grounded,
          TextFormat("active=%d grounded=%d", (int)g.player.ability.active,
                     (int)g.player.grounded));
    (void)firstFixedAnchor;

    // ══════════════════ 敵（Damage / Break）

    // ── 20. 上から踏むと倒せて、跳ね返る
    BuildLevel(g); FreezeEnemyAt(g, 0, {6.0f, 1.55f, 20.0f});
    PlacePlayer(g, {6.0f, 3.4f, 20.0f}, false);
    Sim(g, 0.8f, SimCfg{{0, 0}});
    CHECK("stomp defeats enemy", g.stats.enemiesDefeated >= 1,
          TextFormat("defeated=%d", g.stats.enemiesDefeated));

    // ── 21. 横から触ると弾かれる（無敵時間が入る）
    BuildLevel(g); FreezeEnemyAt(g, 0, {6.0f, 1.55f, 20.0f});
    PlacePlayer(g, {6.0f, 1.6f, 17.5f}, true);
    Sim(g, 1.0f, SimCfg{{0, 1}});
    CHECK("enemy damages player", g.stats.damageTaken >= 1 && g.player.invuln > 0.0f,
          TextFormat("dmg=%d invuln=%.2f", g.stats.damageTaken, g.player.invuln));

    // ── 22. 無敵時間中は連続で食らわない
    int dmgAfter = g.stats.damageTaken;
    Sim(g, 0.4f, SimCfg{{0, 1}});
    INTENT("invuln blocks repeat", g.stats.damageTaken == dmgAfter,
           TextFormat("dmg %d -> %d", dmgAfter, g.stats.damageTaken));

    // ── 23. ダッシュの体当たりでもターゲットを壊せる（AbilityHit）
    BuildLevel(g); GrantAbility(g, AbilityType::Dash);
    PlacePlayer(g, {3.5f, 1.0f, 1.0f}, true);
    Sim(g, 0.8f, SimCfg{{0, 1}, 0.0f, false, 0.0f, true});
    CHECK("dash hit breaks target", g.stats.targetsBroken >= 1,
          TextFormat("broken=%d", g.stats.targetsBroken));

    // ══════════════════ 全体

    // ── 攻撃の手応え：命中でエフェクトが出て、放っておけば消える（溜まらない）
    BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 2.0f}, true);
    Sim(g, 0.12f, SimCfg{{0, 0}, 0.0f, false, 0.3f});   // 当てた直後に見る
    int fxOnHit = (int)g.effects.size();
    Sim(g, 2.0f, SimCfg{});
    CHECK("hit spawns effects", fxOnHit > 0 && g.effects.empty(),
          TextFormat("onHit=%d after=%d", fxOnHit, (int)g.effects.size()));

    // ══════════════════ 破片（world 側の物理 / Jolt）
    //
    // ここで固定したいのは「物理が遊びに触っていないこと」。
    // 破片は見た目と手応えのためだけに居て、届く・届かないを変えてはいけない。

    // ── 26. 壊すと破片が出る
    BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 2.0f}, true);
    Sim(g, 0.30f, SimCfg{{0, 0}, 0.0f, false, 0.3f});
    int debrisOnBreak = DebrisCount(g);
    CHECK("break spawns debris", debrisOnBreak > 0, TextFormat("debris=%d", debrisOnBreak));

    // ── 27. 破片は必ず静止する（転がり続けない）
    Sim(g, 4.0f, SimCfg{});
    INTENT("debris settles", AllDebrisAsleep(g) && DebrisCount(g) > 0,
           TextFormat("asleep=%d n=%d", (int)AllDebrisAsleep(g), DebrisCount(g)));

    // ── 28. 破片は必ず消える（積み上がってフレームを食い潰さない）
    Sim(g, 9.0f, SimCfg{});
    CHECK("debris disappears", DebrisCount(g) == 0, TextFormat("n=%d", DebrisCount(g)));

    // ── 29. 破片は通行を妨げない ★この層を入れた前提そのもの
    //     同じ操作なら、破片があってもなくても同じ場所に着く。
    //     ここが落ちたら物理がゲームプレイに侵食している。
    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    Sim(g, 1.0f, SimCfg{{0, 1}});
    Vector3 cleanEnd = g.player.pos;

    BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
    SpawnDebrisBurst(g, Vector3{0, 1.5f, 3.0f}, Vector3{0, 0, 0}, 40, 6.0f, WHITE);
    Sim(g, 1.0f, SimCfg{{0, 1}});
    float blockDiff = Vector3Distance(cleanEnd, g.player.pos);
    INTENT("debris never blocks", blockDiff < 0.001f,
           TextFormat("diff=%.4fm n=%d", blockDiff, DebrisCount(g)));

    // ── 30. 同じ操作なら破片も同じ結果になる（テストが再現する前提）
    BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 2.0f}, true);
    Sim(g, 1.5f, SimCfg{{0, 0}, 0.0f, false, 0.3f});
    int     runA = DebrisCount(g);
    Vector3 posA = g.debris.empty() ? Vector3{0, 0, 0} : g.debris[0].pos;

    BuildLevel(g); PlacePlayer(g, {3.5f, 1.0f, 2.0f}, true);
    Sim(g, 1.5f, SimCfg{{0, 0}, 0.0f, false, 0.3f});
    int     runB = DebrisCount(g);
    Vector3 posB = g.debris.empty() ? Vector3{0, 0, 0} : g.debris[0].pos;
    INTENT("physics is repeatable", runA == runB && Vector3Distance(posA, posB) < 1e-4f,
           TextFormat("n %d=%d  drift=%.5f", runA, runB, Vector3Distance(posA, posB)));

    // ── 24. キューが毎フレーム空になる（積み残しがない）
    CHECK("queues drained",
          g.interactions.items.empty() && g.events.items.empty(),
          TextFormat("interactions=%d events=%d", (int)g.interactions.items.size(),
                     (int)g.events.items.size()));

    // ── 25. 長時間回して破綻しない
    BuildLevel(g); GrantAbility(g, AbilityType::Dash); PlacePlayer(g, {0, 1.2f, 0}, true);
    for (int i = 0; i < 20; ++i)
        Sim(g, 8.0f, SimCfg{{(i % 2) ? 1.0f : -1.0f, 1}, 0.4f, true, 0.5f, (i % 3) == 0});
    const Player& pl = g.player;
    bool sane = fabsf(pl.pos.x) < 500.0f && fabsf(pl.pos.z) < 500.0f && pl.pos.y > -100.0f &&
                pl.pos.y == pl.pos.y && g.particles.size() < 20000;
    CHECK("long run stability", sane,
          TextFormat("pos=%.1f,%.1f,%.1f particles=%d", pl.pos.x, pl.pos.y, pl.pos.z,
                     (int)g.particles.size()));

    // ── 破片の負荷を測る（ASTRO_BENCH=1）
    //
    // 物理を足したら「毎フレーム何ミリ秒か」を必ず測る。60fps の予算は 16.6ms で、
    // そのうち破片に何割払っているかを知らないままパラメータは決められない。
    if (getenv("ASTRO_BENCH")) {
        struct { const char* label; int n; } cases[] = {{"debris 0", 0}, {"debris 60", 60},
                                                        {"debris 140", 140}};
        for (auto& c : cases) {
            BuildLevel(g); PlacePlayer(g, {0, 1.2f, 0}, true);
            if (c.n > 0) SpawnDebrisBurst(g, Vector3{0, 3.0f, 2.0f}, Vector3{0, 0, 0},
                                          c.n, 6.0f, WHITE);
            const int frames = 600;
            double t0 = GetTime();
            for (int i = 0; i < frames; ++i) UpdateWorldPhysics(g, 1.0f / 60.0f);
            double ms = (GetTime() - t0) * 1000.0 / frames;
            printf("  [bench]  %-10s %.3f ms/frame  (60fps 予算 16.6ms の %.1f%%)\n",
                   c.label, ms, ms / 16.6 * 100.0);
        }
    }

    // ── CSV 出力（ASTRO_CSV=1）。ジャンプ曲線の変化を目で確認するため。
    if (getenv("ASTRO_CSV")) {
        struct { const char* file; SimCfg cfg; } runs[] = {
            {"curve_jump_tap.csv",  SimCfg{{0, 1}, 0.06f, false, 0.0f, false, false, "curve_jump_tap.csv"}},
            {"curve_jump_full.csv", SimCfg{{0, 1}, 0.26f, false, 0.0f, false, false, "curve_jump_full.csv"}},
            {"curve_hover.csv",     SimCfg{{0, 1}, 2.50f, false, 0.0f, false, false, "curve_hover.csv"}},
            {"curve_dash.csv",      SimCfg{{0, 1}, 0.00f, false, 0.0f, true,  false, "curve_dash.csv"}},
        };
        for (auto& r : runs) {
            BuildLevel(g); GrantAbility(g, AbilityType::Dash);
            PlacePlayer(g, {0, 1.2f, -8.0f}, true);
            Sim(g, 2.5f, r.cfg);
            printf("  [csv]    %s\n", r.file);
        }
    }

    printf("=== %s (%d failed) ===\n\n", s_fails == 0 ? "ALL PASS" : "FAILURES", s_fails);
    return s_fails;
}
