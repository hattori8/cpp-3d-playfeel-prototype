// params.h ─ 触り心地の数値をスキーマ込みで1箇所に集める。
//
// X(名前, 初期値, 最小, 最大, 編集ステップ, 説明) を1行足すだけで、
//   ・Params 構造体のフィールド
//   ・params.txt の読み書き
//   ・ゲーム内エディタ（F3）の一覧と増減幅
//   ・読み込み時のクランプ
// が同時に増える。追加時に複数箇所を直す作業が発生しないようにするための仕掛け。
#pragma once
#include <cstddef>

#define PARAM_LIST                                                                                  \
  /* ── 移動 */                                                                                     \
  X(moveSpeed,        8.50f,   0.0f,  30.0f,  0.50f, "最大歩行速度 (m/s)")                           \
  X(moveAccel,       70.0f,    1.0f, 300.0f, 5.00f, "地上の加速度")                                 \
  X(moveDecel,       55.0f,    1.0f, 300.0f, 5.00f, "地上の減速度 (入力なし)")                      \
  X(airAccel,        34.0f,    1.0f, 200.0f, 2.00f, "空中の加速度")                                 \
  X(turnSpeed,       14.0f,    1.0f,  60.0f, 1.00f, "向きの追従速度 (rad/s)")                       \
  /* ── ジャンプ */                                                                                 \
  X(gravity,         34.0f,    5.0f, 120.0f, 2.00f, "重力")                                         \
  X(maxFall,         28.0f,    5.0f, 100.0f, 2.00f, "最大落下速度")                                 \
  X(jumpSpeed,       12.50f,   3.0f,  35.0f, 0.50f, "ジャンプ初速")                                 \
  X(jumpCutSpeed,     4.00f,   0.0f,  20.0f, 0.25f, "ボタンを離した時の上昇打ち切り速度")           \
  X(coyoteTime,       0.090f,  0.0f,   0.5f, 0.01f, "地面を離れてもジャンプできる猶予 (s)")         \
  X(jumpBufferTime,   0.100f,  0.0f,   0.5f, 0.01f, "着地前の先行入力を拾う時間 (s)")               \
  X(apexThreshold,    3.00f,   0.0f,  10.0f, 0.25f, "この上下速度以下を頂点付近と見なす")           \
  X(apexGravityScale, 0.550f,  0.1f,   1.0f, 0.05f, "頂点付近の重力倍率 (滞空感)")                  \
  /* ── ホバー */                                                                                   \
  X(hoverDelay,       0.280f,  0.0f,   1.0f, 0.02f, "ジャンプ後ホバーに移れるまでの時間 (s)")       \
  X(hoverFuelMax,     1.600f,  0.0f,   5.0f, 0.10f, "ホバー燃料 (s)")                               \
  X(hoverTargetVy,   -0.600f, -8.0f,   4.0f, 0.10f, "ホバー中の目標上下速度 (ゆっくり沈む)")        \
  X(hoverAccel,      55.0f,    1.0f, 200.0f, 5.00f, "落下時にホバーが支える強さ")                   \
  X(hoverBrake,      16.0f,    1.0f, 200.0f, 2.00f, "上昇をホバーが打ち消す強さ (小さいと跳ねが残る)") \
  X(hoverStartBoost,  2.500f, -5.0f,  15.0f, 0.25f, "ホバー開始時の一押し")                         \
  X(hoverControlMul,  1.350f,  0.1f,   3.0f, 0.05f, "ホバー中の空中制御倍率")                       \
  /* ── 攻撃 */                                                                                     \
  X(punchDuration,    0.280f,  0.05f,  1.0f, 0.02f, "スピンパンチの持続 (s)")                       \
  X(punchCooldown,    0.360f,  0.05f,  1.5f, 0.02f, "パンチの再使用間隔 (s)")                       \
  X(punchRange,       1.250f,  0.2f,   4.0f, 0.05f, "パンチ判定の前方オフセット")                   \
  X(punchRadius,      1.050f,  0.2f,   4.0f, 0.05f, "パンチ判定の半径")                             \
  X(punchLunge,       4.00f,   0.0f,  20.0f, 0.50f, "パンチ時の前方への踏み込み速度")               \
  X(punchImpulse,    17.0f,    0.0f,  60.0f, 1.00f, "命中時に相手を飛ばす力")                       \
  X(laserInterval,    0.120f,  0.02f,  1.0f, 0.01f, "ホバー中レーザーの発射間隔 (s)")               \
  X(laserRange,      16.0f,    1.0f,  60.0f, 1.00f, "レーザー射程")                                 \
  /* ── 能力: ダッシュ */                                                                           \
  X(dashSpeed,       18.0f,    1.0f,  60.0f, 1.00f, "ダッシュ中の速度")                             \
  X(dashAccel,       90.0f,    5.0f, 400.0f, 5.00f, "ダッシュの立ち上がり")                         \
  X(dashFuelMax,      0.700f,  0.1f,   3.0f, 0.05f, "ダッシュの持続 (s)")                           \
  X(dashCooldown,     0.250f,  0.0f,   2.0f, 0.02f, "ダッシュ終了後の再使用間隔 (s)")               \
  X(dashTurnMul,      0.350f,  0.0f,   1.0f, 0.05f, "ダッシュ中の旋回のききにくさ")                 \
  X(dashHitRadius,    1.100f,  0.2f,   4.0f, 0.05f, "ダッシュ中の体当たり判定半径")                 \
  /* ── 能力: ワイヤー */                                                                           \
  X(wireRange,       15.0f,    2.0f,  40.0f, 0.50f, "ワイヤーが届く距離")                           \
  X(wireAimDot,       0.150f, -1.0f,   1.0f, 0.05f, "狙いの許容（大きいほど正面のみ）")             \
  X(wireMinLength,    2.20f,   0.5f,  10.0f, 0.10f, "ロープの最短")                                 \
  X(wireReelSpeed,    2.50f,   0.0f,  30.0f, 0.25f, "押し続けた時にたぐり寄せる速さ")               \
  X(wireGravityScale, 1.000f,  0.0f,   2.0f, 0.05f, "ぶら下がり中の重力倍率")                       \
  X(wireAirAccel,    18.0f,    0.0f, 100.0f, 1.00f, "ぶら下がり中の空中制御")                       \
  X(wireSwingBoost,   7.00f,   0.0f,  30.0f, 0.50f, "振り出しの初速（棒立ちでも振れる）")           \
  X(wireReleaseBoost, 3.50f,   0.0f,  20.0f, 0.25f, "離した瞬間の上向きの足し")                     \
  X(wireDamping,      0.150f,  0.0f,   3.0f, 0.05f, "ロープの減衰（大きいと止まる）")               \
  X(wirePullSpeed,   16.0f,    1.0f,  60.0f, 1.00f, "Heavy を引き寄せる速さ")                       \
  X(wireCamDistMul,   1.250f,  0.5f,   3.0f, 0.05f, "ぶら下がり中のカメラ距離の倍率")                       \
  /* ── ウォータースライダー */                                                                     \
  X(slideGravity,    40.0f,    1.0f, 120.0f, 2.00f, "斜面に沿って加速する強さ")                     \
  X(slideFlowSpeed,  15.0f,    0.0f,  40.0f, 0.50f, "水流が押し出す基準速度")                         \
  X(slideFlowAccel,   2.50f,   0.5f,  60.0f, 0.50f, "基準速度へ寄る速さ（弱くすると勾配が効く）")                             \
  X(slideMinSpeed,    8.00f,   0.0f,  30.0f, 0.50f, "スライダー中の最低速度")                       \
  X(slideMaxSpeed,   30.0f,    2.0f,  60.0f, 1.00f, "スライダー中の最高速度")                       \
  X(slideSteer,      14.0f,    0.0f,  60.0f, 1.00f, "左右に寄せる速さ")                             \
  X(slideSideMax,     0.650f,  0.0f,   1.0f, 0.05f, "横に寄れる量（半径に対する比）")               \
  X(slideBank,        0.550f,  0.0f,   2.0f, 0.05f, "横に寄った時に壁を登る量")                     \
  X(slideExitBoost,   1.150f,  0.5f,   3.0f, 0.05f, "出口で速度に掛ける倍率")                       \
  X(slideJumpOff,     9.00f,   0.0f,  25.0f, 0.50f, "途中で飛び降りる時の上向き初速")               \
  X(slideCamTurn,     4.00f,   0.2f,  20.0f, 0.25f, "滑走中にカメラが進行方向へ回る速さ")             \
  X(slideCamDistMul,  1.300f,  0.5f,   3.0f, 0.05f, "滑走中のカメラ距離の倍率")                       \
  /* ── ギミック */                                                                                 \
  X(springPower,     20.0f,    3.0f,  45.0f, 1.00f, "バネで打ち上げられる初速")                     \
  X(buttonOpenTime,   6.00f,   0.5f,  30.0f, 0.50f, "ボタンでゲートが開いている時間 (s)")           \
  X(gateMoveSpeed,    6.00f,   0.5f,  30.0f, 0.50f, "ゲートの開閉速度")                             \
  /* ── 敵 */                                                                                       \
  X(enemySpeed,       3.00f,   0.0f,  15.0f, 0.25f, "敵の巡回速度")                                 \
  X(stompBounce,     11.0f,    0.0f,  30.0f, 0.50f, "踏みつけ後に跳ね上がる初速")                   \
  X(damageKnockback, 10.0f,    0.0f,  40.0f, 0.50f, "被弾時に吹き飛ばされる速さ")                   \
  X(invulnTime,       1.00f,   0.0f,   5.0f, 0.10f, "被弾後の無敵時間 (s)")                         \
  /* ── 地形 */                                                                                     \
  X(stepHeight,       0.450f,  0.0f,   2.0f, 0.05f, "自動で登れる段差の高さ")                       \
  /* ── カメラ */                                                                                   \
  X(camDist,          8.00f,   2.0f,  25.0f, 0.25f, "カメラ距離")                                   \
  X(camTargetY,       0.850f, -2.0f,   5.0f, 0.05f, "注視点の高さオフセット")                       \
  X(camYawSpeed,      2.600f,  0.2f,  10.0f, 0.10f, "カメラ左右の回転速度 (rad/s)")                 \
  X(camPitchDefault,  0.300f, -0.3f,   1.2f, 0.02f, "通常の見下ろし角 (rad)")                       \
  X(camPitchFall,     0.620f, -0.3f,   1.2f, 0.02f, "落下中の見下ろし角 (rad)")                     \
  X(camPitchSmooth,   3.500f,  0.2f,  20.0f, 0.25f, "見下ろし角の追従速度")                         \
  X(camPosSmooth,    11.0f,    0.5f,  40.0f, 0.50f, "カメラ位置の追従速度")                         \
  X(camShakeDecay,    6.00f,   0.5f,  30.0f, 0.50f, "カメラ揺れの減衰")                             \
  /* ── 演出（攻撃の見え方） */                                                                     \
  X(punchArcWidth,    1.500f,  0.3f,   5.0f, 0.05f, "スピンパンチの弧の大きさ")                     \
  X(punchArcSweep,  240.0f,   30.0f, 720.0f,10.00f, "弧が振り抜ける角度 (度)")                       \
  X(laserWidth,       0.220f,  0.02f,  1.0f, 0.01f, "レーザーの太さ")                               \
  X(fxRingLife,       0.320f,  0.05f,  2.0f, 0.02f, "衝撃リングの寿命 (s)")                         \
  X(fxRingSize,       2.800f,  0.3f,  10.0f, 0.10f, "衝撃リングの最大半径")                         \
  X(camKickBreak,     0.120f,  0.0f,   0.5f, 0.01f, "破壊時にカメラが寄る量")                       \
  /* ── 演出（ジェット。控えめが既定。派手にしたければ数値を上げる） */                             \
  X(jumpPuffCount,    3.0f,    0.0f,  20.0f, 1.00f, "ジャンプ時に足元へ出る土煙の数")              \
  X(hoverStartPuff,   4.0f,    0.0f,  20.0f, 1.00f, "ホバー開始の一吹きの粒の数")                  \
  X(jetPuffRate,     14.0f,    0.0f,  90.0f, 1.00f, "ホバー中のジェット粒の毎秒発生数")            \
  X(jetPuffSize,      0.115f,  0.02f,  0.5f, 0.005f,"ジェット粒の大きさ")                          \
  X(jetFlameLen,      0.380f,  0.0f,   1.5f, 0.02f, "ジェット炎の長さ")                            \
  X(jetFlameWidth,    0.075f,  0.01f,  0.4f, 0.005f,"ジェット炎の太さ")                            \
  X(jetFlameAlpha,  165.0f,    0.0f, 255.0f, 5.00f, "ジェット炎と粒の濃さ (0=消える)")             \
  /* ── 演出 */                                                                                     \
  X(hitStopTime,      0.055f,  0.0f,   0.3f, 0.005f,"命中時のヒットストップ (s)")                   \
  X(shakeBreak,       0.220f,  0.0f,   2.0f, 0.02f, "破壊時のカメラ揺れ")                           \
  X(shakeDamage,      0.500f,  0.0f,   2.0f, 0.02f, "被弾時のカメラ揺れ")                           \
  X(shakeLand,        0.090f,  0.0f,   2.0f, 0.01f, "着地時のカメラ揺れ")                           \
  /* ── 取得判定 */                                                                                 \
  X(coinRadius,       1.150f,  0.2f,   4.0f, 0.05f, "コインの取得半径")                             \
  X(botRadius,        1.300f,  0.2f,   4.0f, 0.05f, "ボットの救出半径")                             \
  X(pickupRadius,     1.400f,  0.2f,   4.0f, 0.05f, "能力アイテムの取得半径")                         \
  /* ── 破片（Jolt が回す world 側の物理。主人公の手触りには一切関わらない） */                       \
  X(debrisCount,     11.0f,    0.0f,  40.0f, 1.00f, "1回の破壊で出る破片の数")                       \
  X(debrisSize,       0.210f,  0.05f,  1.0f, 0.01f, "破片の一辺の半分 (m)")                          \
  X(debrisPower,      7.20f,   0.0f,  30.0f, 0.20f, "破片が飛び散る初速 (m/s)")                      \
  X(debrisLift,       0.550f,  0.0f,   3.0f, 0.05f, "破片の初速のうち上向きの割合")                  \
  X(debrisSpin,      14.0f,    0.0f,  60.0f, 0.50f, "破片の初期回転 (rad/s)")                        \
  X(debrisLife,       7.00f,   0.5f,  30.0f, 0.50f, "破片が消えるまでの時間 (s)")                    \
  X(debrisFade,       1.20f,   0.1f,   5.0f, 0.10f, "破片が縮んで消える時間 (s)")                    \
  X(debrisMax,      140.0f,    0.0f, 400.0f,10.00f, "同時に存在できる破片の上限")                    \
  X(debrisRestitution,0.280f,  0.0f,   1.0f, 0.02f, "破片の跳ね返り")                                \
  X(debrisFriction,   0.480f,  0.0f,   2.0f, 0.02f, "破片の摩擦")                                    \
  X(debrisImpactMin,  3.20f,   0.2f,  20.0f, 0.10f, "火花を出す衝突速度のしきい値 (m/s)")            \
  X(debrisPushRadius, 0.620f,  0.1f,   3.0f, 0.02f, "歩いて破片を蹴散らす当たりの半径")

struct Params {
#define X(name, def, lo, hi, step, doc) float name = def;
  PARAM_LIST
#undef X
};

struct ParamEntry {
  const char* name;
  size_t      offset;
  float       def, lo, hi, step;
  const char* doc;
};

static const ParamEntry kParamTable[] = {
#define X(name, def, lo, hi, step, doc) {#name, offsetof(Params, name), def, lo, hi, step, doc},
    PARAM_LIST
#undef X
};
static const int kParamCount = (int)(sizeof(kParamTable) / sizeof(kParamTable[0]));

inline float* ParamPtr(Params& p, int i) {
  return (float*)((char*)&p + kParamTable[i].offset);
}
inline float ParamGet(const Params& p, int i) {
  return *(const float*)((const char*)&p + kParamTable[i].offset);
}

// params.txt を読み込む。見つからなければ false（初期値のまま動く）。
bool LoadParams(Params& p, const char** usedPathOut);
// ファイルが更新されていれば読み直す。読み直したら true。
bool ReloadParamsIfChanged(Params& p);
// スキーマの範囲へ収める
void ClampParams(Params& p);
