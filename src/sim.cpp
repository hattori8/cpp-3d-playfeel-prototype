#include "sim.h"
#include "util.h"
#include <cstdio>
#include <cmath>

// FrameStep と同じ処理順。描画と入力収集だけ抜いてある。
void SimStep(Game& g, float dt) {
    g.hitStop = 0.0f;          // テストでは時間を止めない
    g.time += dt;
    UpdateRigInput(g, dt);
    UpdateLevel(g, dt);
    UpdatePlayer(g, dt);
    UpdateWorldPhysics(g, dt);
    ResolveInteractions(g);
    DispatchGameEvents(g);
    UpdateRigFollow(g, dt);
}

void PlacePlayer(Game& g, Vector3 pos, bool grounded) {
    Player& pl = g.player;
    pl.pos = pos;
    pl.vel = Vector3{0, 0, 0};
    pl.yaw = 0.0f;
    pl.grounded = grounded;
    pl.wasGrounded = grounded;
    pl.hovering = false;
    pl.hoverFuel = g.p.hoverFuelMax;
    pl.timeSinceJump = 99.0f;
    pl.coyote = grounded ? g.p.coyoteTime : 0.0f;
    pl.ridingPlatform = -1;
    pl.invuln = 0.0f;
    pl.ability.active = false;
    pl.ability.cooldown = 0.0f;
    if (pl.ability.type == AbilityType::Dash) pl.ability.value = g.p.dashFuelMax;
    g.rig.yaw = 0.0f;          // カメラ前方 = +Z
    g.rig.curTarget = pos;
    g.rig.curPos = Vector3{pos.x, pos.y + 4.0f, pos.z - 8.0f};
}

SimOut Sim(Game& g, float seconds, SimCfg cfg) {
    const float dt = 1.0f / 60.0f;
    int    frames = (int)(seconds / dt);
    SimOut out;
    out.maxY = g.player.pos.y;
    Vector3 start = g.player.pos;

    float punchAcc = 1e9f;
    bool  tookOff = false;

    FILE* csv = nullptr;
    if (cfg.csvPath) {
        csv = fopen(cfg.csvPath, "wb");
        if (csv) fprintf(csv, "t,x,y,z,vx,vy,vz,speedxz,grounded,hovering,ability,fuel\n");
    }

    for (int i = 0; i < frames; ++i) {
        float t = (float)i * dt;

        g.in = Input{};
        g.in.move        = cfg.move;
        g.in.jumpHeld    = (t < cfg.jumpHoldTime);
        g.in.jumpPressed = (i == 0 && cfg.jumpHoldTime > 0.0f);
        g.in.punchHeld   = cfg.punchHold;
        g.in.abilityHeld = cfg.abilityHold;
        g.in.abilityPressed = (i == 0 && cfg.abilityHold);
        if (cfg.punchRepeat > 0.0f) {
            punchAcc += dt;
            if (punchAcc >= cfg.punchRepeat) {
                punchAcc = 0.0f;
                g.in.punchPressed = true;
                g.in.punchHeld = true;
            }
        }

        SimStep(g, dt);

        const Player& pl = g.player;
        float speedXZ = sqrtf(pl.vel.x * pl.vel.x + pl.vel.z * pl.vel.z);
        if (pl.pos.y > out.maxY) out.maxY = pl.pos.y;
        if (speedXZ > out.maxSpeed) out.maxSpeed = speedXZ;

        if (csv) {
            fprintf(csv, "%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%s,%.3f\n",
                    t, pl.pos.x, pl.pos.y, pl.pos.z, pl.vel.x, pl.vel.y, pl.vel.z, speedXZ,
                    (int)pl.grounded, (int)pl.hovering, AbilityName(pl.ability.type),
                    pl.ability.value);
        }

        if (!pl.grounded) { tookOff = true; out.airTime += dt; }
        else if (tookOff && cfg.stopOnLand) break;
    }

    if (csv) fclose(csv);

    out.endPos = g.player.pos;
    float dx = out.endPos.x - start.x, dz = out.endPos.z - start.z;
    out.horizontalDistance = sqrtf(dx * dx + dz * dz);
    return out;
}
