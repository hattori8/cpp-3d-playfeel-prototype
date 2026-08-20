// level_io.cpp ─ level.txt の読み書き
//
// 形式は params.txt と同じ思想にした。
//   ・1行1レコード、先頭のトークンが種類
//   ・'#' 以降はコメント
//   ・人間が直接書き換えても壊れない（順序は自由、知らない行は警告して読み飛ばす）
//
// 索引について：
//   移動床 / ゲート / 引っ張れる岩 は「box の何番目か」を指す。box 行を書いた順が
//   そのまま添字になるので、読み込み側は box 行の出現順だけ守れば復元できる。
//   派生データ（スライダーの中心線など）はここでは書かず、読み込み後に作り直す。
#include "game.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

#if defined(PLATFORM_WEB)
  #include <emscripten.h>

// ブラウザにはユーザーのディスクが無い。保存＝ダウンロード、読み込み＝ファイル選択に
// 置き換える。MEMFS を経由するので、C 側のコードはデスクトップと同じままで済む。
//
// EM_JS の中では「括弧の外のカンマ」がプリプロセッサに引数の区切りと誤解されるので、
// var 宣言は1つずつに分けて書いている。
EM_JS(void, AstroDownloadFile, (const char* pathPtr, const char* namePtr), {
    var path = UTF8ToString(pathPtr);
    var name = UTF8ToString(namePtr);
    try {
        var data = FS.readFile(path);
        var blob = new Blob([data], { type: 'text/plain;charset=utf-8' });
        var url  = URL.createObjectURL(blob);
        var a    = document.createElement('a');
        a.href = url;
        a.download = name;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setTimeout(function () { URL.revokeObjectURL(url); }, 2000);
    } catch (err) {
        console.error('[level] download failed', err);
    }
});
#endif

// 探す順番。docs/ が原本で、公開版（GitHub Pages）がそのまま配信するのもこれ。
// 直下の level.txt は、以前の置き場所を使っている場合のための後方互換。
static const char* kCandidates[] = {
    "docs/level.txt", "../docs/level.txt", "../../docs/level.txt", "../../../docs/level.txt",
    "level.txt",      "../level.txt",      "../../level.txt",      "../../../level.txt",
};

static char      s_path[512] = {0};
static long long s_mtime     = 0;    // 自動読み直しの判定に使う（params.cpp と同じ作り）

static long long FileMTime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long long)st.st_mtime;
}

// params.txt が見つかっているなら、その隣を level.txt の置き場にする。
// 実行ディレクトリがビルドフォルダでも、ソースツリー側へ保存できるようにするため。
// どちらも分からないときは docs/level.txt（原本の置き場）に落とす。
static void DeriveDefaultPath(const Game& g, char* out, size_t n) {
    if (g.paramsPath && g.paramsPath[0]) {
        snprintf(out, n, "%s", g.paramsPath);
        char* slash = strrchr(out, '/');
        char* bslash = strrchr(out, '\\');
        char* cut = (bslash && (!slash || bslash > slash)) ? bslash : slash;
        if (cut) { cut[1] = '\0'; strncat(out, "level.txt", n - strlen(out) - 1); return; }
    }
    snprintf(out, n, "docs/level.txt");
}

// ────────────────────────────────────────────── 書き出し

