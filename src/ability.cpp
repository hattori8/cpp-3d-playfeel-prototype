// ability.cpp ─ ステージ限定能力
//
// 能力ごとに Player へフィールドを増やさない。共通の AbilityState を1つ持ち、
// 種類ごとの処理はここで switch で振り分ける。継承は導入しない。
// 能力が 10〜20 種類を超えて switch 自体が問題になってから作り直す。
//
// 新しい能力を足す手順
//   1. AbilityType に1つ足す（game.h）
//   2. AbilityState の timer / value / phase に意味を割り当てる
//   3. UpdateXxxAbility() を書いてこの switch に足す
//   4. params.h に数値を足す
//   5. tests.cpp に Design Intent Test を足す（通常移動との差を数値で固定する）
#include "game.h"
#include "util.h"
#include <cmath>

const char* AbilityName(AbilityType type) {
    switch (type) {
        case AbilityType::Dash:   return "DASH";
        case AbilityType::Wire:   return "WIRE";
        case AbilityType::Rocket: return "ROCKET";
        case AbilityType::Mouse:  return "MOUSE";
        case AbilityType::Sponge: return "SPONGE";
        case AbilityType::Monkey: return "MONKEY";
        default:                  return "NONE";
    }
}

void GrantAbility(Game& g, AbilityType type) {
    AbilityState& a = g.player.ability;
    a.type     = type;
    a.active   = false;
    a.timer    = 0.0f;
    a.phase    = 0;
    a.cooldown = 0.0f;
    a.value    = (type == AbilityType::Dash) ? g.p.dashFuelMax : 0.0f;
    a.phase    = -1;
}

// 能力ゲージの残量比。HUD 用。
float AbilityFuelRatio(const Game& g) {
    const AbilityState& a = g.player.ability;
    if (a.type == AbilityType::Dash) return Sat(a.value / fmaxf(0.01f, g.p.dashFuelMax));
    if (a.type == AbilityType::Wire) return a.active ? 1.0f : 0.0f;
    return 0.0f;
}

// ══════════════════════════════════════════════ Dash
//
// AbilityState の使い方: value = 残り燃料(s) / timer = 今回のダッシュ経過(s)
//
// 前方へ一定速度で突進する。空中では上下速度を0へ寄せるので、水平移動距離を
// 稼ぐ手段になる（ホバーは「長く浮く」、ダッシュは「速く遠くへ」で役割を分ける）。
static void UpdateDashAbility(Game& g, float dt) {
    Player&       pl = g.player;
    AbilityState& a  = pl.ability;
    const Params& p  = g.p;

    // 着地で回復する。ただしダッシュ中は回復しない（押しっぱなしで無限に走れてしまう）
    if (pl.grounded && !a.active) a.value = p.dashFuelMax;
    a.cooldown = fmaxf(0.0f, a.cooldown - dt);

    bool want = g.in.abilityHeld && a.value > 0.0f && a.cooldown <= 0.0f;

    if (want && !a.active) {
        a.active = true;
        a.timer  = 0.0f;
        SpawnJetPuff(g, Vector3{pl.pos.x, pl.pos.y - pl.half.y, pl.pos.z});
    } else if (!want && a.active) {
        a.active   = false;
        a.cooldown = p.dashCooldown;
    }
    if (!a.active) return;

    a.timer += dt;
    a.value = fmaxf(0.0f, a.value - dt);
    if (a.value <= 0.0f) { a.active = false; a.cooldown = p.dashCooldown; }

    // 向いている方へ突進する（旋回は UpdatePlayerFacing 側で鈍くする）
    Vector3 fwd    = Vector3{sinf(pl.yaw), 0.0f, cosf(pl.yaw)};
    Vector3 target = Vector3Scale(fwd, p.dashSpeed);
    pl.vel.x = MoveTowardsF(pl.vel.x, target.x, p.dashAccel * dt);
    pl.vel.z = MoveTowardsF(pl.vel.z, target.z, p.dashAccel * dt);
    pl.vel.y = MoveTowardsF(pl.vel.y, 0.0f, 40.0f * dt);   // 水平に伸びる
    pl.hovering = false;

    // 体当たり。対象の種類は見ず、AbilityHit を投げるだけ。
    Vector3 hitPos = Vector3Add(pl.pos, Vector3Scale(fwd, 0.5f));
    for (int i = 0; i < (int)g.level.targets.size(); ++i) {
        const Target& t = g.level.targets[i];
        if (!t.alive) continue;
        if (Vector3Distance(hitPos, t.pos) > p.dashHitRadius + t.half.x) continue;
        PushInteraction(g, Interaction{InteractionType::AbilityHit, PLAYER_ID,
                                       MakeId(OBJ_TARGET, i), t.pos, fwd, p.punchImpulse});
    }
    for (int i = 0; i < (int)g.level.enemies.size(); ++i) {
        const Enemy& e = g.level.enemies[i];
        if (!e.alive) continue;
        if (Vector3Distance(hitPos, e.pos) > p.dashHitRadius + e.half.x) continue;
        PushInteraction(g, Interaction{InteractionType::AbilityHit, PLAYER_ID,
                                       MakeId(OBJ_ENEMY, i), e.pos, fwd, p.punchImpulse});
    }

    if (GetRandomValue(0, 1) == 0)
        SpawnJetPuff(g, Vector3{pl.pos.x, pl.pos.y - pl.half.y * 0.4f, pl.pos.z});
}

