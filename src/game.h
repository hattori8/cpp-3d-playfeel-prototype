// game.h ─ astro_proto のデータ定義
//
// 設計方針（DESIGN.md 参照）
//   ・主人公は専用コードで磨く          → Player は専用の構造体と専用の更新関数
//   ・世界は小さなデータ部品で構成する  → 型ごとの struct + std::vector
//   ・両者は Interaction / Event でつなぐ
// クラス階層・ECS・マネージャは作らない。struct + free function だけで書く。
#pragma once
#include "raylib.h"
#include "raymath.h"
#include "params.h"
#include <vector>

// ══════════════════════════════════════════════ ID
//
// Interaction / Event で「どれに対して何が起きたか」を指すための軽い ID。
// 種類 + 添字 を1つの int に押し込むだけ。ハンドルの世代管理などはしない。
enum ObjKind {
    OBJ_NONE = 0,
    OBJ_PLAYER,
    OBJ_COIN,
    OBJ_BOT,
    OBJ_TARGET,
    OBJ_SPRING,
    OBJ_BUTTON,
    OBJ_ENEMY,
    OBJ_PICKUP,
    OBJ_SLIDE,
    OBJ_ANCHOR,
    OBJ_GOAL,
    OBJ_CRUMBLE,
    OBJ_CRATE,
};
static const int kIdStride = 10000;

inline int     MakeId(ObjKind k, int index) { return (int)k * kIdStride + index; }
inline ObjKind IdKind(int id)               { return (ObjKind)(id / kIdStride); }
inline int     IdIndex(int id)              { return id % kIdStride; }
static const int PLAYER_ID = OBJ_PLAYER * kIdStride;

// ══════════════════════════════════════════════ World / Level

// 地形。kind は見た目とゲーム上の意味の両方を担う。
// 「見た目 / 当たり判定 / 登れるか / 壊せるか / 表面属性」を独立に組み合わせたく
// なった時点で分離する。先回りはしない。
enum BoxKind {
    BOX_FLOOR   = 0,   // 歩ける床
    BOX_STEP    = 1,   // 登れる段差（明るい色＋黄の縁）
    BOX_WALL    = 2,   // 登れない壁（暗い青灰）
    BOX_MOVING  = 3,   // 動く床
    BOX_SUBPATH = 4,   // サブパスの足場
    BOX_GATE    = 5,   // ボタンで開くゲート
    BOX_CRUMBLE = 6,   // 乗ると崩れる床（ひび割れ模様つき）
    BOX_CRATE   = 7,   // 積み木。位置を Jolt が動かす（崩して足場にする）
    BOX_SCENERY = 9,   // 遠景。当たり判定なし
};

struct Box {
    Vector3 c, h;
    int     kind;
    bool    solid = true;   // false なら見えるけれど当たらない（引き抜かれた岩など）
};

struct MovingPlatform {
    int     boxIndex;
    Vector3 a, b;
    float   speed;
    float   t;
    int     dir;
    Vector3 delta;      // 今フレームの移動量（乗っている物を運ぶ）
};

// ── Collectible
struct Coin { Vector3 pos; bool taken = false; float spin = 0.0f; };
struct Bot  { Vector3 pos; bool saved = false; float bob  = 0.0f; };

// ── 壊せるターゲット
struct Target {
    Vector3 pos, vel, half, home;
    int   hp       = 1;
    bool  alive    = true;
    bool  floating = false;   // true なら空中固定（ホバー中のレーザー用）
    float respawn  = 0.0f;
    float flash    = 0.0f;
    float spin     = 0.0f;
};

// ── Gimmick: バネ（踏むと打ち上げ）
struct Spring {
    Vector3 pos, half;
    float   power    = 0.0f;   // 0 なら params.springPower を使う
    float   compress = 0.0f;   // 見た目の縮み
    float   cooldown = 0.0f;
};

