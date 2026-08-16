// sim.h ─ 描画なしのシミュレーション。tests.cpp と tour.cpp が使う。
//
// FrameStep とは別に、入力を直接組み立てて Update 群だけを回す経路を用意している。
// これがあるおかげでテストは1秒未満で終わり、CI にも置ける。
#pragma once
#include "game.h"

struct SimCfg {
    Vector2 move{0, 0};
    float   jumpHoldTime = 0.0f;   // この秒数だけジャンプボタンを押し続ける
    bool    punchHold    = false;
    float   punchRepeat  = 0.0f;   // >0 ならこの間隔でパンチを押し直す
    bool    abilityHold  = false;  // 能力ボタンを押し続ける
    bool    stopOnLand   = false;  // 一度離陸してから着地した時点で止める
    const char* csvPath  = nullptr; // 指定すると毎フレームの状態を CSV に出す
};

struct SimOut {
    float   maxY               = 0.0f;
    float   airTime            = 0.0f;
    float   maxSpeed           = 0.0f;   // 水平速度の最大
    float   horizontalDistance = 0.0f;   // 開始位置からの水平直線距離
    Vector3 endPos{0, 0, 0};
};

// 固定 dt (1/60) で seconds ぶん回す
SimOut Sim(Game& g, float seconds, SimCfg cfg);

// 1フレームだけ進める（描画・入力収集なし）
void SimStep(Game& g, float dt);

// プレイヤーを任意の位置へ置き直す（カメラ前方 = +Z に固定される）
void PlacePlayer(Game& g, Vector3 pos, bool grounded);
