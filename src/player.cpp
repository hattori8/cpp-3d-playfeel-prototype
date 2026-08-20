// player.cpp ─ 主人公。専用コードで磨く層。
//
// ECS 化しない。加速・減速・旋回・コヨーテタイム・可変ジャンプ・頂点重力・ホバー・
// 段差・接地吸着・攻撃・カメラ連携は互いに強く結びついているので、専用の関数として
// 順番を明示して書く。内部は段階ごとに分けてあり、大きくなった所から抜き出す。
//
//   移動        : 左スティック / WASD（カメラ相対）
//   ジャンプ    : × / Space（可変高度・コヨーテタイム・先行入力バッファ）
//   ホバー      : ジャンプ後もボタンを押し続ける（燃料制）
//   スピンパンチ: □ / J（地上）
//   レーザー    : 空中で □ / J を押し続ける（真下へ）
//   能力        : R2 / K（ability.cpp）
#include "game.h"
#include "util.h"
#include "rlgl.h"
#include <cmath>

static Vector3 PlayerForward(const Player& pl) {
    return Vector3{sinf(pl.yaw), 0.0f, cosf(pl.yaw)};
}

// ────────────────────────────────────────────── 衝突解決のための軸アクセス

static float& Comp(Vector3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
static float  CompC(const Vector3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

// 1 軸だけ動かして押し戻す。戻り値: 0=接触なし / +1=正方向で接触 / -1=負方向で接触
static int MoveAxisPlayer(Game& g, int axis, float d) {
    if (fabsf(d) < 1e-6f) return 0;
    Player& pl = g.player;
    Comp(pl.pos, axis) += d;

    int hit = 0;
    for (const Box& b : g.level.boxes) {
        if (b.kind == BOX_SCENERY || !b.solid) continue;
        if (!BoxOverlap(pl.pos, pl.half, b.c, b.h)) continue;
        if (d > 0.0f) {
            Comp(pl.pos, axis) = CompC(b.c, axis) - CompC(b.h, axis) - CompC(pl.half, axis) - 0.001f;
            hit = 1;
        } else {
            Comp(pl.pos, axis) = CompC(b.c, axis) + CompC(b.h, axis) + CompC(pl.half, axis) + 0.001f;
            hit = -1;
        }
    }
    return hit;
}

void RespawnPlayer(Game& g) {
    Player& pl = g.player;
    int idx = pl.checkpoint;
    if (idx < 0 || idx >= (int)g.level.checkpoints.size()) idx = 0;
    pl.pos = g.level.checkpoints[idx];
    pl.vel = Vector3{0, 0, 0};
    pl.hovering = false;
    pl.hoverFuel = g.p.hoverFuelMax;
    pl.ability.active = false;
    pl.ability.value = (pl.ability.type == AbilityType::Dash) ? g.p.dashFuelMax : 0.0f;
    pl.slide = SlideState{};
    pl.respawnFlash = 0.6f;
    SpawnBurst(g, pl.pos, 16, SKYBLUE, 5.0f, 0.2f);
}

// ══════════════════════════════════════════════ 1. 動く床に運ばれる

static void ApplyMovingPlatform(Game& g) {
    Player& pl = g.player;
    if (pl.ridingPlatform < 0 || pl.ridingPlatform >= (int)g.level.platforms.size()) return;
    pl.pos = Vector3Add(pl.pos, g.level.platforms[pl.ridingPlatform].delta);
}

// ══════════════════════════════════════════════ 1.5 ウォータースライダー
//
// 乗っている間は通常の移動・ジャンプ・衝突処理を全部飛ばし、中心線に沿って
// 位置を決める（レール方式）。管の当たり判定は作らない。
// true を返したフレームは「運ばれている」状態。

static void DetachSlide(Game& g, Vector3 vel) {
    Player& pl = g.player;
    pl.slide.active   = false;
    pl.slide.cooldown = 0.5f;      // 出口で即座に再乗車しないように
    pl.vel = vel;
    pl.grounded = false;
    pl.jumpCutArmed = false;
    pl.timeSinceJump = 0.0f;       // 押しっぱなしでも即ホバーにならない
    pl.hoverFuel = g.p.hoverFuelMax;
    PushEvent(g, GameEventType::SlideExited, pl.pos, PLAYER_ID,
              (int)sqrtf(vel.x * vel.x + vel.z * vel.z));
}

static bool UpdatePlayerSlide(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;
    SlideState&   sl = pl.slide;

    sl.cooldown = fmaxf(0.0f, sl.cooldown - dt);
    if (!sl.active) return false;
    if (sl.index < 0 || sl.index >= (int)g.level.slides.size()) { sl.active = false; return false; }

    const WaterSlide& s = g.level.slides[sl.index];
    Vector3 tan = SlideTangent(s, sl.dist);

    // 勾配で加減速し、そのうえで「水流」が基準速度へ寄せる。
    // 重力だけに任せると、緩い勾配の水路で歩くより遅くなって乗る意味がなくなる
    // （実際に slide keeps momentum のテストが落ちて気づいた）。水で押される
    // 乗り物として基準速度を持たせ、勾配はその上の抑揚として効かせる。
    sl.speed += (-tan.y) * p.slideGravity * dt;
    sl.speed = MoveTowardsF(sl.speed, p.slideFlowSpeed, p.slideFlowAccel * dt);
    sl.speed = Clamp(sl.speed, p.slideMinSpeed, p.slideMaxSpeed);
    sl.dist += sl.speed * dt;

    // 左右に寄る。画面右 = cross(進行方向, 上) なので入力の符号はそのまま使える。
    Vector3 right = Vector3Normalize(Vector3CrossProduct(tan, Vector3{0, 1, 0}));
    float maxSide = s.radius * p.slideSideMax;
    sl.side = MoveTowardsF(sl.side, g.in.move.x * maxSide, p.slideSteer * dt);
    sl.side = Clamp(sl.side, -maxSide, maxSide);
    float k    = (maxSide > 0.001f) ? (sl.side / maxSide) : 0.0f;
    float bank = k * k * s.radius * p.slideBank;      // 外に寄るほど壁を登る

    Vector3 c = SlidePoint(s, sl.dist);
    pl.pos = Vector3Add(Vector3Add(c, Vector3Scale(right, sl.side)),
                        Vector3{0, -s.radius * 0.55f + pl.half.y + 0.02f + bank, 0});
    pl.vel = Vector3Scale(tan, sl.speed);
    pl.yaw = atan2f(tan.x, tan.z);

    pl.grounded = false;
    pl.hovering = false;
    pl.laserOn = false;
    pl.ability.active = false;
    pl.hoverFuel = p.hoverFuelMax;
    pl.jumpCutArmed = false;
    pl.timeSinceJump = 0.0f;
    // 速度に応じて縦に伸ばす（速さを見た目に出す）
    float stretch = Sat((sl.speed - p.slideMinSpeed) / fmaxf(1.0f, p.slideMaxSpeed - p.slideMinSpeed));
    pl.squashXZ = 1.0f - 0.14f * stretch;
    pl.squashY  = 1.0f + 0.10f * stretch;

    if (GetRandomValue(0, 2) == 0) {
        SpawnBurst(g, Vector3{pl.pos.x, pl.pos.y - pl.half.y, pl.pos.z}, 1,
                   Color{200, 245, 255, 255}, 3.0f, 0.12f);
    }

    // 途中で飛び降りる
    if (g.in.jumpPressed) {
        DetachSlide(g, Vector3Add(Vector3Scale(tan, sl.speed), Vector3{0, p.slideJumpOff, 0}));
        pl.squashY = 1.25f;
        return true;
    }
    // 終端で射出
    if (sl.dist >= s.length) {
        float boost = (s.exitBoost > 0.0f) ? s.exitBoost : p.slideExitBoost;
        DetachSlide(g, Vector3Scale(tan, sl.speed * boost));
    }
    return true;
}

// ══════════════════════════════════════════════ 2. 水平移動

static void UpdatePlayerMovement(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;

    // 右手系 + Y 上向きなので、視線が +Z を向いている時に画面右へ出るのはワールド -X。
    // 画面右 = cross(前, 上) = (-cos(yaw), 0, sin(yaw))。符号を間違えると左右が逆になる。
    Vector3 fwd   = Vector3{ sinf(g.rig.yaw), 0.0f, cosf(g.rig.yaw)};
    Vector3 right = Vector3{-cosf(g.rig.yaw), 0.0f, sinf(g.rig.yaw)};

    float mag = sqrtf(g.in.move.x * g.in.move.x + g.in.move.y * g.in.move.y);
    if (mag > 1.0f) mag = 1.0f;

    Vector3 wish = Vector3{0, 0, 0};
    if (mag > 0.001f) {
        wish = Vector3Add(Vector3Scale(fwd, g.in.move.y), Vector3Scale(right, g.in.move.x));
        if (Vector3Length(wish) > 0.001f) wish = Vector3Normalize(wish);
    }
    pl.wishDir = wish;
    pl.wishMag = mag;

    Vector3 targetVel = Vector3Scale(wish, p.moveSpeed * mag);

    float accel;
    if (pl.grounded) accel = (mag > 0.001f) ? p.moveAccel : p.moveDecel;
    else             accel = p.airAccel * (pl.hovering ? p.hoverControlMul : 1.0f);

    pl.vel.x = MoveTowardsF(pl.vel.x, targetVel.x, accel * dt);
    pl.vel.z = MoveTowardsF(pl.vel.z, targetVel.z, accel * dt);
}

// ══════════════════════════════════════════════ 3. ジャンプ

static void UpdatePlayerJump(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;

    pl.timeSinceJump += dt;
    if (pl.grounded) {
        pl.coyote    = p.coyoteTime;
        pl.hoverFuel = p.hoverFuelMax;
    } else {
        pl.coyote -= dt;
    }

    if (g.in.jumpPressed) pl.jumpBuffer = p.jumpBufferTime;
    pl.jumpBuffer -= dt;

    if (pl.jumpBuffer > 0.0f && pl.coyote > 0.0f) {
        pl.vel.y         = p.jumpSpeed;
        pl.jumpBuffer    = 0.0f;
        pl.coyote        = 0.0f;
        pl.grounded      = false;
        pl.timeSinceJump = 0.0f;
        pl.jumpCutArmed  = true;
        pl.squashY  = 1.30f;
        pl.squashXZ = 0.80f;
        // 跳んだことは squash とカメラで既に伝わっている。土煙は接地の名残くらいでいい。
        SpawnBurst(g, Vector3{pl.pos.x, pl.pos.y - pl.half.y, pl.pos.z}, (int)p.jumpPuffCount,
                   Color{255, 255, 220, 235}, 2.6f, 0.11f);
    }

    // ボタンを離したら上昇を打ち切る＝押し続けた分だけ高く跳ぶ。
    // 自分のジャンプ中だけに限る（バネや踏みつけの跳ね返りを削らないため）。
    if (pl.jumpCutArmed && !g.in.jumpHeld && pl.vel.y > p.jumpCutSpeed) {
        pl.vel.y = p.jumpCutSpeed;
        pl.jumpCutArmed = false;
    }
    if (pl.grounded) pl.jumpCutArmed = false;
}

// ══════════════════════════════════════════════ 4. ホバーと重力
//
// 重力はホバーしていない時だけ掛ける。接地中も掛けるのは感触ではなく判定の都合で、
// 接地中に vel.y = 0 にすると Y 方向の移動量が 0 になり、下向きの接触判定が取れず
// 段差の自動乗り越えが効かなくなる。
static void UpdatePlayerHover(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;

    bool canHover = !pl.grounded && g.in.jumpHeld && pl.timeSinceJump > p.hoverDelay &&
                    pl.hoverFuel > 0.0f && !pl.ability.active;

    if (canHover) {
        if (!pl.hovering) {
            pl.hovering = true;
            if (pl.vel.y < p.hoverStartBoost) pl.vel.y = p.hoverStartBoost;
            SpawnBurst(g, Vector3{pl.pos.x, pl.pos.y - pl.half.y, pl.pos.z},
                       (int)p.hoverStartPuff, Color{255, 205, 135, 220}, 3.0f, 0.11f);
        }
        // 上昇中は「ゆるく打ち消す」だけ、落ち始めてからは強く支える。
        // この非対称が浮遊感になる（対称にすると跳ねが死ぬ）。
        float rate = (pl.vel.y > p.hoverTargetVy) ? p.hoverBrake : p.hoverAccel;
        pl.vel.y = MoveTowardsF(pl.vel.y, p.hoverTargetVy, rate * dt);
        pl.hoverFuel = fmaxf(0.0f, pl.hoverFuel - dt);
        pl.jetFlicker += dt * 30.0f;
        // フレーム毎の抽選ではなく「毎秒 N 個」で出す。fps が変わっても見え方が変わらない。
        pl.jetPuffAccum += p.jetPuffRate * dt;
        while (pl.jetPuffAccum >= 1.0f) {
            pl.jetPuffAccum -= 1.0f;
            SpawnJetPuff(g, Vector3{pl.pos.x, pl.pos.y - pl.half.y, pl.pos.z});
        }
        return;
    }

    pl.hovering = false;

    float grav = p.gravity;
    if (!pl.grounded && fabsf(pl.vel.y) < p.apexThreshold) grav *= p.apexGravityScale;
    pl.vel.y -= grav * dt;
    if (pl.vel.y < -p.maxFall) pl.vel.y = -p.maxFall;
}

// ══════════════════════════════════════════════ 5. 攻撃
//
// 対象の種類ごとの処理はここに書かない。Interaction を積むだけ。

static void PushAttackInteractions(Game& g, InteractionType type, Vector3 center, Vector3 dir,
                                   float radius, float strength) {
    for (int i = 0; i < (int)g.level.targets.size(); ++i) {
        const Target& t = g.level.targets[i];
        if (!t.alive) continue;
        if (Vector3Distance(center, t.pos) > radius + t.half.x) continue;
        PushInteraction(g, Interaction{type, PLAYER_ID, MakeId(OBJ_TARGET, i), t.pos, dir, strength});
    }
    for (int i = 0; i < (int)g.level.enemies.size(); ++i) {
        const Enemy& e = g.level.enemies[i];
        if (!e.alive) continue;
        if (Vector3Distance(center, e.pos) > radius + e.half.x) continue;
        PushInteraction(g, Interaction{type, PLAYER_ID, MakeId(OBJ_ENEMY, i), e.pos, dir, strength});
    }
    for (int i = 0; i < (int)g.level.crates.size(); ++i) {
        const Crate& c = g.level.crates[i];
        if (c.boxIndex < 0 || c.boxIndex >= (int)g.level.boxes.size()) continue;
        Vector3 cp = g.level.boxes[c.boxIndex].c;
        if (Vector3Distance(center, cp) > radius + c.size) continue;
        PushInteraction(g, Interaction{type, PLAYER_ID, MakeId(OBJ_CRATE, i), cp, dir, strength});
    }
    for (int i = 0; i < (int)g.level.buttons.size(); ++i) {
        const Button& b = g.level.buttons[i];
        if (Vector3Distance(center, b.pos) > radius + b.half.x) continue;
        PushInteraction(g, Interaction{type, PLAYER_ID, MakeId(OBJ_BUTTON, i), b.pos, dir, strength});
    }
}

static void UpdatePlayerAttack(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;

    pl.punchCooldown = fmaxf(0.0f, pl.punchCooldown - dt);
    pl.punchTimer    = fmaxf(0.0f, pl.punchTimer - dt);

    // ── 地上：スピンパンチ
    if (pl.grounded) {
        pl.laserOn = false;
        if (g.in.punchPressed && pl.punchCooldown <= 0.0f) {
            pl.punchTimer    = p.punchDuration;
            pl.punchCooldown = p.punchCooldown;
            pl.punchHitDone  = false;
            pl.vel = Vector3Add(pl.vel, Vector3Scale(PlayerForward(pl), p.punchLunge));
        }
        if (pl.punchTimer > 0.0f && !pl.punchHitDone) {
            Vector3 fwd    = PlayerForward(pl);
            Vector3 center = Vector3Add(pl.pos, Vector3Scale(fwd, p.punchRange));
            PushAttackInteractions(g, InteractionType::Punch, center, fwd, p.punchRadius,
                                   p.punchImpulse);
            pl.punchHitDone = true;
        }
        return;
    }

    // ── 空中：真下へレーザー
    pl.laserOn = g.in.punchHeld && !pl.ability.active;
    if (!pl.laserOn) { pl.laserTimer = 0.0f; return; }

    int   hitTarget = -1;
    float y = RaycastDown(g.level, pl.pos, p.laserRange, &hitTarget);
    pl.laserEnd = Vector3{pl.pos.x, y, pl.pos.z};

    pl.laserTimer -= dt;
    if (pl.laserTimer <= 0.0f) {
        pl.laserTimer = p.laserInterval;
        SpawnSparks(g, pl.laserEnd, Vector3{0, 1, 0}, 5, Color{190, 250, 255, 255}, 6.0f, 0.09f);
        SpawnEffect(g, EffectKind::Ring, Vector3{pl.laserEnd.x, pl.laserEnd.y + 0.03f, pl.laserEnd.z},
                    Vector3{0, 1, 0}, 0.22f, 0.25f, 1.3f, Color{170, 245, 255, 210});
        if (hitTarget >= 0) {
            PushInteraction(g, Interaction{InteractionType::Laser, PLAYER_ID,
                                           MakeId(OBJ_TARGET, hitTarget),
                                           g.level.targets[hitTarget].pos,
                                           Vector3{0, -1, 0}, 2.0f});
        }
    }
}

// ══════════════════════════════════════════════ 6. 衝突解決

static void ResolvePlayerCollision(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;

    pl.grounded = false;

    int hy = MoveAxisPlayer(g, 1, pl.vel.y * dt);
    if (hy < 0) {
        float impact = fabsf(pl.vel.y);
        pl.vel.y = 0.0f;
        pl.grounded = true;
        if (!pl.wasGrounded) {
            float k = Sat(impact / 20.0f);
            if (impact > 4.0f) {
                pl.squashY  = 1.0f - 0.45f * k;
                pl.squashXZ = 1.0f + 0.40f * k;
            }
            PushEvent(g, GameEventType::PlayerLanded,
                      Vector3{pl.pos.x, pl.pos.y - pl.half.y, pl.pos.z}, PLAYER_ID,
                      (int)(k * 100.0f));
        }
    } else if (hy > 0) {
        pl.vel.y = 0.0f;
    }

    Vector3 beforeXZ = pl.pos;
    int hx = MoveAxisPlayer(g, 0, pl.vel.x * dt);
    int hz = MoveAxisPlayer(g, 2, pl.vel.z * dt);

    // 段差の自動乗り越え
    if ((hx != 0 || hz != 0) && (pl.grounded || pl.wasGrounded) && p.stepHeight > 0.0f) {
        Vector3 blockedPos = pl.pos;
        pl.pos = beforeXZ;
        pl.pos.y += p.stepHeight;
        if (!AnyOverlapSolid(g.level, pl.pos, pl.half)) {
            MoveAxisPlayer(g, 0, pl.vel.x * dt);
            MoveAxisPlayer(g, 2, pl.vel.z * dt);
            MoveAxisPlayer(g, 1, -p.stepHeight);
            if (AnyOverlapSolid(g.level, pl.pos, pl.half)) pl.pos = blockedPos;
        } else {
            pl.pos = blockedPos;
        }
    }

    // 段差を降りる時に浮かないよう、少しだけ地面に吸着させる
    if (!pl.grounded && pl.wasGrounded && pl.vel.y <= 0.1f && !pl.hovering && !pl.ability.active) {
        Vector3 save = pl.pos;
        if (MoveAxisPlayer(g, 1, -(p.stepHeight + 0.05f)) < 0) {
            pl.grounded = true;
            pl.vel.y = 0.0f;
        } else {
            pl.pos = save;
        }
    }

    // 乗っている動く床を特定
    pl.ridingPlatform = -1;
    if (pl.grounded) {
        Vector3 probe = pl.pos;
        probe.y -= 0.08f;
        for (int i = 0; i < (int)g.level.platforms.size(); ++i) {
            const Box& b = g.level.boxes[g.level.platforms[i].boxIndex];
            if (BoxOverlap(probe, pl.half, b.c, b.h)) { pl.ridingPlatform = i; break; }
        }
    }
}

// ══════════════════════════════════════════════ 7. 接触の検出
//
// 「触った」という事実だけを Interaction として積む。相手が何で、どう反応するかは
// interaction.cpp が決める。上から踏んだ場合だけ Stomp を送る。
static void DetectPlayerContacts(Game& g) {
    Player& pl = g.player;
    const Params& p = g.p;
    Vector3 down = Vector3{0, -1, 0};
    bool falling = pl.vel.y <= 0.5f;

    for (int i = 0; i < (int)g.level.coins.size(); ++i) {
        const Coin& c = g.level.coins[i];
        if (c.taken) continue;
        if (Vector3Distance(pl.pos, c.pos) > p.coinRadius) continue;
        PushInteraction(g, Interaction{InteractionType::Touch, PLAYER_ID, MakeId(OBJ_COIN, i),
                                       c.pos, down, 0.0f});
    }
    for (int i = 0; i < (int)g.level.bots.size(); ++i) {
        const Bot& b = g.level.bots[i];
        if (b.saved) continue;
        if (Vector3Distance(pl.pos, b.pos) > p.botRadius) continue;
        PushInteraction(g, Interaction{InteractionType::Touch, PLAYER_ID, MakeId(OBJ_BOT, i),
                                       b.pos, down, 0.0f});
    }
    for (int i = 0; i < (int)g.level.pickups.size(); ++i) {
        const AbilityPickup& a = g.level.pickups[i];
        if (a.taken) continue;
        if (Vector3Distance(pl.pos, a.pos) > p.pickupRadius) continue;
        PushInteraction(g, Interaction{InteractionType::Touch, PLAYER_ID, MakeId(OBJ_PICKUP, i),
                                       a.pos, down, 0.0f});
    }
    for (int i = 0; i < (int)g.level.springs.size(); ++i) {
        const Spring& s = g.level.springs[i];
        Vector3 h = Vector3{s.half.x, s.half.y + 0.12f, s.half.z};
        if (!BoxOverlap(pl.pos, pl.half, s.pos, h)) continue;
        PushInteraction(g, Interaction{falling ? InteractionType::Stomp : InteractionType::Touch,
                                       PLAYER_ID, MakeId(OBJ_SPRING, i), s.pos, down, 0.0f});
    }
    for (int i = 0; i < (int)g.level.buttons.size(); ++i) {
        const Button& b = g.level.buttons[i];
        Vector3 h = Vector3{b.half.x, b.half.y + 0.12f, b.half.z};
        if (!BoxOverlap(pl.pos, pl.half, b.pos, h)) continue;
        bool above = pl.pos.y - pl.half.y > b.pos.y + b.half.y - 0.25f;
        PushInteraction(g, Interaction{(falling && above) ? InteractionType::Stomp
                                                          : InteractionType::Touch,
                                       PLAYER_ID, MakeId(OBJ_BUTTON, i), b.pos, down, 0.0f});
    }
    for (int i = 0; i < (int)g.level.enemies.size(); ++i) {
        const Enemy& e = g.level.enemies[i];
        if (!e.alive) continue;
        if (!BoxOverlap(pl.pos, pl.half, e.pos, e.half)) continue;
        bool above = pl.pos.y > e.pos.y + e.half.y * 0.4f;
        PushInteraction(g, Interaction{(falling && above) ? InteractionType::Stomp
                                                          : InteractionType::Touch,
                                       PLAYER_ID, MakeId(OBJ_ENEMY, i), e.pos, down, 0.0f});
    }
    // ウォータースライダーの入口（乗っていない時だけ見る）
    if (!pl.slide.active && pl.slide.cooldown <= 0.0f) {
        for (int i = 0; i < (int)g.level.slides.size(); ++i) {
            const WaterSlide& s = g.level.slides[i];
            float dToLine = 0.0f;
            float d = SlideNearestDist(s, pl.pos, &dToLine);
            if (dToLine > s.radius) continue;
            if (d > s.length - 1.5f) continue;        // 出口側からは乗らない
            PushInteraction(g, Interaction{InteractionType::Touch, PLAYER_ID,
                                           MakeId(OBJ_SLIDE, i), pl.pos, down, 0.0f});
        }
    }

    if (!g.level.cleared && Vector3Distance(pl.pos, g.level.goal) < 2.0f) {
        PushInteraction(g, Interaction{InteractionType::Touch, PLAYER_ID, MakeId(OBJ_GOAL, 0),
                                       g.level.goal, down, 0.0f});
    }
}

// ══════════════════════════════════════════════ 8. 向き

static void UpdatePlayerFacing(Game& g, float dt) {
    Player&       pl = g.player;
    const Params& p  = g.p;
    if (pl.wishMag <= 0.15f) return;

    // ダッシュ中は旋回を鈍くする（能力ごとの差をここで吸収する）
    float turn = p.turnSpeed * (pl.ability.active ? p.dashTurnMul : 1.0f);
    float want = atan2f(pl.wishDir.x, pl.wishDir.z);
    pl.yaw = MoveTowardsAngle(pl.yaw, want, turn * dt);
}

// ══════════════════════════════════════════════ まとめ

void UpdatePlayer(Game& g, float dt) {
    Player& pl = g.player;

    pl.wasGrounded  = pl.grounded;
    pl.respawnFlash = fmaxf(0.0f, pl.respawnFlash - dt);
    pl.invuln       = fmaxf(0.0f, pl.invuln - dt);

    ApplyMovingPlatform(g);

    // スライダーに乗っている間は移動・衝突を丸ごと飛ばす（位置は中心線が決める）
    bool riding = UpdatePlayerSlide(g, dt);

    if (!riding) {
        UpdatePlayerMovement(g, dt);
        UpdatePlayerJump(g, dt);
        UpdatePlayerHover(g, dt);

        UpdatePlayerAbility(g, dt);
        UpdatePlayerAttack(g, dt);

        ResolvePlayerCollision(g, dt);
    }

    DetectPlayerContacts(g);          // コインなどは滑りながらでも拾える

    if (!riding) {
        UpdatePlayerFacing(g, dt);
        // スクワッシュ＆ストレッチを素に戻す（滑走中は速度で伸ばしているので触らない）
        pl.squashY  = ExpSmooth(pl.squashY, 1.0f, 12.0f, dt);
        pl.squashXZ = ExpSmooth(pl.squashXZ, 1.0f, 12.0f, dt);
    }

    // チェックポイント更新 / 落下復帰
    if (pl.grounded) {
        for (int i = 0; i < (int)g.level.checkpoints.size(); ++i) {
            Vector3 cp = g.level.checkpoints[i];
            float dxz = sqrtf((pl.pos.x - cp.x) * (pl.pos.x - cp.x) +
                              (pl.pos.z - cp.z) * (pl.pos.z - cp.z));
            if (pl.pos.z >= cp.z - 3.0f && dxz < 16.0f && i > pl.checkpoint) pl.checkpoint = i;
        }
    }
    if (pl.pos.y < -18.0f) RespawnPlayer(g);
}

// ══════════════════════════════════════════════ 描画

void DrawPlayer(Game& g) {
    Player& pl = g.player;

    // ── レーザー：芯・グロー・流れる輪・着弾点の4層で「出ている」ことを強く見せる
    if (pl.laserOn) {
        float w    = g.p.laserWidth * (0.85f + 0.15f * sinf(g.time * 70.0f));
        Vector3 a  = Vector3{pl.pos.x, pl.pos.y - pl.half.y * 0.2f, pl.pos.z};
        Vector3 b  = pl.laserEnd;
        float  len = fmaxf(0.01f, a.y - b.y);

        // 内側から外へ描く。太い方を先に描くと深度が書かれて、中の芯が消えてしまう。
        DrawCylinderEx(a, b, w * 1.0f, w * 0.8f, 12, Color{255, 255, 255, 255});  // 白い芯
        DrawCylinderEx(a, b, w * 2.0f, w * 1.4f, 12, Color{ 70, 215, 255, 190});  // 中間
        DrawCylinderEx(a, b, w * 3.4f, w * 2.4f, 12, Color{ 90, 225, 255,  70});  // 外側のグロー

        // ビームを流れ落ちる輪（速度と方向が伝わる）
        for (int i = 0; i < 6; ++i) {
            float t = fmodf(g.time * 3.6f + (float)i * 0.166f, 1.0f);
            float y = a.y - len * t;
            float rr = w * (2.8f + 3.0f * t);
            unsigned char al = (unsigned char)(230 * (1.0f - t));
            DrawCircle3D(Vector3{a.x, y, a.z}, rr,        Vector3{1, 0, 0}, 90.0f,
                         Color{225, 252, 255, al});
            DrawCircle3D(Vector3{a.x, y, a.z}, rr * 0.9f, Vector3{1, 0, 0}, 90.0f,
                         Color{225, 252, 255, al});
        }
        // 銃口のきらめき
        DrawSphere(a, w * 2.0f, Color{255, 255, 255, 210});
        // 着弾点：脈打つ二重の輪＋光の柱
        float pulse = 0.5f + 0.5f * sinf(g.time * 26.0f);
        DrawCircle3D(Vector3{b.x, b.y + 0.03f, b.z}, 0.42f + 0.16f * pulse, Vector3{1, 0, 0}, 90.0f,
                     Color{180, 250, 255, 235});
        DrawCircle3D(Vector3{b.x, b.y + 0.03f, b.z}, 0.62f + 0.20f * pulse, Vector3{1, 0, 0}, 90.0f,
                     Color{180, 250, 255, 120});
        DrawCylinderEx(Vector3{b.x, b.y, b.z}, Vector3{b.x, b.y + 0.5f, b.z}, 0.30f, 0.02f, 8,
                       Color{190, 250, 255, 90});
    }

    // 影（着地点の予測にもなるので必ず出す）
    float gy = RaycastDown(g.level, pl.pos, 40.0f, nullptr);
    float h  = pl.pos.y - gy;
    if (h < 16.0f) {
        float k = Sat(1.0f - h / 16.0f);
        float r = 0.44f * (0.55f + 0.45f * k);
        DrawCylinderEx(Vector3{pl.pos.x, gy + 0.015f, pl.pos.z},
                       Vector3{pl.pos.x, gy + 0.035f, pl.pos.z}, r, r, 16,
                       Color{20, 24, 34, (unsigned char)(120 * k)});
    }

    bool blink = (pl.respawnFlash > 0.0f || pl.invuln > 0.0f) &&
                 fmodf(g.time * 20.0f, 2.0f) < 1.0f;
    Color body = blink ? Color{255, 150, 150, 255} : Color{240, 244, 250, 255};

    rlPushMatrix();
    rlTranslatef(pl.pos.x, pl.pos.y, pl.pos.z);
    rlRotatef(pl.yaw * RAD2DEG, 0, 1, 0);
    rlScalef(pl.squashXZ, pl.squashY, pl.squashXZ);

    DrawSphere(Vector3{0, 0.02f, 0}, 0.36f, body);                                  // 胴
    DrawCube(Vector3{0, 0.06f, 0.30f}, 0.42f, 0.26f, 0.10f, Color{35, 40, 55, 255}); // 顔の面
    DrawSphere(Vector3{-0.10f, 0.07f, 0.35f}, 0.055f, Color{130, 235, 255, 255});    // 目
    DrawSphere(Vector3{ 0.10f, 0.07f, 0.35f}, 0.055f, Color{130, 235, 255, 255});
    DrawCube(Vector3{-0.16f, -0.38f, 0}, 0.15f, 0.24f, 0.22f, Color{200, 206, 216, 255}); // 脚
    DrawCube(Vector3{ 0.16f, -0.38f, 0}, 0.15f, 0.24f, 0.22f, Color{200, 206, 216, 255});

    // 背中側のバックパック（後ろ姿でも向きが読めるように色を変える）
    Color pack = (pl.ability.type == AbilityType::None) ? Color{90, 150, 215, 255}
                                                       : Color{240, 190, 80, 255};
    DrawCube(Vector3{0, 0.02f, -0.28f}, 0.34f, 0.30f, 0.16f, pack);
    DrawCubeWires(Vector3{0, 0.02f, -0.28f}, 0.34f, 0.30f, 0.16f, Color{40, 70, 120, 255});

    float armSwing = pl.punchTimer > 0.0f ? 0.34f : 0.0f;
    DrawCube(Vector3{-0.34f, 0.02f, armSwing}, 0.14f, 0.14f, 0.24f, Color{215, 220, 230, 255});
    DrawCube(Vector3{ 0.34f, 0.02f, armSwing}, 0.14f, 0.14f, 0.24f, Color{215, 220, 230, 255});

    if (pl.hovering && g.p.jetFlameLen > 0.001f) {
        // 炎は「浮いている」ことの記号。長さも太さも濃さも params から引く。
        float f = g.p.jetFlameLen * (1.0f + 0.22f * sinf(pl.jetFlicker));
        float w = g.p.jetFlameWidth;
        Color c = Color{255, 195, 95, (unsigned char)Clamp(g.p.jetFlameAlpha, 0.0f, 255.0f)};
        DrawCylinderEx(Vector3{-0.16f, -0.50f, 0}, Vector3{-0.16f, -0.50f - f, 0}, w, 0.015f, 6, c);
        DrawCylinderEx(Vector3{ 0.16f, -0.50f, 0}, Vector3{ 0.16f, -0.50f - f, 0}, w, 0.015f, 6, c);
    }
    rlPopMatrix();

    // ── スピンパンチ：振り抜ける弧を描く
    //
    // 単なる円だと「何かが光った」までしか伝わらない。先端が太く、後ろに向かって
    // 細く薄くなる弧が回ることで「振った」「どこに当たる」が読める。
    if (pl.punchTimer > 0.0f) {
        const Params& p = g.p;
        float t     = 1.0f - pl.punchTimer / fmaxf(0.0001f, p.punchDuration);   // 0→1
        float ease  = 1.0f - (1.0f - t) * (1.0f - t);
        float sweep = p.punchArcSweep * DEG2RAD;
        float head  = pl.yaw - sweep * 0.5f + sweep * ease;    // 振りの先端
        float tail  = head - sweep * 0.45f;
        float rad   = p.punchArcWidth;
        float fade  = 1.0f - t * t;

        const int N = 16;
        for (int k = 0; k < N; ++k) {
            float a0 = tail + (head - tail) * ((float)k / N);
            float a1 = tail + (head - tail) * ((float)(k + 1) / N);
            float k0 = (float)(k + 1) / N;                     // 先端ほど 1 に近い
            float wgt = 0.05f + 0.20f * k0;
            Vector3 p0 = Vector3Add(pl.pos, Vector3{sinf(a0) * rad, 0.04f, cosf(a0) * rad});
            Vector3 p1 = Vector3Add(pl.pos, Vector3{sinf(a1) * rad, 0.04f, cosf(a1) * rad});
            unsigned char al = (unsigned char)(235.0f * fade * k0);
            DrawCylinderEx(p0, p1, wgt, wgt, 5, Color{255, 244, 190, al});
            // 内側にもう一本、細く白い線を重ねて芯を出す
            Vector3 q0 = Vector3Add(pl.pos, Vector3{sinf(a0) * rad * 0.78f, 0.06f, cosf(a0) * rad * 0.78f});
            Vector3 q1 = Vector3Add(pl.pos, Vector3{sinf(a1) * rad * 0.78f, 0.06f, cosf(a1) * rad * 0.78f});
            DrawCylinderEx(q0, q1, wgt * 0.45f, wgt * 0.45f, 4,
                           Color{255, 255, 255, (unsigned char)(210.0f * fade * k0)});
        }
        // 拳の先端の光
        Vector3 tip = Vector3Add(pl.pos, Vector3{sinf(head) * rad, 0.05f, cosf(head) * rad});
        DrawSphere(tip, 0.20f * fade + 0.06f, Color{255, 250, 210, (unsigned char)(230 * fade)});
        // 足元に広がる薄い輪（判定範囲の目安）
        DrawCircle3D(Vector3{pl.pos.x, pl.pos.y - pl.half.y + 0.03f, pl.pos.z},
                     p.punchRadius * (0.5f + 0.8f * ease), Vector3{1, 0, 0}, 90.0f,
                     Color{255, 235, 150, (unsigned char)(120 * fade)});
    }
}