// ── Gimmick: 崩れる床
//
// 乗ると揺れて、delay 秒で落ちる。respawn 秒たつと元の位置へ戻る。
// 当たり判定は専用に持たず、足元の Box の solid を落とすだけで表現する。
// 「立てる床が消える」という結果が欲しいだけなので、これで足りる。
struct Crumble {
    int     boxIndex = -1;
    float   delay    = 0.55f;   // 乗ってから落ち始めるまで（0 なら既定値）
    float   respawn  = 2.50f;   // 落ちてから戻るまで（0 なら既定値）
    Vector3 home{0, 0, 0};      // 作者が置いた位置。戻る先でもある
    Vector3 vel{0, 0, 0};
    float   timer = 0.0f;
    int     state = 0;          // 0=待機 1=揺れている 2=落下（復活待ち）
    float   shake = 0.0f;
};

// ── Gimmick: 積み木
//
// ここだけ「物理の結果が遊びに関わる」ことを許している（DESIGN.md 11.5 の例外）。
// 崩したあとの山を足場にして登る、という遊びのため。
//
// 実体は Box として level.boxes に入れてある。プレイヤーの当たり判定は自前 AABB の
// ままで、位置だけを Jolt から書き戻す。こうすると「乗れる」と「転がる」が両立する。
//
// 飛んでいる最中は solid を落とす。回転した箱に AABB で乗ると噛み合わないし、
// 飛んでいる箱に乗れてしまうのも変なので、静まってから足場になる、と決めている。
struct Crate {
    int        boxIndex = -1;
    float      size     = 0.55f;      // 立方体の半径。回転しても形が変わらないので扱いやすい
    Vector3    home{0, 0, 0};         // 置いた位置。落ちすぎたらここへ戻す
    Quaternion rot{0, 0, 0, 1};       // 見た目だけ（当たり判定は軸に沿ったまま）
    bool       settled  = true;       // 静止した＝乗れる
    float      flash    = 0.0f;
};

// ── Gimmick: ボタン（叩くとゲートが開く）
struct Button {
    Vector3 pos, half;
    int     gateBoxIndex = -1;
    Vector3 gateClosed{0, 0, 0};
    Vector3 gateOpen{0, 0, 0};
    float   openTimer = 0.0f;
    float   openRatio = 0.0f;   // 0=閉 1=開
    float   press     = 0.0f;   // 見た目の沈み
};

// ── Gimmick: ウォータースライダー
//
// 中心線（折れ線）＋半径だけを持つ。管の当たり判定は作らず、乗っている間は
// プレイヤーの位置を中心線から計算して置く（レールに沿わせる方式）。
struct WaterSlide {
    // ctrl が「作者が置いた形」で、pts はそこから毎回作り直せる派生データ。
    // エディタと level.txt が触るのは ctrl だけにして、保存と編集の対象を1つに絞る。
    std::vector<Vector3> ctrl;         // 制御点（編集・保存の対象）
    int                  sub = 8;      // 制御点1区間あたりの分割数
    std::vector<Vector3> pts;          // Catmull-Rom で細分化済みの中心線（派生）
    std::vector<float>   cum;          // 始点からの累積距離（派生）
    float length     = 0.0f;
    float radius     = 1.6f;
    float exitBoost  = 1.0f;           // 0 なら params.slideExitBoost を使う
};

// ── Gimmick: ワイヤーを引っ掛ける points
//
// 種類が2つあるのが肝。見た目は同じ輪だが、
//   Fixed … 固定されている  → プレイヤーが飛んでいく（振り子）
//   Heavy … 物に付いている  → 物の方が飛んでくる（＝驚き）
// どちらになるかを決めるのは interaction.cpp。ワイヤーを撃つ側は知らない。
enum AnchorKind { ANCHOR_FIXED = 0, ANCHOR_HEAVY = 1 };

struct WireAnchor {
    Vector3 pos;
    int     kind     = ANCHOR_FIXED;
    int     boxIndex = -1;        // Heavy のとき、引っ張られる箱
    Vector3 vel{0, 0, 0};
    bool    pulled   = false;     // 引き抜かれた後
    float   bob      = 0.0f;
    float   flash    = 0.0f;
};

// ── Enemy: 直線を巡回するだけ。踏めば倒せる、横から触ると弾かれる。
struct Enemy {
    Vector3 pos, half;
    Vector3 a, b;
    float   speed   = 0.0f;
    float   t       = 0.0f;
    int     dir     = 1;
    bool    alive   = true;
    float   respawn = 0.0f;
    float   flash   = 0.0f;
    float   bob     = 0.0f;
};

