// presentation.cpp ─ 見せ方（パーティクル / カメラ揺れ / 将来の音・ハプティクス）
//
// ここが Gameplay から呼ばれることはない。Gameplay は Event を積むだけで、
// 「壊れた時にどう感じさせるか」はこのファイルだけで足したり削ったりできる。
#include "game.h"
#include "util.h"
#include <cstdio>
#include <cstring>

// ────────────────────────────────────────────── パーティクル

void SpawnBurst(Game& g, Vector3 pos, int n, Color col, float power, float size) {
    for (int i = 0; i < n; ++i) {
        Particle pt{};
        pt.pos = pos;
        pt.vel = Vector3{RandF(-1, 1), RandF(-0.2f, 1.2f), RandF(-1, 1)};
        pt.vel = Vector3Scale(Vector3Normalize(pt.vel), power * RandF(0.4f, 1.0f));
        pt.maxLife = pt.life = RandF(0.25f, 0.6f);
        pt.size = size * RandF(0.6f, 1.2f);
        pt.col  = col;
        pt.gravity = true;
        g.particles.push_back(pt);
    }
}

// 方向を持った火花。当たった向きが分かるので、当たり判定の位置が伝わりやすい。
void SpawnSparks(Game& g, Vector3 pos, Vector3 dir, int n, Color col, float power, float size) {
    Vector3 d = (Vector3Length(dir) > 0.01f) ? Vector3Normalize(dir) : Vector3{0, 1, 0};
    for (int i = 0; i < n; ++i) {
        Particle pt{};
        pt.pos = pos;
        Vector3 jitter = Vector3{RandF(-1, 1), RandF(-1, 1), RandF(-1, 1)};
        Vector3 v = Vector3Add(Vector3Scale(d, 1.5f), jitter);
        pt.vel = Vector3Scale(Vector3Normalize(v), power * RandF(0.5f, 1.2f));
        pt.maxLife = pt.life = RandF(0.18f, 0.42f);
        pt.size = size * RandF(0.7f, 1.3f);
        pt.col  = col;
        pt.gravity = true;
        pt.streak  = true;
        g.particles.push_back(pt);
    }
}

void SpawnEffect(Game& g, EffectKind kind, Vector3 pos, Vector3 dir, float life,
                 float size0, float size1, Color col) {
    Effect e{};
    e.kind = kind;
    e.pos  = pos;
    e.dir  = (Vector3Length(dir) > 0.01f) ? Vector3Normalize(dir) : Vector3{0, 1, 0};
    e.life = e.maxLife = life;
    e.size0 = size0;
    e.size1 = size1;
    e.angle = 0.0f;
    e.col  = col;
    g.effects.push_back(e);
}

void UpdateEffects(Game& g, float dt) {
    for (size_t i = 0; i < g.effects.size();) {
        g.effects[i].life -= dt;
        if (g.effects[i].life <= 0.0f) {
            g.effects[i] = g.effects.back();
            g.effects.pop_back();
            continue;
        }
        ++i;
    }
}

void DrawEffects(Game& g) {
    for (const Effect& e : g.effects) {
        float t = 1.0f - Sat(e.life / fmaxf(0.0001f, e.maxLife));   // 0→1
        float ease = 1.0f - (1.0f - t) * (1.0f - t);                // 最初速く、後でゆっくり
        float r = e.size0 + (e.size1 - e.size0) * ease;
        Color c = e.col;
        c.a = (unsigned char)(e.col.a * (1.0f - t));

        switch (e.kind) {
            case EffectKind::Ring:
                // 二重の輪。太さを出すために半径を少しずらして重ねる。
                DrawCircle3D(e.pos, r,          e.dir, 90.0f, c);
                DrawCircle3D(e.pos, r * 0.93f,  e.dir, 90.0f, c);
                DrawCircle3D(e.pos, r * 0.86f,  e.dir, 90.0f,
                             Color{c.r, c.g, c.b, (unsigned char)(c.a / 2)});
                break;

            case EffectKind::Shockwave: {
                // 地面を走る太い輪
                for (int k = 0; k < 4; ++k) {
                    float rr = r - 0.06f * k;
                    if (rr <= 0.0f) continue;
                    DrawCircle3D(Vector3{e.pos.x, e.pos.y + 0.02f + 0.02f * k, e.pos.z}, rr,
                                 Vector3{1, 0, 0}, 90.0f, c);
                }
                break;
            }

            case EffectKind::Flash: {
                // 一瞬の球＋放射状のトゲ。当たった瞬間を一番強く見せる。
                float fr = e.size0 * (1.0f - t) + e.size1 * t;
                DrawSphere(e.pos, fr * 0.45f, c);
                for (int k = 0; k < 8; ++k) {
                    float a = (float)k * (2.0f * PI / 8.0f) + e.angle;
                    Vector3 d = Vector3{cosf(a), sinf(a) * 0.55f, sinf(a * 1.7f) * 0.55f};
                    d = Vector3Normalize(d);
                    DrawCylinderEx(Vector3Add(e.pos, Vector3Scale(d, fr * 0.35f)),
                                   Vector3Add(e.pos, Vector3Scale(d, fr * 1.25f)),
                                   0.09f * (1.0f - t), 0.0f, 4, c);
                }
                break;
            }

        }
    }
}