// ══════════════════════════════════════════════ Wire（ワイヤーアクション）
//
// AbilityState の使い方: value = ロープの長さ / phase = 掴んでいるアンカー / timer = 経過
//
// ・押した瞬間に、狙いに近いアンカーを1つ選んで Interaction を投げる
// ・「掴んだ結果どうなるか」はここでは決めない（interaction.cpp が決める）
//   固定アンカー → 自分が振られる / 重い物のアンカー → 物が飛んでくる
// ・掴んでいる間は振り子。押し続けるとロープをたぐって登る。離すと勢いのまま飛ぶ。

// 狙いに一番合うアンカーを選ぶ。見つからなければ -1。
static int PickAnchor(const Game& g) {
    const Player& pl = g.player;
    const Params& p  = g.p;
    // 狙いは「水平方向のカメラ前方」で見る。高さを混ぜると、真上の近いアンカーより
    // 遠くの低いアンカーの方が正面に見えてしまい、掴む相手が読めなくなる。
    Vector3 aim = Vector3{sinf(g.rig.yaw), 0.0f, cosf(g.rig.yaw)};

    int   best = -1;
    float bestScore = -1e9f;
    for (int i = 0; i < (int)g.level.anchors.size(); ++i) {
        const WireAnchor& a = g.level.anchors[i];
        if (a.pulled) continue;
        Vector3 d = Vector3Subtract(a.pos, pl.pos);
        float dist = Vector3Length(d);
        if (dist > p.wireRange || dist < 0.5f) continue;

        Vector3 flat = Vector3{d.x, 0.0f, d.z};
        float   flatLen = Vector3Length(flat);
        // ほぼ真上なら向きは問わない（真下から掴めた方が素直）
        float dot = (flatLen < 1.5f) ? 1.0f
                                     : Vector3DotProduct(Vector3Scale(flat, 1.0f / flatLen), aim);
        if (dot < p.wireAimDot) continue;

        // 向きが合っているほど、近いほど高得点（近さを強めに見る）
        float score = dot * 2.0f - (dist / fmaxf(1.0f, p.wireRange)) * 2.5f;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best;
}

static void ReleaseWire(Game& g) {
    Player& pl = g.player;
    AbilityState& a = pl.ability;
    if (!a.active) return;
    a.active = false;
    a.phase  = -1;
    pl.vel.y += g.p.wireReleaseBoost;      // 離した瞬間に少し伸びる
    pl.jumpCutArmed = false;               // 外から与えた速度は打ち切らない
    pl.timeSinceJump = 0.0f;
    PushEvent(g, GameEventType::WireReleased, pl.pos, PLAYER_ID,
              (int)sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z));
}

