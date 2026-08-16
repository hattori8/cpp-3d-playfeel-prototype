// world_physics.cpp ─ 「世界の側」の物理だけを Jolt Physics に任せる。
//
// ■ ここに入れるもの
//     壊れた物の破片、押されて転がるもの。当たれば動く。見て気持ちいい。
//     遊びの成否（届く／届かない、勝てる／負ける）には関わらない。
//
// ■ ここに入れないもの
//     主人公の移動と当たり判定。あれは player.cpp の自前 AABB のままにする。
//     理由は3つで、DESIGN.md 11.5 章に書いた通り：
//       1. 手触りの数値が Jolt の内側に入ると params.txt から追い込めなくなる
//       2. [intent] テスト（跳べる高さ、届く距離）が数値で書けなくなる
//       3. 『アストロボット』も Havok に投げているのは流体・布・破壊であって、
//          キャラクターの移動ではない（CEDEC 2025 の講演で語られている構成）
//
// ■ 依存の向き
//     Jolt のヘッダを include するのはこのファイルだけ。game.h には Debris が
//     持つ bodyId（ただの uint）しか出てこない。物理をやめたくなったら、
//     このファイルと CMake の1行を消せば元に戻る。
#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "game.h"
#include "rlgl.h"
#include <cmath>
#include <cstdlib>

JPH_SUPPRESS_WARNINGS

// ══════════════════════════════════════════════ レイヤー
//
// 2枚だけ。静止物（地形）と動く物（破片・主人公の押し出し用）。
// 主人公はここでは「押す側」でしかないので、専用レイヤーは要らない。
namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING     = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}
namespace BPLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM_LAYERS(2);
}

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::NON_MOVING) return b == Layers::MOVING;
        return true;
    }
};

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mMap[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        mMap[Layers::MOVING]     = BPLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return mMap[l]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "layer"; }
#endif
private:
    JPH::BroadPhaseLayer mMap[Layers::NUM_LAYERS];
};

class ObjectVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        if (a == Layers::NON_MOVING) return b == BPLayers::MOVING;
        return true;
    }
};

// ══════════════════════════════════════════════ 接触の記録
//
// Jolt から返ってくる接触を、そのまま演出に流すと数が多すぎる。
// 「速度がしきい値を超えたものだけ」「1フレーム数個まで」に絞って
// GameEvent へ変換する。ここが物理と演出の唯一の接点。
struct Impact { Vector3 pos; float speed; };
static std::vector<Impact> sImpacts;
static float sImpactMin = 3.0f;

class DebrisContactListener final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                        const JPH::ContactManifold& man, JPH::ContactSettings&) override {
        if (sImpacts.size() >= 8) return;
        JPH::Vec3 rel = b1.GetLinearVelocity() - b2.GetLinearVelocity();
        float speed = fabsf(rel.Dot(man.mWorldSpaceNormal));
        if (speed < sImpactMin) return;
        JPH::RVec3 p = man.GetWorldSpaceContactPointOn1(0);
        sImpacts.push_back({Vector3{(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()}, speed});
    }
};

// ══════════════════════════════════════════════ 物理世界の実体
//
// ファイル内 static。Game の外に置いているのは Jolt の型を game.h に
// 持ち込まないためで、シングルトンとして使い回すためではない。
// ResetWorldPhysics() で丸ごと作り直せる（＝Game を作り直せる性質を壊さない）。
static JPH::PhysicsSystem*           sSystem = nullptr;
static JPH::TempAllocatorImpl*       sTemp   = nullptr;
static JPH::JobSystemSingleThreaded* sJobs   = nullptr;
static BPLayerInterfaceImpl          sBPLayer;
static ObjectVsBPLayerFilterImpl     sObjVsBP;
static ObjectLayerPairFilterImpl     sObjVsObj;
static DebrisContactListener         sContacts;

static std::vector<JPH::BodyID> sBoxBodies;    // level.boxes と同じ添字（無効なら Invalid）
static std::vector<Vector3>     sBoxCenters;   // 前フレームの中心（動いたかの判定用）
static std::vector<char>        sBoxSolid;     // 前フレームの solid
static JPH::BodyID              sPlayerBody;
static Vector3                  sPlayerHalf{0, 0, 0};
static float                    sAccum = 0.0f;
static unsigned                 sSeed  = 12345u;

static const float kFixedStep = 1.0f / 60.0f;