static void WriteLevel(const Level& l, FILE* f) {
    fprintf(f, "# astro_proto level  ─ ゲーム内エディタ（F4）の F6 で保存 / F5 で読み直し\n");
    fprintf(f, "# 手で書き換えても構いません。'#' 以降はコメント、行の順番は自由です。\n");
    fprintf(f, "version 1\n\n");

    // ── box（この順番がそのまま添字になる。platform / button / anchor が参照する）
    fprintf(f, "# box  cx cy cz  hx hy hz  kind"
               "   (0=床 1=段差 2=壁 3=動く床 4=サブパス 5=ゲート 6=崩れる床 9=遠景)\n");
    for (int i = 0; i < (int)l.boxes.size(); ++i) {
        const Box& b = l.boxes[i];
        Vector3 c = b.c;
        // 動いている物は「作者が置いた位置」に戻して書く（遊んだ後に保存しても形が崩れない）
        for (const MovingPlatform& pf : l.platforms)
            if (pf.boxIndex == i) c = pf.a;
        for (const Button& bt : l.buttons)
            if (bt.gateBoxIndex == i) c = bt.gateClosed;
        for (const Crumble& cr : l.crumbles)
            if (cr.boxIndex == i) c = cr.home;
        fprintf(f, "box  %8.3f %8.3f %8.3f   %7.3f %7.3f %7.3f   %d   # [%d]\n",
                c.x, c.y, c.z, b.h.x, b.h.y, b.h.z, b.kind, i);
    }

    fprintf(f, "\n# platform  boxIndex  ax ay az  bx by bz  speed\n");
    for (const MovingPlatform& pf : l.platforms) {
        fprintf(f, "platform  %d   %8.3f %8.3f %8.3f   %8.3f %8.3f %8.3f   %6.3f\n",
                pf.boxIndex, pf.a.x, pf.a.y, pf.a.z, pf.b.x, pf.b.y, pf.b.z, pf.speed);
    }

    fprintf(f, "\n# coin  x y z\n");
    for (const Coin& c : l.coins) fprintf(f, "coin  %8.3f %8.3f %8.3f\n", c.pos.x, c.pos.y, c.pos.z);

    fprintf(f, "\n# bot  x y z\n");
    for (const Bot& b : l.bots) fprintf(f, "bot  %8.3f %8.3f %8.3f\n", b.pos.x, b.pos.y, b.pos.z);

    fprintf(f, "\n# target  x y z  hx hy hz  floating  hp\n");
    for (const Target& t : l.targets) {
        fprintf(f, "target  %8.3f %8.3f %8.3f   %6.3f %6.3f %6.3f   %d  %d\n",
                t.home.x, t.home.y, t.home.z, t.half.x, t.half.y, t.half.z,
                t.floating ? 1 : 0, t.hp);
    }

    fprintf(f, "\n# spring  x y z  power   (power=0 なら params.springPower)\n");
    for (const Spring& s : l.springs)
        fprintf(f, "spring  %8.3f %8.3f %8.3f   %6.3f\n", s.pos.x, s.pos.y, s.pos.z, s.power);

    fprintf(f, "\n# button  px py pz  gateBoxIndex  closed(x y z)  open(x y z)\n");
    for (const Button& b : l.buttons) {
        fprintf(f, "button  %8.3f %8.3f %8.3f   %d   %8.3f %8.3f %8.3f   %8.3f %8.3f %8.3f\n",
                b.pos.x, b.pos.y, b.pos.z, b.gateBoxIndex,
                b.gateClosed.x, b.gateClosed.y, b.gateClosed.z,
                b.gateOpen.x, b.gateOpen.y, b.gateOpen.z);
    }

    fprintf(f, "\n# crumble  boxIndex  delay  respawn   (乗ってから落ちるまで / 戻るまで。0 は既定値)\n");
    for (const Crumble& c : l.crumbles)
        fprintf(f, "crumble  %d   %6.3f  %6.3f\n", c.boxIndex, c.delay, c.respawn);

    fprintf(f, "\n# slide  radius exitBoost sub  n   x y z  x y z ...   (制御点だけ。中心線は読込時に生成)\n");
    for (const WaterSlide& s : l.slides) {
        fprintf(f, "slide  %6.3f %6.3f %d  %d ", s.radius, s.exitBoost, s.sub, (int)s.ctrl.size());
        for (const Vector3& p : s.ctrl) fprintf(f, "  %8.3f %8.3f %8.3f", p.x, p.y, p.z);
        fprintf(f, "\n");
    }

    fprintf(f, "\n# anchor  x y z  kind(0=固定 1=引っ張れる)  boxIndex\n");
    for (const WireAnchor& a : l.anchors)
        fprintf(f, "anchor  %8.3f %8.3f %8.3f   %d  %d\n", a.pos.x, a.pos.y, a.pos.z,
                a.kind, a.boxIndex);

    fprintf(f, "\n# enemy  ax ay az  bx by bz  speed   (speed=0 なら params.enemySpeed)\n");
    for (const Enemy& e : l.enemies)
        fprintf(f, "enemy  %8.3f %8.3f %8.3f   %8.3f %8.3f %8.3f   %6.3f\n",
                e.a.x, e.a.y, e.a.z, e.b.x, e.b.y, e.b.z, e.speed);

    fprintf(f, "\n# pickup  x y z  type  (1=Dash 2=Wire 3=Rocket 4=Mouse 5=Sponge 6=Monkey)\n");
    for (const AbilityPickup& a : l.pickups)
        fprintf(f, "pickup  %8.3f %8.3f %8.3f   %d\n", a.pos.x, a.pos.y, a.pos.z, (int)a.type);

    fprintf(f, "\n# checkpoint  x y z   (1つ目がスタート地点)\n");
    for (const Vector3& c : l.checkpoints)
        fprintf(f, "checkpoint  %8.3f %8.3f %8.3f\n", c.x, c.y, c.z);

    fprintf(f, "\ngoal  %8.3f %8.3f %8.3f\n", l.goal.x, l.goal.y, l.goal.z);
}