// ── 能力アイテム（1ステージ1能力の受け渡し口）
enum class AbilityType {
    None = 0,
    Dash,      // 実装済み
    Wire,      // 実装済み（ワイヤーアクション）
    Rocket,    // 未実装（枠だけ）
    Mouse,     // 未実装
    Sponge,    // 未実装
    Monkey,    // 未実装
};

struct AbilityPickup {
    Vector3     pos;
    AbilityType type = AbilityType::None;
    bool        taken = false;
    float       bob   = 0.0f;
};

// ── 破片（world 側の物理）
//
// 壊れた物の破片。ここだけ Jolt Physics に任せている。
// 「当たり判定を持つが、遊びの成否には関わらない」ものだけを置く場所で、
// 主人公はここに入らない（理由は DESIGN.md 11.5 章）。
//
// この struct には Jolt の型を持ち込まない。bodyId だけを持ち、実体は
// world_physics.cpp の中にいる。game.h を include するだけで Jolt の
// ヘッダまで引きずり込まれると、ビルド時間も依存の向きも壊れるため。
struct Debris {
    Vector3    pos{0, 0, 0};
    Vector3    half{0.2f, 0.2f, 0.2f};
    Quaternion rot{0, 0, 0, 1};
    Color      col{255, 255, 255, 255};
    float      life = 0.0f, maxLife = 0.0f;
    bool       asleep = false;       // Jolt が眠らせた（＝静止した）
    unsigned   bodyId = 0xFFFFFFFFu;
};

struct Level {
    std::vector<Box>            boxes;
    std::vector<MovingPlatform> platforms;
    std::vector<Coin>           coins;
    std::vector<Bot>            bots;
    std::vector<Target>         targets;
    std::vector<Spring>         springs;
    std::vector<Button>         buttons;
    std::vector<Crumble>        crumbles;
    std::vector<Crate>          crates;
    std::vector<WaterSlide>     slides;
    std::vector<WireAnchor>     anchors;
    std::vector<Enemy>          enemies;
    std::vector<AbilityPickup>  pickups;
    std::vector<Vector3>        checkpoints;
    Vector3 goal    = {0, 0, 0};
    bool    cleared = false;
};

// ══════════════════════════════════════════════ Player

// 能力の共通状態。能力ごとに Player へフィールドを増やし続けないための置き場。
// 意味は能力ごとに読み替える（Dash なら value=残り燃料, timer=経過, phase=未使用）。
struct AbilityState {
    AbilityType type   = AbilityType::None;
    float       timer  = 0.0f;
    float       value  = 0.0f;
    int         phase  = 0;
    bool        active = false;
    float       cooldown = 0.0f;
};

// スライダーに運ばれている間の状態。能力と同じ考え方で、Player へ個別の
// フィールドを散らさずに1つの構造体へまとめる。
struct SlideState {
    int   index    = -1;      // 乗っているスライダー
    float dist     = 0.0f;    // 中心線に沿った距離
    float speed    = 0.0f;
    float side     = 0.0f;    // 中心からの横のずれ
    float cooldown = 0.0f;    // 降りた直後の再乗車防止
    bool  active   = false;
};

struct Player {
    Vector3 pos  = {0, 1, 0};
    Vector3 vel  = {0, 0, 0};
    Vector3 half = {0.38f, 0.50f, 0.38f};
    float   yaw  = 0.0f;

    bool  grounded = false, wasGrounded = false;
    float coyote = 0.0f, jumpBuffer = 0.0f, timeSinceJump = 99.0f;
    // 上昇の打ち切り（可変ジャンプ）を適用してよいか。自分のジャンプ中だけ true。
    // バネや踏みつけで得た上向きの速度まで削らないようにするためのフラグ。
    bool  jumpCutArmed = false;

    bool  hovering   = false;
    float hoverFuel  = 1.6f;
    float jetFlicker = 0.0f;
    float jetPuffAccum = 0.0f;   // ジェット粒を「毎秒 N 個」で出すための端数

    float punchTimer = 0.0f, punchCooldown = 0.0f;
    bool  punchHitDone = false;