// テスト用に決まった乱数を使う（同じ入力なら同じ絵になるほうがデバッグしやすい）
static float Rnd(float lo, float hi) {
    sSeed = sSeed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((sSeed >> 8) & 0xFFFF) / 65535.0f;
}

static inline JPH::RVec3 ToJ(Vector3 v) { return JPH::RVec3(v.x, v.y, v.z); }
static inline Vector3    ToR(JPH::RVec3 v) {
    return Vector3{(float)v.GetX(), (float)v.GetY(), (float)v.GetZ()};
}

// ══════════════════════════════════════════════ 生成 / 破棄

static void EnsureJoltStarted() {
    static bool started = false;
    if (started) return;
    started = true;
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

static void AddBoxBody(Game& g, int i) {
    const Box& b = g.level.boxes[i];
    float minHalf = fminf(b.h.x, fminf(b.h.y, b.h.z));
    float radius  = fminf(0.05f, minHalf * 0.4f);
    JPH::BoxShapeSettings shape(JPH::Vec3(b.h.x, b.h.y, b.h.z), radius);
    shape.SetEmbedded();
    JPH::ShapeSettings::ShapeResult res = shape.Create();
    if (res.HasError()) return;

    // 動く床とゲートだけ kinematic。破片が床に乗って一緒に運ばれる。
    bool moves = (b.kind == BOX_MOVING || b.kind == BOX_GATE);
    JPH::BodyCreationSettings bcs(res.Get(), ToJ(b.c), JPH::Quat::sIdentity(),
                                  moves ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
                                  Layers::NON_MOVING);
    bcs.mFriction    = 0.6f;
    bcs.mRestitution = 0.1f;
    JPH::Body* body = sSystem->GetBodyInterface().CreateBody(bcs);
    if (!body) return;
    sSystem->GetBodyInterface().AddBody(body->GetID(), JPH::EActivation::DontActivate);
    sBoxBodies[i]  = body->GetID();
    sBoxCenters[i] = b.c;
    sBoxSolid[i]   = b.solid ? 1 : 0;
}

void ResetWorldPhysics(Game& g) {
    EnsureJoltStarted();

    g.debris.clear();
    sImpacts.clear();
    sAccum = 0.0f;
    sSeed  = 12345u;

    delete sSystem; sSystem = nullptr;
    delete sJobs;   sJobs   = nullptr;
    delete sTemp;   sTemp   = nullptr;

    sTemp   = new JPH::TempAllocatorImpl(8 * 1024 * 1024);
    sJobs   = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
    sSystem = new JPH::PhysicsSystem();
    sSystem->Init(2048, 0, 4096, 2048, sBPLayer, sObjVsBP, sObjVsObj);
    sSystem->SetGravity(JPH::Vec3(0.0f, -g.p.gravity, 0.0f));
    sSystem->SetContactListener(&sContacts);

    // 地形をそのまま静的ボディにする。当たり判定の「正」は自前 AABB のままで、
    // こちらは破片が乗る床でしかない。二重に持つのを許しているのは、
    // 主人公の挙動を Jolt に一切依存させないため。
    size_t n = g.level.boxes.size();
    sBoxBodies.assign(n, JPH::BodyID());
    sBoxCenters.assign(n, Vector3{0, 0, 0});
    sBoxSolid.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const Box& b = g.level.boxes[i];
        if (b.kind == BOX_SCENERY || !b.solid) continue;
        AddBoxBody(g, (int)i);
    }

    // 主人公は kinematic の箱。押す側にしかならない（破片に押し返されない）。
    sPlayerHalf = Vector3{g.p.debrisPushRadius, g.player.half.y, g.p.debrisPushRadius};
    JPH::BoxShapeSettings pshape(JPH::Vec3(sPlayerHalf.x, sPlayerHalf.y, sPlayerHalf.z), 0.05f);
    pshape.SetEmbedded();
    JPH::ShapeSettings::ShapeResult pres = pshape.Create();
    if (!pres.HasError()) {
        JPH::BodyCreationSettings bcs(pres.Get(), ToJ(g.player.pos), JPH::Quat::sIdentity(),
                                      JPH::EMotionType::Kinematic, Layers::MOVING);
        bcs.mFriction = 0.4f;
        JPH::Body* body = sSystem->GetBodyInterface().CreateBody(bcs);
        if (body) {
            sPlayerBody = body->GetID();
            sSystem->GetBodyInterface().AddBody(sPlayerBody, JPH::EActivation::Activate);
        }
    }

    sSystem->OptimizeBroadPhase();
}