bool SaveLevel(Game& g, const char* path) {
    char target[512];
    if (path && path[0]) {
        snprintf(target, sizeof(target), "%s", path);
    } else if (g.editor.path && g.editor.path[0]) {
        snprintf(target, sizeof(target), "%s", g.editor.path);
    } else if (s_path[0]) {
        snprintf(target, sizeof(target), "%s", s_path);
    } else {
        DeriveDefaultPath(g, target, sizeof(target));
    }

    FILE* f = fopen(target, "wb");
    if (!f) {
        fprintf(stderr, "[level] 保存できません: %s\n", target);
        return false;
    }
    WriteLevel(g.level, f);
    fclose(f);

    snprintf(s_path, sizeof(s_path), "%s", target);
    s_mtime = FileMTime(s_path);      // 自分で書いた分で読み直しが走らないように
    g.editor.path = s_path;
    printf("[level] saved to %s\n", s_path);
    fflush(stdout);

#if defined(PLATFORM_WEB)
    // 書いた先は MEMFS（ページを閉じると消える）なので、そのままダウンロードへ流す。
    // 手元に降りてきた level.txt をリポジトリに置けば、公開版にも反映できる。
    AstroDownloadFile(s_path, "level.txt");
#endif
    return true;
}

void DumpLevel(const Game& g) {
    printf("---- level.txt ----\n");
    WriteLevel(g.level, stdout);
    printf("-------------------\n");
    fflush(stdout);
}

// ────────────────────────────────────────────── 読み込み

