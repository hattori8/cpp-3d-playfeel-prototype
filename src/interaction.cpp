// interaction.cpp ─ Interaction の解決（= 対象側の Reaction を決める）と Event の配送
//
//   Player ──Punch/Laser/Touch/Stomp/AbilityHit──▶ Interaction キュー
//                                                        │
//                                          対象の種類ごとに Reaction を決定
//                                                        │
//                                                   Event キュー
//                                                        │
//                                            ┌───────────┴───────────┐
//                                          Stats                Presentation
//
// 攻撃側は対象が何かを知らない。ここが唯一「種類ごとの反応」を知っている場所。
#include "game.h"
#include "util.h"
#include <cmath>

void PushInteraction(Game& g, const Interaction& it) {
    g.interactions.items.push_back(it);
}

void PushEvent(Game& g, GameEventType type, Vector3 pos, int sourceId, int value) {
    GameEvent e;
    e.type = type;
    e.position = pos;
    e.sourceId = sourceId;
    e.value = value;
    g.events.items.push_back(e);
}

const char* ReactionName(ReactionKind r) {
    switch (r) {
        case ReactionKind::Break:    return "Break";
        case ReactionKind::Bounce:   return "Bounce";
        case ReactionKind::Damage:   return "Damage";
        case ReactionKind::Activate: return "Activate";
        case ReactionKind::Rescue:   return "Rescue";
        case ReactionKind::Ride:     return "Ride";
        case ReactionKind::Pull:     return "Pull";
        default:                     return "None";
    }
}

// ══════════════════════════════════════════════ Reaction: Break（ターゲット）

static ReactionKind ReactTarget(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.targets.size()) return ReactionKind::None;
    Target& t = g.level.targets[idx];
    if (!t.alive) return ReactionKind::None;

    t.hp--;
    t.flash = 1.0f;

    // 浮いているターゲットは吹き飛ばさない（レーザーの的として位置を保つ）
    if (!t.floating && it.strength > 0.0f) {
        Vector3 dir = it.direction;
        dir.y = 0.0f;
        if (Vector3Length(dir) < 0.01f) dir = Vector3{0, 0, 1};
        dir = Vector3Normalize(dir);
        float lift = (it.type == InteractionType::Punch || it.type == InteractionType::AbilityHit)
                         ? 7.0f : 2.0f;
        t.vel = Vector3Add(Vector3Scale(dir, it.strength), Vector3{0, lift, 0});
    }

    if (t.hp <= 0) {
        t.alive = false;
        t.respawn = 2.5f;
        PushEvent(g, GameEventType::TargetBroken, t.pos, it.targetId, 1);
    } else {
        PushEvent(g, GameEventType::TargetHit, t.pos, it.targetId, t.hp);
    }
    return ReactionKind::Break;
}

// ══════════════════════════════════════════════ Reaction: Rescue（ボット / コイン）

static ReactionKind ReactBot(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.bots.size()) return ReactionKind::None;
    Bot& b = g.level.bots[idx];
    if (b.saved) return ReactionKind::None;
    b.saved = true;
    PushEvent(g, GameEventType::BotSaved, b.pos, it.targetId, 1);
    return ReactionKind::Rescue;
}

static ReactionKind ReactCoin(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.coins.size()) return ReactionKind::None;
    Coin& c = g.level.coins[idx];
    if (c.taken) return ReactionKind::None;
    c.taken = true;
    PushEvent(g, GameEventType::CoinTaken, c.pos, it.targetId, 1);
    return ReactionKind::Rescue;
}

// ══════════════════════════════════════════════ Reaction: Bounce（バネ）

