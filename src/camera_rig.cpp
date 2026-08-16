// camera_rig.cpp ─ 後方追従カメラ
//
// 2段階に分けている理由：
//   UpdateRigInput  … プレイヤーより前。プレイヤーの移動は「最新のカメラ方向」を使う
//   UpdateRigFollow … プレイヤーより後。カメラ位置は「最新のプレイヤー位置」を使う
// 1つの関数でやると、どちらかが必ず1フレーム古くなる。
//
// CEDEC「カメラ編」から：
//   ・ジャンプ・落下中は見下ろし角を強める（着地点を見せる）
//   ・平坦な移動中は角度を寝かせて周囲を把握させる
#include "game.h"
#include "util.h"
#include <cmath>

void UpdateRigInput(Game& g, float dt) {
    Rig&          r  = g.rig;
    const Player& pl = g.player;
    const Params& p  = g.p;

    // 左右回転。yaw が増えると前方ベクトルは +X 側へ回る。+X は画面左なので、
    // 「右に倒したら右へ回る」ようにするには yaw を減らす。
    r.yaw -= g.in.look.x * p.camYawSpeed * dt;
    r.yaw = WrapAngle(r.yaw);

    // スライダーに乗っている間は、進行方向の真後ろへカメラを回す。
    // 乗り物は「進んでいる感じを見せる」のが仕事なので、プレイヤーの向き（=水路の接線）に
    // 追従させる。自由に回したい時は降りればいい。
    if (pl.slide.active) {
        float d = WrapAngle(pl.yaw - r.yaw);
        r.yaw = WrapAngle(r.yaw + d * (1.0f - expf(-p.slideCamTurn * dt)));
    }

    // 見下ろし角：落下中は強く、地上では通常
    float wantPitch = p.camPitchDefault;
    if (pl.slide.active) {
        wantPitch = p.camPitchDefault + 0.16f;   // 少し見下ろして水路の先を見せる
    } else if (!pl.grounded && pl.vel.y < -1.0f) {
        float k = Sat(-pl.vel.y / 14.0f);
        wantPitch = p.camPitchDefault + (p.camPitchFall - p.camPitchDefault) * k;
    }
    r.pitch += g.in.look.y * 1.6f * dt;
    r.pitch = Clamp(r.pitch, -0.35f, 1.05f);
    if (fabsf(g.in.look.y) < 0.01f) r.pitch = ExpSmooth(r.pitch, wantPitch, p.camPitchSmooth, dt);
}

void UpdateRigFollow(Game& g, float dt) {
    Rig&          r  = g.rig;
    const Player& pl = g.player;
    const Params& p  = g.p;

    // 注視点：本体より少し上。上下は緩やかに追う（ジャンプで画面が跳ねないように）
    Vector3 wantTarget = Vector3{pl.pos.x, pl.pos.y + p.camTargetY, pl.pos.z};
    r.curTarget.x = ExpSmooth(r.curTarget.x, wantTarget.x, p.camPosSmooth, dt);
    r.curTarget.z = ExpSmooth(r.curTarget.z, wantTarget.z, p.camPosSmooth, dt);
    r.curTarget.y = ExpSmooth(r.curTarget.y, wantTarget.y, pl.grounded ? 7.0f : 3.2f, dt);

    float cp = cosf(r.pitch), sp = sinf(r.pitch);
    Vector3 offset  = Vector3{-sinf(r.yaw) * cp, sp, -cosf(r.yaw) * cp};
    // 命中時に一瞬だけ寄る（当たった手応えを画で出す）
    r.kick = ExpSmooth(r.kick, 0.0f, 9.0f, dt);
    bool wiring = pl.ability.active && pl.ability.type == AbilityType::Wire;
    float   dist    = p.camDist * (pl.slide.active ? p.slideCamDistMul : (wiring ? p.wireCamDistMul : 1.0f))
                    * (1.0f - r.kick);
    Vector3 wantPos = Vector3Add(r.curTarget, Vector3Scale(offset, dist));
    r.curPos = ExpSmooth3(r.curPos, wantPos, p.camPosSmooth * 1.3f, dt);

    // 揺れ（Presentation 層が Event を見て r.shake に値を入れる）
    // 位相は dt で進める。ヒットストップ中も揺れは止めたくないので g.time は使わない。
    r.shake = ExpSmooth(r.shake, 0.0f, p.camShakeDecay, dt);
    Vector3 shakeOff = Vector3{0, 0, 0};
    if (r.shake > 0.001f) {
        r.shakeSeed += dt * 38.0f;
        float t = r.shakeSeed;
        shakeOff = Vector3{sinf(t * 1.7f) * r.shake, sinf(t * 2.3f + 1.1f) * r.shake,
                           sinf(t * 1.3f + 2.7f) * r.shake * 0.5f};
    }

    r.cam.position   = Vector3Add(r.curPos, shakeOff);
    r.cam.target     = Vector3Add(r.curTarget, Vector3Scale(shakeOff, 0.55f));
    r.cam.up         = Vector3{0, 1, 0};
    r.cam.fovy       = 52.0f;
    r.cam.projection = CAMERA_PERSPECTIVE;
}