// ══════════════════════════════════════════════ 破片を出す

void SpawnDebrisBurst(Game& g, Vector3 pos, Vector3 dir, int n, float power, Color col) {
    if (!sSystem || n <= 0) return;
    JPH::BodyInterface& bi = sSystem->GetBodyInterface();

    // 上限を超えたら古いものから消す。「増え続けない」ことを保証する。
    int limit = (int)g.p.debrisMax;
    while ((int)g.debris.size() + n > limit && !g.debris.empty()) {
        bi.RemoveBody(JPH::BodyID(g.debris.front().bodyId));
        bi.DestroyBody(JPH::BodyID(g.debris.front().bodyId));
        g.debris.erase(g.debris.begin());
    }

    float base = g.p.debrisSize;
    for (int i = 0; i < n; ++i) {
        float s = base * Rnd(0.6f, 1.4f);
        JPH::BoxShapeSettings shape(JPH::Vec3(s, s * Rnd(0.7f, 1.3f), s), fminf(0.02f, s * 0.3f));
        shape.SetEmbedded();
        JPH::ShapeSettings::ShapeResult res = shape.Create();
        if (res.HasError()) continue;

        Vector3 at = {pos.x + Rnd(-0.3f, 0.3f), pos.y + Rnd(-0.3f, 0.3f), pos.z + Rnd(-0.3f, 0.3f)};
        JPH::BodyCreationSettings bcs(res.Get(), ToJ(at), JPH::Quat::sIdentity(),
                                      JPH::EMotionType::Dynamic, Layers::MOVING);
        bcs.mFriction    = g.p.debrisFriction;
        bcs.mRestitution = g.p.debrisRestitution;
        bcs.mLinearDamping  = 0.10f;
        bcs.mAngularDamping = 0.25f;
        JPH::Body* body = bi.CreateBody(bcs);
        if (!body) break;

        // 攻撃の向きに散らしつつ、上へ持ち上げる。真横に飛ぶと絵にならない。
        Vector3 v = {dir.x * power + Rnd(-power, power) * 0.55f,
                     power * g.p.debrisLift + Rnd(0.0f, power * 0.5f),
                     dir.z * power + Rnd(-power, power) * 0.55f};
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        bi.SetLinearVelocity(body->GetID(), JPH::Vec3(v.x, v.y, v.z));
        bi.SetAngularVelocity(body->GetID(),
                              JPH::Vec3(Rnd(-1, 1), Rnd(-1, 1), Rnd(-1, 1)) * g.p.debrisSpin);

        Debris d;
        d.pos     = at;
        d.half    = Vector3{s, s, s};
        d.col     = col;
        d.life    = g.p.debrisLife;
        d.maxLife = g.p.debrisLife;
        d.bodyId  = body->GetID().GetIndexAndSequenceNumber();
        g.debris.push_back(d);
    }
}

// Event → 破片。どの出来事が物理を生むかは、ここ1か所だけを見れば分かる。
void ApplyEventToWorldPhysics(Game& g, const GameEvent& e) {
    switch (e.type) {
    case GameEventType::TargetBroken:
        SpawnDebrisBurst(g, e.position, Vector3{0, 0, 0}, (int)g.p.debrisCount,
                         g.p.debrisPower, Color{255, 170, 60, 255});
        break;
    case GameEventType::EnemyDefeated:
        SpawnDebrisBurst(g, e.position, Vector3{0, 0, 0}, (int)(g.p.debrisCount * 0.7f),
                         g.p.debrisPower * 0.8f, Color{230, 90, 110, 255});
        break;
    case GameEventType::SurpriseRevealed:
        // 驚きの瞬間だけ量を増やす。物量そのものが演出になる。
        SpawnDebrisBurst(g, e.position, Vector3{0, 0, 0}, (int)(g.p.debrisCount * 2.0f),
                         g.p.debrisPower * 1.3f, Color{150, 140, 130, 255});
        break;
    default:
        break;
    }
}

// ══════════════════════════════════════════════ 1フレーム