static ReactionKind ReactSpring(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.springs.size()) return ReactionKind::None;
    Spring& s = g.level.springs[idx];
    if (s.cooldown > 0.0f) return ReactionKind::None;

    // 上から踏まれた時だけ跳ばす（横から当たっただけでは反応しない）
    if (it.type != InteractionType::Stomp && it.type != InteractionType::Touch) return ReactionKind::None;
    if (g.player.vel.y > 1.0f) return ReactionKind::None;

    float power = (s.power > 0.0f) ? s.power : g.p.springPower;
    g.player.vel.y   = power;
    g.player.grounded = false;
    g.player.jumpCutArmed = false;   // ボタンを押していなくても削られないように
    g.player.hovering = false;
    g.player.hoverFuel = g.p.hoverFuelMax;   // 跳んだ先でホバーが使える
    g.player.timeSinceJump = 0.0f;           // 押し続けてもすぐホバーに移らない
    g.player.squashY  = 1.35f;
    g.player.squashXZ = 0.78f;
    s.compress = 1.0f;
    s.cooldown = 0.15f;

    PushEvent(g, GameEventType::SpringBounced, s.pos, it.targetId, (int)power);
    return ReactionKind::Bounce;
}

// ══════════════════════════════════════════════ Reaction: Ride（ウォータースライダー）

static ReactionKind ReactSlide(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.slides.size()) return ReactionKind::None;
    Player& pl = g.player;
    if (pl.slide.active || pl.slide.cooldown > 0.0f) return ReactionKind::None;

    const WaterSlide& s = g.level.slides[idx];
    float dToLine = 0.0f;
    float d = SlideNearestDist(s, pl.pos, &dToLine);
    if (dToLine > s.radius) return ReactionKind::None;
    if (d > s.length - 1.5f) return ReactionKind::None;

    // 飛び込んだ勢いを引き継ぐ（速く入れば速く滑り出す）
    Vector3 tan   = SlideTangent(s, d);
    float   along = Vector3DotProduct(pl.vel, tan);

    pl.slide.index  = idx;
    pl.slide.dist   = d;
    pl.slide.side   = 0.0f;
    pl.slide.speed  = fmaxf(g.p.slideMinSpeed, along);
    pl.slide.active = true;

    PushEvent(g, GameEventType::SlideEntered, pl.pos, it.targetId, (int)pl.slide.speed);
    return ReactionKind::Ride;
}

// ══════════════════════════════════════════════ Reaction: Pull（ワイヤー）
//
// ここがこのプロトタイプで一番「設計が効いている」場所。
// プレイヤーは「ワイヤーを引っ掛けた」としか言っていない。
// 相手が固定物か、動かせる物かで、動くのが自分か相手かが変わる。
//   → 同じ操作なのに結果が違う ＝ 驚き
static ReactionKind ReactAnchor(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.anchors.size()) return ReactionKind::None;
    WireAnchor& a = g.level.anchors[idx];
    if (a.pulled) return ReactionKind::None;
    Player& pl = g.player;

    a.flash = 1.0f;

    if (a.kind == ANCHOR_FIXED) {
        // 固定されている → プレイヤーが振られる
        float len = Vector3Distance(a.pos, pl.pos);
        pl.ability.active = true;
        pl.ability.phase  = idx;
        pl.ability.timer  = 0.0f;
        pl.ability.value  = Clamp(len, g.p.wireMinLength, g.p.wireRange);
        pl.grounded = false;
        pl.hovering = false;
        pl.jumpCutArmed = false;

        // 止まったまま掴んでも振り出せるように、接線方向へ一押しする
        Vector3 toA = Vector3Normalize(Vector3Subtract(a.pos, pl.pos));
        Vector3 fwd = Vector3{sinf(g.rig.yaw), 0.0f, cosf(g.rig.yaw)};
        Vector3 tangent = Vector3Subtract(fwd, Vector3Scale(toA, Vector3DotProduct(fwd, toA)));
        if (Vector3Length(tangent) > 0.01f) {
            tangent = Vector3Normalize(tangent);
            float along = Vector3DotProduct(pl.vel, tangent);
            if (along < g.p.wireSwingBoost)
                pl.vel = Vector3Add(pl.vel, Vector3Scale(tangent, g.p.wireSwingBoost - along));
        }
        PushEvent(g, GameEventType::WireAttached, a.pos, it.targetId, (int)pl.ability.value);
        return ReactionKind::Pull;
    }

    // 動かせる物に付いている → 物の方が飛んでくる。
    // ただし真っ直ぐプレイヤーへ飛ばすと体を突き抜けて見えるので、少し横へ逸らして
    // 「肩をかすめて後ろへ飛んでいく」軌道にする。
    Vector3 dir = Vector3Subtract(pl.pos, a.pos);
    dir.y = 0.0f;
    if (Vector3Length(dir) < 0.01f) dir = Vector3{0, 0, -1};
    dir = Vector3Normalize(dir);
    const float kSideAngle = 0.55f;                       // 約 32 度
    Vector3 side = Vector3{dir.z, 0.0f, -dir.x};          // 水平の直交方向
    dir = Vector3Normalize(Vector3Add(Vector3Scale(dir, cosf(kSideAngle)),
                                      Vector3Scale(side, sinf(kSideAngle))));
    a.pulled = true;
    a.vel = Vector3Add(Vector3Scale(dir, g.p.wirePullSpeed), Vector3{0, 9.0f, 0});
    if (a.boxIndex >= 0 && a.boxIndex < (int)g.level.boxes.size()) {
        g.level.boxes[a.boxIndex].kind = BOX_MOVING;   // 動いているものとして描く
    }
    PushEvent(g, GameEventType::SurpriseRevealed, a.pos, it.targetId, 1);
    return ReactionKind::Pull;
}