    bool    laserOn = false;
    float   laserTimer = 0.0f;
    Vector3 laserEnd = {0, 0, 0};

    AbilityState ability;
    SlideState   slide;

    float invuln = 0.0f;

    float squashY = 1.0f, squashXZ = 1.0f;

    int   ridingPlatform = -1;
    int   checkpoint = 0;
    float respawnFlash = 0.0f;

    // 移動計算の途中結果。攻撃や能力から参照する。
    Vector3 wishDir = {0, 0, 0};
    float   wishMag = 0.0f;
};

// ══════════════════════════════════════════════ Camera

struct Rig {
    float    yaw = 0.0f, pitch = 0.30f;
    Vector3  curTarget = {0, 1, 0};
    Vector3  curPos = {0, 5, -8};
    float    shake = 0.0f;      // 現在の揺れ量
    float    shakeSeed = 0.0f;
    float    kick = 0.0f;       // 命中時に一瞬だけ寄る量
    Camera3D cam{};
};

// ══════════════════════════════════════════════ Input

struct Input {
    Vector2 move{0, 0};    // x=右, y=前
    Vector2 look{0, 0};    // x=ヨー, y=ピッチ
    bool jumpPressed = false, jumpHeld = false;
    bool punchPressed = false, punchHeld = false;
    bool abilityPressed = false, abilityHeld = false;
    bool usingPad = false;
};

// ══════════════════════════════════════════════ Interaction
//
// 「誰が誰に何をしたか」だけを表す。攻撃側は対象の種類を知らない。
// 対象がどう反応するか（Reaction）は interaction.cpp 側で決める。
enum class InteractionType {
    Punch,
    Wire,
    Laser,
    Touch,
    Stomp,
    AbilityHit,
};

struct Interaction {
    InteractionType type;
    int             sourceId = 0;
    int             targetId = 0;
    Vector3         position{0, 0, 0};
    Vector3         direction{0, 0, 0};
    float           strength = 0.0f;
};

struct InteractionQueue {
    std::vector<Interaction> items;
};

// 対象側の反応の種類。ログとデバッグ表示のために名前を持たせている。
enum class ReactionKind { None, Break, Bounce, Damage, Activate, Rescue, Ride, Pull, Push };

// ══════════════════════════════════════════════ Event
//
// 「結果として何が起きたか」。Stats と Presentation がこれを読む。
// Gameplay 側は音・パーティクル・カメラを直接触らない。
enum class GameEventType {
    TargetHit,
    TargetBroken,
    CoinTaken,
    BotSaved,
    SpringBounced,
    SlideEntered,
    SlideExited,
    WireAttached,
    WireReleased,
    SurpriseRevealed,
    ButtonActivated,
    EnemyDefeated,
    PlayerDamaged,
    PlayerLanded,
    AbilityGained,
    StageCleared,
    DebrisImpact,      // 破片が強くぶつかった（value = 衝突速度 ×10）
    CrumbleBroke,
    CratePushed,       // 積み木を突き飛ばした（value = 強さ×10）      // 崩れる床が抜けた
};

struct GameEvent {
    GameEventType type;
    Vector3       position{0, 0, 0};
    int           sourceId = 0;
    int           value    = 0;
};

struct GameEvents {
    std::vector<GameEvent> items;
};

// ══════════════════════════════════════════════ Presentation

struct Particle {
    Vector3 pos, vel;
    float   life, maxLife, size;
    Color   col;
    bool    gravity;
    bool    streak;      // true なら速度方向に伸ばして描く（火花っぽくなる）
};

// 短命の見た目だけの要素。パーティクルとは別に、形のあるエフェクトをここで持つ。
// Gameplay からは触らない。Event を受けた presentation.cpp だけが積む。
enum class EffectKind {
    Ring,        // 平たい輪が広がる（着弾・破壊）
    Shockwave,   // 太い輪が地面を走る
    Flash,       // 一瞬の球＋放射状のトゲ
};

struct Effect {
    EffectKind kind;
    Vector3    pos;
    Vector3    dir;        // 輪の法線 / 斬撃の向き
    float      life, maxLife;
    float      size0, size1;
    float      angle;      // 位相（トゲの向きなど）
    Color      col;
};