// 空白区切りの float をまとめて読む小さなヘルパー。sscanf を種類ごとに書き分けると
// 引数の数と順番を毎回間違えるので、「先頭トークンの後ろの数値列」として扱う。
static int ScanFloats(const char* s, float* out, int maxN) {
    int n = 0;
    const char* p = s;
    while (n < maxN) {
        char* end = nullptr;
        float v = strtof(p, &end);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

static bool ApplyLevelLine(Level& l, char* line, int lineNo) {
    char* hash = strchr(line, '#');
    if (hash) *hash = '\0';

    char key[64];
    int  keyLen = 0;
    const char* p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && keyLen < 63) key[keyLen++] = *p++;
    key[keyLen] = '\0';
    if (keyLen == 0) return true;      // 空行

    float v[256];
    int   n = ScanFloats(p, v, 256);

    auto need = [&](int want) -> bool {
        if (n < want) {
            fprintf(stderr, "[level] %d行目 '%s' は数値が %d 個必要です（%d 個でした）\n",
                    lineNo, key, want, n);
            return false;
        }
        return true;
    };

    if (strcmp(key, "version") == 0) {
        return true;
    }
    if (strcmp(key, "box") == 0) {
        if (!need(7)) return false;
        l.boxes.push_back(Box{{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, (int)v[6], true});
        return true;
    }
    if (strcmp(key, "platform") == 0) {
        if (!need(8)) return false;
        MovingPlatform pf{};
        pf.boxIndex = (int)v[0];
        pf.a = Vector3{v[1], v[2], v[3]};
        pf.b = Vector3{v[4], v[5], v[6]};
        pf.speed = v[7];
        pf.t = 0.0f; pf.dir = 1; pf.delta = Vector3{0, 0, 0};
        l.platforms.push_back(pf);
        return true;
    }
    if (strcmp(key, "coin") == 0) {
        if (!need(3)) return false;
        Coin c; c.pos = Vector3{v[0], v[1], v[2]};
        l.coins.push_back(c);
        return true;
    }
    if (strcmp(key, "bot") == 0) {
        if (!need(3)) return false;
        l.bots.push_back(Bot{{v[0], v[1], v[2]}});
        return true;
    }
    if (strcmp(key, "target") == 0) {
        if (!need(3)) return false;
        Target t;
        t.pos = t.home = Vector3{v[0], v[1], v[2]};
        t.vel  = Vector3{0, 0, 0};
        t.half = (n >= 6) ? Vector3{v[3], v[4], v[5]} : Vector3{0.5f, 0.5f, 0.5f};
        t.floating = (n >= 7) ? (v[6] != 0.0f) : false;
        t.hp = (n >= 8) ? (int)v[7] : (t.floating ? 2 : 1);
        l.targets.push_back(t);
        return true;
    }
    if (strcmp(key, "spring") == 0) {
        if (!need(3)) return false;
        Spring s;
        s.pos  = Vector3{v[0], v[1], v[2]};
        s.half = Vector3{0.9f, 0.25f, 0.9f};
        s.power = (n >= 4) ? v[3] : 0.0f;
        l.springs.push_back(s);
        return true;
    }
    if (strcmp(key, "button") == 0) {
        if (!need(10)) return false;
        Button b;
        b.pos  = Vector3{v[0], v[1], v[2]};
        b.half = Vector3{0.75f, 0.15f, 0.75f};
        b.gateBoxIndex = (int)v[3];
        b.gateClosed = Vector3{v[4], v[5], v[6]};
        b.gateOpen   = Vector3{v[7], v[8], v[9]};
        l.buttons.push_back(b);
        return true;
    }
    if (strcmp(key, "crumble") == 0) {
        if (!need(1)) return false;
        Crumble c;
        c.boxIndex = (int)v[0];
        c.delay    = (n >= 2) ? v[1] : 0.55f;
        c.respawn  = (n >= 3) ? v[2] : 2.50f;
        l.crumbles.push_back(c);
        return true;
    }
    if (strcmp(key, "slide") == 0) {
        if (!need(4)) return false;
        WaterSlide s;
        s.radius    = v[0];
        s.exitBoost = v[1];
        s.sub       = (int)v[2];
        int cnt = (int)v[3];
        if (cnt < 2 || 4 + cnt * 3 > n) {
            fprintf(stderr, "[level] %d行目 slide の制御点数が合いません\n", lineNo);
            return false;
        }
        for (int i = 0; i < cnt; ++i)
            s.ctrl.push_back(Vector3{v[4 + i * 3], v[5 + i * 3], v[6 + i * 3]});
        RebuildSlide(s);
        l.slides.push_back(s);
        return true;
    }
    if (strcmp(key, "anchor") == 0) {
        if (!need(3)) return false;
        WireAnchor a;
        a.pos = Vector3{v[0], v[1], v[2]};
        a.kind     = (n >= 4) ? (int)v[3] : ANCHOR_FIXED;
        a.boxIndex = (n >= 5) ? (int)v[4] : -1;
        l.anchors.push_back(a);
        return true;
    }
    if (strcmp(key, "enemy") == 0) {
        if (!need(6)) return false;
        Enemy e;
        e.a = Vector3{v[0], v[1], v[2]};
        e.b = Vector3{v[3], v[4], v[5]};
        e.pos = e.a;
        e.half = Vector3{0.45f, 0.45f, 0.45f};
        e.speed = (n >= 7) ? v[6] : 0.0f;
        e.t = 0.0f; e.dir = 1;
        l.enemies.push_back(e);
        return true;
    }
    if (strcmp(key, "pickup") == 0) {
        if (!need(3)) return false;
        AbilityPickup a;
        a.pos = Vector3{v[0], v[1], v[2]};
        a.type = (AbilityType)((n >= 4) ? (int)v[3] : 0);
        l.pickups.push_back(a);
        return true;
    }
    if (strcmp(key, "checkpoint") == 0) {
        if (!need(3)) return false;
        l.checkpoints.push_back(Vector3{v[0], v[1], v[2]});
        return true;
    }
    if (strcmp(key, "goal") == 0) {
        if (!need(3)) return false;
        l.goal = Vector3{v[0], v[1], v[2]};
        return true;
    }

    fprintf(stderr, "[level] %d行目 unknown key: %s\n", lineNo, key);
    return false;
}

// 索引の後始末。手で書き換えたファイルでも落ちないように、
// 範囲外を指していたら黙って切る（読み込みは失敗させない）。
static void FixupLevel(Level& l) {
    const int nb = (int)l.boxes.size();

    for (MovingPlatform& pf : l.platforms) {
        if (pf.boxIndex < 0 || pf.boxIndex >= nb) { pf.boxIndex = -1; continue; }
        l.boxes[pf.boxIndex].c = pf.a;
        l.boxes[pf.boxIndex].kind = BOX_MOVING;
    }
    // 参照先を失った移動床は消す（更新側が boxIndex の有効性を前提にしているため）
    for (int i = (int)l.platforms.size() - 1; i >= 0; --i)
        if (l.platforms[i].boxIndex < 0) l.platforms.erase(l.platforms.begin() + i);

    for (Button& b : l.buttons) {
        if (b.gateBoxIndex < 0 || b.gateBoxIndex >= nb) { b.gateBoxIndex = -1; continue; }
        l.boxes[b.gateBoxIndex].c = b.gateClosed;
    }
    for (WireAnchor& a : l.anchors)
        if (a.boxIndex < 0 || a.boxIndex >= nb) a.boxIndex = -1;

    for (Crumble& c : l.crumbles) {
        if (c.boxIndex < 0 || c.boxIndex >= nb) { c.boxIndex = -1; continue; }
        c.home = l.boxes[c.boxIndex].c;
        l.boxes[c.boxIndex].kind  = BOX_CRUMBLE;
        l.boxes[c.boxIndex].solid = true;
    }
    // 参照先を失った崩れる床は消す（更新側が boxIndex の有効性を前提にしている）
    for (int i = (int)l.crumbles.size() - 1; i >= 0; --i)
        if (l.crumbles[i].boxIndex < 0) l.crumbles.erase(l.crumbles.begin() + i);

    if (l.checkpoints.empty()) l.checkpoints.push_back(Vector3{0, 1.2f, 0});
}

bool LoadLevel(Game& g, const char* path) {
    const char* found = nullptr;
    if (path && path[0]) {
        found = path;
    } else if (s_path[0]) {
        found = s_path;
    } else {
        for (const char* cand : kCandidates) {
            FILE* f = fopen(cand, "rb");
            if (f) { fclose(f); found = cand; break; }
        }
    }
    if (!found) return false;

    FILE* f = fopen(found, "rb");
    if (!f) return false;

    // 途中で失敗しても現在のレベルを壊さないよう、いったん別の Level に読む
    Level nl{};
    char  line[4096];
    int   lineNo = 0, applied = 0, bad = 0;
    while (fgets(line, sizeof(line), f)) {
        ++lineNo;
        if (ApplyLevelLine(nl, line, lineNo)) ++applied;
        else ++bad;
    }
    fclose(f);

    if (nl.boxes.empty() && nl.coins.empty()) {
        fprintf(stderr, "[level] %s は空に見えるので読み込みを中止しました\n", found);
        return false;
    }
    FixupLevel(nl);

    snprintf(s_path, sizeof(s_path), "%s", found);
    s_mtime = FileMTime(s_path);
    g.editor.path = s_path;

    g.level = nl;

    // BuildLevel の末尾と同じ初期化。レベルが変わったら遊びの状態も作り直す。
    g.player = Player{};
    g.player.pos = g.level.checkpoints[0];
    g.player.hoverFuel = g.p.hoverFuelMax;
    g.stats = GameStats{};
    g.interactions.items.clear();
    g.events.items.clear();
    g.particles.clear();
    g.effects.clear();
    ResetWorldPhysics(g);
    g.time = 0.0f;
    g.hitStop = 0.0f;
    g.clearBanner = 0.0f;

    printf("[level] loaded %d lines (%d bad) from %s\n", applied, bad, s_path);
    fflush(stdout);
    return true;
}

// level.txt が外で書き換わっていたら読み直す。
//
// 狙いは「ブラウザのエディタで保存 → ゲームに切り替えたらもう新しい」という往復。
// params.txt のホットリロード（params.cpp）と同じ仕掛けで、ポーリングは game.cpp 側。
// ブラウザ版はファイルが MEMFS の中にしか無いので何もしない。
bool ReloadLevelIfChanged(Game& g) {
#if defined(PLATFORM_WEB)
    (void)g;
    return false;
#else
    if (!s_path[0]) return false;
    long long t = FileMTime(s_path);
    if (t == 0 || t == s_mtime) return false;
    return LoadLevel(g, s_path);
#endif
}

// ────────────────────────────────────────────── ブラウザ側の入口
//
// shell.html のボタンとドラッグ&ドロップから ccall で呼ぶ。
// ここだけが JS から見える窓口で、他は何も公開しない。
#if defined(PLATFORM_WEB)
extern "C" {

// 受け取ったテキストをそのまま今のレベルとして読み込む
EMSCRIPTEN_KEEPALIVE void AstroLoadLevelText(const char* text) {
    if (!text) return;
    FILE* f = fopen("/level.txt", "wb");
    if (!f) { Toast(gGame, "LEVEL LOAD FAILED"); return; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    if (LoadLevel(gGame, "/level.txt")) {
        gGame.editor.index = -1;
        gGame.editor.dirty = true;
        Toast(gGame, "LEVEL LOADED");
    } else {
        Toast(gGame, "LEVEL LOAD FAILED");
    }
}

// 今のレベルを level.txt としてダウンロードさせる
EMSCRIPTEN_KEEPALIVE void AstroSaveLevelDownload(void) {
    if (SaveLevel(gGame, nullptr)) Toast(gGame, "LEVEL SAVED (download)");
    else                           Toast(gGame, "SAVE FAILED");
}

// 編集モードの ON/OFF（ページのボタンから触れるように）
EMSCRIPTEN_KEEPALIVE void AstroToggleEditor(void) {
    ToggleEditor(gGame);
}

}  // extern "C"
#endif