// ══════════════════════════════════════════════ Reaction: Activate（ボタン）

static ReactionKind ReactButton(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.buttons.size()) return ReactionKind::None;
    Button& b = g.level.buttons[idx];

    // 叩く・踏む・体当たりで反応する。触っただけでは反応しない。
    if (it.type == InteractionType::Touch) return ReactionKind::None;

    bool wasClosed = (b.openTimer <= 0.0f);
    b.openTimer = g.p.buttonOpenTime;
    b.press = 1.0f;
    if (wasClosed) PushEvent(g, GameEventType::ButtonActivated, b.pos, it.targetId, 1);
    return ReactionKind::Activate;
}

// ══════════════════════════════════════════════ Reaction: Damage（敵）

static ReactionKind ReactEnemy(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.enemies.size()) return ReactionKind::None;
    Enemy& e = g.level.enemies[idx];
    if (!e.alive) return ReactionKind::None;

    // 踏む・叩く・ダッシュで当たる → 倒せる
    if (it.type == InteractionType::Stomp || it.type == InteractionType::Punch ||
        it.type == InteractionType::AbilityHit || it.type == InteractionType::Laser) {
        e.alive = false;
        e.respawn = 3.0f;
        e.flash = 1.0f;
        if (it.type == InteractionType::Stomp) {
            g.player.vel.y = g.p.stompBounce;   // 踏んだら跳ねる
            g.player.grounded = false;
            g.player.jumpCutArmed = false;
            g.player.timeSinceJump = 0.0f;
            g.player.squashY = 1.25f;
        }
        PushEvent(g, GameEventType::EnemyDefeated, e.pos, it.targetId, 1);
        return ReactionKind::Break;
    }

    // 横から触った → プレイヤーが弾かれる
    if (it.type == InteractionType::Touch) {
        if (g.player.invuln > 0.0f) return ReactionKind::None;
        Vector3 dir = Vector3Subtract(g.player.pos, e.pos);
        dir.y = 0.0f;
        if (Vector3Length(dir) < 0.01f) dir = Vector3{0, 0, -1};
        dir = Vector3Normalize(dir);
        g.player.vel = Vector3Add(Vector3Scale(dir, g.p.damageKnockback), Vector3{0, 6.0f, 0});
        g.player.grounded = false;
        g.player.jumpCutArmed = false;
        g.player.invuln = g.p.invulnTime;
        g.player.ability.active = false;
        PushEvent(g, GameEventType::PlayerDamaged, g.player.pos, it.targetId, 1);
        return ReactionKind::Damage;
    }
    return ReactionKind::None;
}