void UpdateWorldPhysics(Game& g, float dt) {
    if (!sSystem) return;
    JPH::BodyInterface& bi = sSystem->GetBodyInterface();

    sSystem->SetGravity(JPH::Vec3(0.0f, -g.p.gravity, 0.0f));
    sImpactMin = g.p.debrisImpactMin;

    // ── 地形の変化を反映（動く床・開くゲート・引き抜かれた岩）
    for (size_t i = 0; i < g.level.boxes.size(); ++i) {
        const Box& b = g.level.boxes[i];
        if (b.kind == BOX_SCENERY) continue;
        bool wantBody = b.solid;
        bool hasBody  = !sBoxBodies[i].IsInvalid();
        if (wantBody && !hasBody) { AddBoxBody(g, (int)i); continue; }
        if (!wantBody && hasBody) {
            bi.RemoveBody(sBoxBodies[i]);
            bi.DestroyBody(sBoxBodies[i]);
            sBoxBodies[i] = JPH::BodyID();
            continue;
        }
        if (!hasBody) continue;
        if (fabsf(b.c.x - sBoxCenters[i].x) > 1e-5f ||
            fabsf(b.c.y - sBoxCenters[i].y) > 1e-5f ||
            fabsf(b.c.z - sBoxCenters[i].z) > 1e-5f) {
            bi.MoveKinematic(sBoxBodies[i], ToJ(b.c), JPH::Quat::sIdentity(), kFixedStep);
            sBoxCenters[i] = b.c;
        }
    }

    // ── 主人公を運ぶ（押す側としてだけ物理世界に存在する）
    if (!sPlayerBody.IsInvalid())
        bi.MoveKinematic(sPlayerBody, ToJ(g.player.pos), JPH::Quat::sIdentity(), kFixedStep);

    // ── 固定 dt で回す。フレームレートが揺れても結果が変わらないように。
    sImpacts.clear();
    sAccum += dt;
    int steps = 0;
    while (sAccum >= kFixedStep && steps < 4) {
        sSystem->Update(kFixedStep, 1, sTemp, sJobs);
        sAccum -= kFixedStep;
        ++steps;
    }
    if (steps == 4) sAccum = 0.0f;   // 追いつけない時は捨てる（渦を巻かせない）

    // ── 結果を読み戻して、寿命を進める
    for (size_t i = 0; i < g.debris.size();) {
        Debris& d = g.debris[i];
        JPH::BodyID id(d.bodyId);
        JPH::RVec3 p;
        JPH::Quat  q;
        bi.GetPositionAndRotation(id, p, q);
        d.pos    = ToR(p);
        d.rot    = Quaternion{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
        d.asleep = !bi.IsActive(id);
        d.life  -= dt;

        if (d.life <= 0.0f || d.pos.y < -30.0f) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
            g.debris.erase(g.debris.begin() + i);
            continue;
        }
        ++i;
    }

    // ── 接触を Event に変換する。物理が演出へ出ていく唯一の口。
    for (const Impact& im : sImpacts) {
        int v = (int)(im.speed * 10.0f);
        PushEvent(g, GameEventType::DebrisImpact, im.pos, 0, v);
    }
}

// ══════════════════════════════════════════════ 描画

void DrawWorldPhysics(Game& g) {
    for (const Debris& d : g.debris) {
        // 最後の debrisFade 秒で縮めて消す。パッと消えると目が拾ってしまう。
        float k = 1.0f;
        if (d.life < g.p.debrisFade) k = fmaxf(0.0f, d.life / g.p.debrisFade);
        if (k <= 0.01f) continue;

        Matrix rot = QuaternionToMatrix(d.rot);
        rlPushMatrix();
        rlTranslatef(d.pos.x, d.pos.y, d.pos.z);
        rlMultMatrixf(MatrixToFloat(rot));
        Vector3 s = Vector3Scale(d.half, 2.0f * k);
        DrawCubeV(Vector3{0, 0, 0}, s, d.col);
        DrawCubeWiresV(Vector3{0, 0, 0}, s,
                       Color{(unsigned char)(d.col.r / 2), (unsigned char)(d.col.g / 2),
                             (unsigned char)(d.col.b / 2), 255});
        rlPopMatrix();
    }
}

// ══════════════════════════════════════════════ テストから見るための窓

int DebrisCount(const Game& g) { return (int)g.debris.size(); }

bool AllDebrisAsleep(const Game& g) {
    for (const Debris& d : g.debris)
        if (!d.asleep) return false;
    return true;
}