static void UpdateWireAbility(Game& g, float dt) {
    Player&       pl = g.player;
    AbilityState& a  = pl.ability;
    const Params& p  = g.p;

    a.cooldown = fmaxf(0.0f, a.cooldown - dt);

    // ── 掴む
    if (!a.active && g.in.abilityPressed && a.cooldown <= 0.0f) {
        int idx = PickAnchor(g);
        if (idx >= 0) {
            PushInteraction(g, Interaction{InteractionType::Wire, PLAYER_ID,
                                           MakeId(OBJ_ANCHOR, idx), g.level.anchors[idx].pos,
                                           Vector3Normalize(Vector3Subtract(g.level.anchors[idx].pos,
                                                                            pl.pos)),
                                           p.wirePullSpeed});
        }
        a.cooldown = 0.12f;
    }

    if (!a.active) return;
    if (a.phase < 0 || a.phase >= (int)g.level.anchors.size()) { a.active = false; return; }

    // ── 離す
    if (!g.in.abilityHeld) { ReleaseWire(g); return; }

    const WireAnchor& anc = g.level.anchors[a.phase];
    a.timer += dt;

    // ぶら下がり中の重力と空中制御（ホバーは使えない）
    pl.hovering = false;
    pl.vel.y -= p.gravity * p.wireGravityScale * dt;
    if (pl.wishMag > 0.05f) {
        pl.vel.x += pl.wishDir.x * p.wireAirAccel * dt;
        pl.vel.z += pl.wishDir.z * p.wireAirAccel * dt;
    }

    // 押し続けている間はロープをたぐる（登れる／振りが速くなる）
    a.value = fmaxf(p.wireMinLength, a.value - p.wireReelSpeed * dt);

    // 位置を進めてから、ロープの長さで拘束する（張力）
    pl.pos = Vector3Add(pl.pos, Vector3Scale(pl.vel, dt));
    Vector3 toA = Vector3Subtract(anc.pos, pl.pos);
    float   d   = Vector3Length(toA);
    if (d > a.value && d > 0.001f) {
        Vector3 n = Vector3Scale(toA, 1.0f / d);        // プレイヤー → アンカー
        pl.pos = Vector3Subtract(anc.pos, Vector3Scale(n, a.value));
        float radial = Vector3DotProduct(pl.vel, n);
        if (radial < 0.0f) pl.vel = Vector3Subtract(pl.vel, Vector3Scale(n, radial));
        pl.vel = Vector3Scale(pl.vel, 1.0f - Clamp(p.wireDamping * dt, 0.0f, 0.9f));
    }

    // 向きは進行方向へ
    if (fabsf(pl.vel.x) + fabsf(pl.vel.z) > 0.5f) pl.yaw = atan2f(pl.vel.x, pl.vel.z);

    pl.grounded = false;
    pl.jumpCutArmed = false;

    // ジャンプボタンで飛び降りる（振り子から離脱して跳ぶ）
    if (g.in.jumpPressed) {
        ReleaseWire(g);
        pl.vel.y += p.jumpSpeed * 0.55f;
        return;
    }
    // 地面にぶつかったら自然に解除（後段の衝突解決が接地させる）
    if (a.timer > 0.15f && pl.pos.y < anc.pos.y - a.value - 0.5f) ReleaseWire(g);
}

// ══════════════════════════════════════════════ 振り分け

void UpdatePlayerAbility(Game& g, float dt) {
    switch (g.player.ability.type) {
        case AbilityType::None:
            break;

        case AbilityType::Dash:
            UpdateDashAbility(g, dt);
            break;

        case AbilityType::Wire:
            UpdateWireAbility(g, dt);
            break;

        // ここから下は枠だけ。実装する時は UpdateXxxAbility() を書いて差し込む。
        case AbilityType::Rocket:
        case AbilityType::Mouse:
        case AbilityType::Sponge:
        case AbilityType::Monkey:
            break;
    }
}

// ══════════════════════════════════════════════ 見た目

void DrawAbilityFx(Game& g) {
    const Player& pl = g.player;
    if (!pl.ability.active) return;

    // ワイヤー：張ったロープを描く。たるみを少し入れると「紐」に見える。
    if (pl.ability.type == AbilityType::Wire) {
        int idx = pl.ability.phase;
        if (idx < 0 || idx >= (int)g.level.anchors.size()) return;
        Vector3 anc = g.level.anchors[idx].pos;
        Vector3 hand = Vector3{pl.pos.x, pl.pos.y + 0.30f, pl.pos.z};
        float len = Vector3Distance(hand, anc);
        float slack = fmaxf(0.0f, pl.ability.value - len) * 0.35f;

        const int N = 10;
        Vector3 prev = hand;
        for (int i = 1; i <= N; ++i) {
            float t = (float)i / N;
            Vector3 q = Vector3Lerp(hand, anc, t);
            q.y -= slack * sinf(t * PI);                 // たるみ
            DrawCylinderEx(prev, q, 0.045f, 0.045f, 5, Color{255, 245, 200, 255});
            prev = q;
        }
        DrawSphere(hand, 0.10f, Color{255, 250, 220, 255});
        DrawSphere(anc,  0.16f, Color{255, 240, 170, 255});
        return;
    }

    if (pl.ability.type == AbilityType::Dash) {
        Vector3 back = Vector3{-sinf(pl.yaw), 0.0f, -cosf(pl.yaw)};
        for (int i = 1; i <= 4; ++i) {
            Vector3 q = Vector3Add(pl.pos, Vector3Scale(back, 0.42f * (float)i));
            unsigned char al = (unsigned char)(120 - 25 * i);
            DrawSphere(q, 0.34f - 0.05f * i, Color{160, 220, 255, al});
        }
        // 進行方向のリング
        Vector3 fwd = Vector3{sinf(pl.yaw), 0.0f, cosf(pl.yaw)};
        Vector3 c   = Vector3Add(pl.pos, Vector3Scale(fwd, 0.55f));
        DrawCircle3D(c, 0.55f, fwd, 90.0f, Color{200, 240, 255, 180});
    }
}