// ══════════════════════════════════════════════ Reaction: 能力アイテム / ゴール

static ReactionKind ReactPickup(Game& g, const Interaction& it) {
    int idx = IdIndex(it.targetId);
    if (idx < 0 || idx >= (int)g.level.pickups.size()) return ReactionKind::None;
    AbilityPickup& a = g.level.pickups[idx];
    if (a.taken) return ReactionKind::None;
    a.taken = true;
    GrantAbility(g, a.type);
    PushEvent(g, GameEventType::AbilityGained, a.pos, it.targetId, (int)a.type);
    return ReactionKind::Rescue;
}

static ReactionKind ReactGoal(Game& g, const Interaction& it) {
    if (g.level.cleared) return ReactionKind::None;
    g.level.cleared = true;
    PushEvent(g, GameEventType::StageCleared, g.level.goal, it.targetId, 1);
    return ReactionKind::Activate;
}

// ══════════════════════════════════════════════ 解決

void ResolveInteractions(Game& g) {
    // 解決中に新しい Interaction が積まれる可能性があるので、添字で回す
    for (size_t i = 0; i < g.interactions.items.size(); ++i) {
        const Interaction it = g.interactions.items[i];
        ReactionKind r = ReactionKind::None;
        switch (IdKind(it.targetId)) {
            case OBJ_TARGET: r = ReactTarget(g, it); break;
            case OBJ_BOT:    r = ReactBot(g, it);    break;
            case OBJ_COIN:   r = ReactCoin(g, it);   break;
            case OBJ_SPRING: r = ReactSpring(g, it); break;
            case OBJ_SLIDE:  r = ReactSlide(g, it);  break;
            case OBJ_ANCHOR: r = ReactAnchor(g, it); break;
            case OBJ_BUTTON: r = ReactButton(g, it); break;
            case OBJ_ENEMY:  r = ReactEnemy(g, it);  break;
            case OBJ_PICKUP: r = ReactPickup(g, it); break;
            case OBJ_GOAL:   r = ReactGoal(g, it);   break;
            default: break;
        }
        if (r != ReactionKind::None) g.debug.lastReaction = (int)r;
    }
    g.interactions.items.clear();
}

// ══════════════════════════════════════════════ Event の配送

static void ApplyEventToStats(Game& g, const GameEvent& e) {
    switch (e.type) {
        case GameEventType::CoinTaken:       g.stats.coinsTaken      += e.value; break;
        case GameEventType::BotSaved:        g.stats.botsSaved       += e.value; break;
        case GameEventType::TargetBroken:    g.stats.targetsBroken   += e.value; break;
        case GameEventType::EnemyDefeated:   g.stats.enemiesDefeated += e.value; break;
        case GameEventType::PlayerDamaged:   g.stats.damageTaken     += e.value; break;
        case GameEventType::SpringBounced:   g.stats.springBounces   += 1;       break;
        case GameEventType::SlideEntered:    g.stats.slideRides      += 1;       break;
        case GameEventType::WireAttached:    g.stats.wireGrabs       += 1;       break;
        case GameEventType::SurpriseRevealed:g.stats.surprises       += 1;       break;
        case GameEventType::ButtonActivated: g.stats.buttonHits      += 1;       break;
        default: break;
    }
}

void DispatchGameEvents(Game& g) {
    for (size_t i = 0; i < g.events.items.size(); ++i) {
        const GameEvent e = g.events.items[i];
        ApplyEventToStats(g, e);
        ApplyEventToWorldPhysics(g, e);   // 破片を出すのはここ（見せ方の前）
        ApplyEventToPresentation(g, e);
    }
    g.events.items.clear();
}