// ══════════════════════════════════════════════ Game

struct GameStats {
    int coinsTaken      = 0;
    int botsSaved       = 0;
    int targetsBroken   = 0;
    int enemiesDefeated = 0;
    int damageTaken     = 0;
    int springBounces   = 0;
    int buttonHits      = 0;
    int slideRides      = 0;
    int wireGrabs       = 0;
    int surprises       = 0;
};

struct GameDebug {
    bool showDebug     = true;
    bool showParams    = false;
    bool showCollision = false;
    int  paramCursor   = 0;
    int  lastReaction  = 0;   // 直近の Reaction（デバッグ表示用）
};

// ══════════════════════════════════════════════ Editor
//
// レベルエディタ（F4）の状態。ゲームプレイ側の struct には一切足さない。
// エディタが消えても Level / Player は何も変わらない、という関係を保つ。

// 編集できる物の種類。Level が持つ vector と1対1に対応させる。
// 種類を増やすときは、この enum と editor.cpp の kEdTypeName / EdCount /
// EdRef / level_io.cpp の読み書きの4箇所を足す。
enum EditType {
    ED_BOX = 0,
    ED_PLATFORM,
    ED_COIN,
    ED_BOT,
    ED_TARGET,
    ED_SPRING,
    ED_BUTTON,
    ED_SLIDE,
    ED_ANCHOR,
    ED_ENEMY,
    ED_PICKUP,
    ED_CHECKPOINT,
    ED_GOAL,
    ED_CRUMBLE,
    ED_CRATE,
    ED_TYPE_COUNT,
};

struct EditorState {
    bool  on       = false;   // 編集モード中か（true の間はゲームを止める）
    bool  dirty    = false;   // 地形を触ったか（抜けるときに物理世界を作り直す）
    int   type     = ED_BOX;  // 選択中／これから置く種類
    int   index    = -1;      // 選択中の添字（-1 = 未選択）
    int   part     = 0;       // 0=全体, 移動床/敵なら 1=A 2=B, スライダーなら 1..n=制御点
    int   field    = 0;       // 種類ごとの数値フィールドの選択位置（- / = で増減）
    int   boxKind  = BOX_FLOOR;   // 新しく置く Box の種類
    int   pickupKind = 1;         // 新しく置く能力アイテムの種類（AbilityType の値）

    float grid     = 0.5f;    // スナップの刻み
    bool  snap     = true;
    bool  showHelp = true;

    // フリーカメラ（軌道式）。ゲームの Rig とは別に持つ。
    Vector3 camTarget = {0, 2, 0};
    float   camYaw    = 0.0f;
    float   camPitch  = 0.55f;
    float   camDist   = 18.0f;

    const char* path = nullptr;   // 読み書きに使った level.txt のパス
};

struct Game {
    Params p;

    Level  level;
    Player player;
    Rig    rig;
    Input  in;

    InteractionQueue interactions;
    GameEvents       events;

    std::vector<Particle> particles;
    std::vector<Effect>   effects;
    std::vector<Debris>   debris;      // 実体は world_physics.cpp（Jolt）側

    GameStats   stats;
    GameDebug   debug;
    EditorState editor;

    float time        = 0.0f;
    float hitStop     = 0.0f;
    float clearBanner = 0.0f;
    float toastTimer  = 0.0f;
    char  toast[128]  = {0};

    const char* paramsPath = nullptr;
};

// ══════════════════════════════════════════════ 関数（すべて free function）

// ── game.cpp
extern Game gGame;
void  GatherInput(Game& g);
float ApplyHitStop(Game& g, float frameDt);
void  FrameStep(Game& g, float frameDt);
void  FrameStepCallback(void);          // Emscripten のコールバック用
void  DrawGame(Game& g);