void SpawnJetPuff(Game& g, Vector3 pos) {
    // ジェットは「浮いている」ことが読めれば十分で、主役ではない。
    // 主役はプレイヤーのシルエットと、その先の地形。派手さは params で戻せる。
    const Params& p = g.p;
    Particle pt{};
    pt.pos = Vector3{pos.x + RandF(-0.10f, 0.10f), pos.y, pos.z + RandF(-0.10f, 0.10f)};
    pt.vel = Vector3{RandF(-0.6f, 0.6f), RandF(-4.2f, -2.2f), RandF(-0.6f, 0.6f)};
    pt.maxLife = pt.life = 0.18f;
    pt.size = p.jetPuffSize;
    pt.col  = Color{255, 200, 120, (unsigned char)Clamp(p.jetFlameAlpha, 0.0f, 255.0f)};
    pt.gravity = false;
    g.particles.push_back(pt);
}

void UpdateParticles(Game& g, float dt) {
    for (size_t i = 0; i < g.particles.size();) {
        Particle& pt = g.particles[i];
        pt.life -= dt;
        if (pt.life <= 0.0f) {
            g.particles[i] = g.particles.back();
            g.particles.pop_back();
            continue;
        }
        if (pt.gravity) pt.vel.y -= 24.0f * dt;
        pt.pos = Vector3Add(pt.pos, Vector3Scale(pt.vel, dt));
        ++i;
    }
}

void DrawParticles(Game& g) {
    for (const Particle& pt : g.particles) {
        float k = Sat(pt.life / pt.maxLife);
        Color c = pt.col;
        c.a = (unsigned char)(255.0f * k);
        if (pt.streak && Vector3Length(pt.vel) > 0.5f) {
            // 速度方向に伸ばして火花に見せる
            Vector3 d = Vector3Scale(Vector3Normalize(pt.vel), pt.size * 3.2f * k);
            DrawCylinderEx(pt.pos, Vector3Add(pt.pos, d), pt.size * 0.5f * k, 0.0f, 4, c);
        } else {
            DrawCube(pt.pos, pt.size * k, pt.size * k, pt.size * k, c);
        }
    }
}

// ────────────────────────────────────────────── カメラ揺れ

void AddCameraShake(Game& g, float amp) {
    if (amp <= 0.0f) return;
    if (amp > g.rig.shake) g.rig.shake = amp;
    g.rig.shakeSeed += 1.7f;
}

void AddCameraKick(Game& g, float amount) {
    if (amount > g.rig.kick) g.rig.kick = amount;
}

// ────────────────────────────────────────────── HUD トースト

void Toast(Game& g, const char* msg) {
    snprintf(g.toast, sizeof(g.toast), "%s", msg);
    g.toastTimer = 2.0f;
}

