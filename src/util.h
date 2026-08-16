// util.h ─ 小さな数学ヘルパー
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <cmath>

inline float MoveTowardsF(float cur, float target, float maxDelta) {
    float d = target - cur;
    if (fabsf(d) <= maxDelta) return target;
    return cur + (d > 0.0f ? maxDelta : -maxDelta);
}

// 指数減衰の追従。フレームレートに依存しない Lerp。
inline float ExpSmooth(float cur, float target, float rate, float dt) {
    return target + (cur - target) * expf(-rate * dt);
}

inline Vector3 ExpSmooth3(Vector3 cur, Vector3 target, float rate, float dt) {
    float k = expf(-rate * dt);
    return Vector3{target.x + (cur.x - target.x) * k,
                   target.y + (cur.y - target.y) * k,
                   target.z + (cur.z - target.z) * k};
}

// 角度差を -PI..PI に畳む
inline float WrapAngle(float a) {
    a = fmodf(a + PI, 2.0f * PI);
    if (a < 0.0f) a += 2.0f * PI;
    return a - PI;
}

inline float MoveTowardsAngle(float cur, float target, float maxDelta) {
    float d = WrapAngle(target - cur);
    if (fabsf(d) <= maxDelta) return target;
    return cur + (d > 0.0f ? maxDelta : -maxDelta);
}

inline float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float RandF(float lo, float hi) {
    return lo + (hi - lo) * (float)GetRandomValue(0, 10000) / 10000.0f;
}