// ── level.cpp
void  BuildLevel(Game& g);
void  UpdateLevel(Game& g, float dt);
void  DrawLevel(Game& g);
bool  BoxOverlap(Vector3 ca, Vector3 ha, Vector3 cb, Vector3 hb);
bool  AnyOverlapSolid(const Level& l, Vector3 c, Vector3 h);
float RaycastDown(const Level& l, Vector3 from, float maxDist, int* hitTargetOut);
// 制御点(ctrl)から中心線(pts/cum/length)を作り直す。エディタと level_io から呼ぶ。
void  RebuildSlide(WaterSlide& s);
// スライダーの中心線サンプリング（player.cpp から使う）
Vector3 SlidePoint(const WaterSlide& s, float dist);
Vector3 SlideTangent(const WaterSlide& s, float dist);
// 中心線に一番近い距離を返す（乗り込む位置を決めるため）
float   SlideNearestDist(const WaterSlide& s, Vector3 from, float* outDistToLine);

// ── player.cpp
void UpdatePlayer(Game& g, float dt);
void DrawPlayer(Game& g);
void RespawnPlayer(Game& g);

// ── ability.cpp
void        UpdatePlayerAbility(Game& g, float dt);
void        GrantAbility(Game& g, AbilityType type);
const char* AbilityName(AbilityType type);
void        DrawAbilityFx(Game& g);
float       AbilityFuelRatio(const Game& g);

// ── interaction.cpp
void PushInteraction(Game& g, const Interaction& it);
void ResolveInteractions(Game& g);
void PushEvent(Game& g, GameEventType type, Vector3 pos, int sourceId, int value);
void DispatchGameEvents(Game& g);
const char* ReactionName(ReactionKind r);

// ── presentation.cpp
void SpawnBurst(Game& g, Vector3 pos, int n, Color col, float power, float size);
void SpawnSparks(Game& g, Vector3 pos, Vector3 dir, int n, Color col, float power, float size);
void SpawnEffect(Game& g, EffectKind kind, Vector3 pos, Vector3 dir, float life,
                 float size0, float size1, Color col);
void UpdateEffects(Game& g, float dt);
void DrawEffects(Game& g);
void AddCameraKick(Game& g, float amount);
void SpawnJetPuff(Game& g, Vector3 pos);
void UpdateParticles(Game& g, float dt);
void DrawParticles(Game& g);
void AddCameraShake(Game& g, float amp);
void ApplyEventToPresentation(Game& g, const GameEvent& e);
void Toast(Game& g, const char* msg);

// ── world_physics.cpp（Jolt Physics を使うのはこのファイルだけ）
void ResetWorldPhysics(Game& g);                  // BuildLevel の最後に呼ぶ
// 積み木を突き飛ばす。遊び側から物理へ出ていく口はここだけ（interaction.cpp が呼ぶ）
void PushCrate(Game& g, int crateIndex, Vector3 dir, float power);
void UpdateWorldPhysics(Game& g, float dt);       // FrameStep の中で固定 dt で回す
void DrawWorldPhysics(Game& g);
void SpawnDebrisBurst(Game& g, Vector3 pos, Vector3 dir, int n, float power, Color col);
void ApplyEventToWorldPhysics(Game& g, const GameEvent& e);
int  DebrisCount(const Game& g);
bool AllDebrisAsleep(const Game& g);

// ── camera_rig.cpp
void UpdateRigInput(Game& g, float dt);
void UpdateRigFollow(Game& g, float dt);

// ── debug.cpp
void UpdateDebugKeys(Game& g, float rdt);
void DrawHUD(Game& g);
void DrawDebugPanel(Game& g);
void DrawParamEditor(Game& g);
void SaveParams(const Game& g);
void DumpParams(const Game& g);

// ── level_io.cpp（level.txt の読み書き。エディタが無くても単体で使える）
bool LoadLevel(Game& g, const char* path);   // path=nullptr なら既定の候補を探す
bool SaveLevel(Game& g, const char* path);   // path=nullptr なら読んだパス / level.txt
void DumpLevel(const Game& g);               // 標準出力へ（ブラウザでのコピペ用）
bool ReloadLevelIfChanged(Game& g);          // 外で level.txt が更新されていたら読み直す

// ── editor.cpp
void ToggleEditor(Game& g);
void UpdateEditor(Game& g, float rdt);       // 編集モード中は毎フレームこれだけ
void DrawEditor3D(Game& g);                  // BeginMode3D の中
void DrawEditorUI(Game& g);                  // 2D オーバーレイ

// ── tests.cpp / tour.cpp
int RunSelfTest(Game& g);
int RunTour(Game& g);