// ────────────────────────────────────────────── Event → 見せ方
//
// 音とハプティクスはまだ実装していないので、鳴らすべき場所にコメントだけ置いてある。
// SurfaceProfile を導入する時はこの関数から引くことになる。
void ApplyEventToPresentation(Game& g, const GameEvent& e) {
    const Params& p = g.p;

    switch (e.type) {
        case GameEventType::TargetHit:
            // 当たったのに壊れていない、を伝える：白い閃光＋火花＋小さい輪
            SpawnEffect(g, EffectKind::Flash, e.position, Vector3{0, 1, 0}, 0.14f,
                        0.6f, 1.5f, Color{255, 255, 235, 255});
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, p.fxRingLife * 0.7f,
                        0.3f, p.fxRingSize * 0.55f, Color{255, 240, 170, 230});
            SpawnSparks(g, e.position, Vector3{0, 1, 0}, 10, Color{255, 235, 160, 255}, 7.0f, 0.10f);
            AddCameraShake(g, p.shakeBreak * 0.45f);
            g.hitStop = p.hitStopTime;
            // Audio: 硬い物を叩いた音 / Haptics: 短い1発
            break;

        case GameEventType::TargetBroken:
            // 壊れた、を伝える：大きい閃光＋衝撃波＋破片＋カメラが一瞬寄る
            SpawnEffect(g, EffectKind::Flash, e.position, Vector3{0, 1, 0}, 0.18f,
                        0.9f, 2.6f, Color{255, 250, 220, 255});
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, p.fxRingLife,
                        0.4f, p.fxRingSize, Color{255, 190, 120, 240});
            SpawnEffect(g, EffectKind::Shockwave,
                        Vector3{e.position.x, e.position.y - 0.45f, e.position.z},
                        Vector3{0, 1, 0}, p.fxRingLife * 1.2f, 0.5f, p.fxRingSize * 1.3f,
                        Color{255, 225, 180, 200});
            SpawnBurst(g, e.position, 20, Color{255, 140, 90, 255}, 8.0f, 0.22f);
            SpawnSparks(g, e.position, Vector3{0, 1, 0}, 16, Color{255, 210, 140, 255}, 10.0f, 0.12f);
            AddCameraShake(g, p.shakeBreak);
            AddCameraKick(g, p.camKickBreak);
            g.hitStop = p.hitStopTime * 1.6f;
            // Audio: 破壊音 / Haptics: 弾ける感触
            break;

        case GameEventType::CoinTaken:
            SpawnBurst(g, e.position, 8, GOLD, 4.5f, 0.16f);
            // Audio: コイン
            break;

        case GameEventType::BotSaved:
            SpawnBurst(g, e.position, 26, Color{120, 220, 255, 255}, 7.0f, 0.22f);
            AddCameraShake(g, p.shakeBreak * 0.5f);
            Toast(g, "BOT RESCUED!");
            break;

        case GameEventType::SpringBounced:
            SpawnBurst(g, e.position, 14, Color{255, 230, 120, 255}, 6.0f, 0.18f);
            AddCameraShake(g, p.shakeLand);
            // Haptics: 押し込まれてから跳ね返る2段の振動
            break;

        case GameEventType::SlideEntered:
            SpawnBurst(g, e.position, 20, Color{190, 240, 255, 255}, 6.0f, 0.20f);
            AddCameraShake(g, p.shakeLand * 1.5f);
            Toast(g, "WATER SLIDE!");
            // Audio: 水に飛び込む音 / Haptics: 水流の連続振動を開始
            break;

        case GameEventType::SlideExited:
            SpawnBurst(g, e.position, 16, Color{220, 250, 255, 255}, 7.0f, 0.20f);
            // Haptics: 連続振動を止める
            break;

        case GameEventType::WireAttached:
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, 0.28f,
                        0.3f, 1.6f, Color{255, 240, 170, 235});
            SpawnSparks(g, e.position, Vector3{0, -1, 0}, 8, Color{255, 245, 200, 255}, 5.0f, 0.09f);
            // Haptics: 引っ掛かった一瞬のコツン
            break;

        case GameEventType::WireReleased:
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, 0.22f,
                        0.3f, 1.4f, Color{255, 250, 210, 200});
            break;

        // 驚きの瞬間はいちばん強く出す。止めて、揺らして、寄って、光らせる。
        case GameEventType::SurpriseRevealed:
            SpawnEffect(g, EffectKind::Flash, e.position, Vector3{0, 1, 0}, 0.26f,
                        1.2f, 4.0f, Color{255, 255, 230, 255});
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, p.fxRingLife * 1.4f,
                        0.5f, p.fxRingSize * 1.6f, Color{255, 235, 150, 240});
            SpawnEffect(g, EffectKind::Shockwave,
                        Vector3{e.position.x, e.position.y - 1.0f, e.position.z},
                        Vector3{0, 1, 0}, p.fxRingLife * 1.6f, 0.6f, p.fxRingSize * 2.0f,
                        Color{255, 225, 160, 210});
            SpawnBurst(g, e.position, 30, Color{255, 200, 120, 255}, 10.0f, 0.24f);
            SpawnSparks(g, e.position, Vector3{0, 1, 0}, 20, Color{255, 245, 200, 255}, 12.0f, 0.13f);
            AddCameraShake(g, p.shakeDamage * 1.2f);
            AddCameraKick(g, p.camKickBreak * 1.6f);
            g.hitStop = p.hitStopTime * 4.0f;
            Toast(g, "!?");
            break;

        case GameEventType::CratePushed:
            SpawnBurst(g, e.position, 10, Color{215, 175, 120, 255}, 5.0f, 0.17f);
            AddCameraShake(g, p.shakeLand * 0.8f);
            AddCameraKick(g, 0.010f);
            // Audio: 木箱がぶつかる音 / Haptics: 短い一発
            break;

        case GameEventType::ButtonActivated:
            SpawnBurst(g, e.position, 16, Color{140, 255, 200, 255}, 5.0f, 0.18f);
            AddCameraShake(g, p.shakeLand * 2.0f);
            Toast(g, "GATE OPEN");
            break;

        case GameEventType::EnemyDefeated:
            SpawnEffect(g, EffectKind::Flash, e.position, Vector3{0, 1, 0}, 0.20f,
                        1.0f, 3.0f, Color{245, 225, 255, 255});
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, p.fxRingLife,
                        0.4f, p.fxRingSize, Color{210, 150, 255, 240});
            SpawnEffect(g, EffectKind::Shockwave,
                        Vector3{e.position.x, e.position.y - 0.4f, e.position.z},
                        Vector3{0, 1, 0}, p.fxRingLife * 1.2f, 0.5f, p.fxRingSize * 1.2f,
                        Color{225, 190, 255, 200});
            SpawnBurst(g, e.position, 20, Color{200, 130, 255, 255}, 7.0f, 0.22f);
            SpawnSparks(g, e.position, Vector3{0, 1, 0}, 14, Color{235, 200, 255, 255}, 9.0f, 0.12f);
            AddCameraShake(g, p.shakeBreak);
            AddCameraKick(g, p.camKickBreak);
            g.hitStop = p.hitStopTime * 1.8f;
            break;

        case GameEventType::PlayerDamaged:
            SpawnEffect(g, EffectKind::Flash, e.position, Vector3{0, 1, 0}, 0.22f,
                        0.9f, 2.4f, Color{255, 120, 120, 255});
            SpawnEffect(g, EffectKind::Ring, e.position, Vector3{0, 1, 0}, p.fxRingLife,
                        0.4f, p.fxRingSize * 0.9f, Color{255, 90, 90, 230});
            SpawnBurst(g, e.position, 18, Color{255, 90, 90, 255}, 6.0f, 0.20f);
            AddCameraShake(g, p.shakeDamage);
            g.hitStop = p.hitStopTime * 2.0f;
            Toast(g, "OUCH!");
            break;

        case GameEventType::PlayerLanded: {
            // value に着地の強さ（0..100）が入っている
            float k = Sat((float)e.value / 100.0f);
            if (k > 0.15f) {
                SpawnBurst(g, e.position, 4 + (int)(10 * k), Color{235, 235, 220, 255},
                           3.0f + 4.0f * k, 0.14f);
                AddCameraShake(g, p.shakeLand * k);
            }
            break;
        }

        case GameEventType::AbilityGained:
            SpawnBurst(g, e.position, 30, Color{255, 220, 120, 255}, 8.0f, 0.22f);
            Toast(g, TextFormat("ABILITY: %s", AbilityName((AbilityType)e.value)));
            break;

        case GameEventType::StageCleared:
            SpawnBurst(g, e.position, 60, GOLD, 10.0f, 0.25f);
            g.clearBanner = 4.0f;
            break;

        case GameEventType::CrumbleBroke:
            SpawnBurst(g, e.position, 16, Color{222, 166, 152, 255}, 5.0f, 0.22f);
            SpawnEffect(g, EffectKind::Shockwave, e.position, Vector3{0, 1, 0},
                        0.35f, 0.4f, 2.2f, Color{200, 140, 125, 200});
            AddCameraShake(g, p.shakeLand * 0.8f);
            // Audio: 石が割れて落ちる音 / Haptics: 短い落下感
            break;

        case GameEventType::DebrisImpact: {
            // 破片が強くぶつかった。value = 衝突速度 ×10。
            // 物理の結果を「触った感じ」に変換する唯一の場所で、
            // 『アストロボット』が触れた物の数と速さからハプティクスと音を
            // 作っているのと同じ位置づけ。今は火花だけを出している。
            float k = Sat((float)e.value / 100.0f);
            SpawnSparks(g, e.position, Vector3{0, 1, 0}, 1 + (int)(3 * k),
                        Color{255, 225, 170, 255}, 2.0f + 4.0f * k, 0.06f);
            if (k > 0.55f) AddCameraShake(g, 0.03f * k);
            // Audio:   小石が当たる音。k で音量とピッチを振る
            // Haptics: 弱く短いパルス。同時にぶつかった数だけ重ねる
            break;
        }
    }
}
