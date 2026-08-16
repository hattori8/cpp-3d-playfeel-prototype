#include "params.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

static const char* kCandidates[] = {
    "params.txt", "../params.txt", "../../params.txt", "../../../params.txt",
};

static char      s_path[512] = {0};
static long long s_mtime     = 0;

static long long FileMTime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long long)st.st_mtime;
}

void ClampParams(Params& p) {
    for (int i = 0; i < kParamCount; ++i) {
        float* v = ParamPtr(p, i);
        if (*v < kParamTable[i].lo) *v = kParamTable[i].lo;
        if (*v > kParamTable[i].hi) *v = kParamTable[i].hi;
    }
}

static bool ApplyLine(Params& p, char* line) {
    char* hash = strchr(line, '#');
    if (hash) *hash = '\0';

    char  name[128];
    float value = 0.0f;
    for (char* c = line; *c; ++c)
        if (*c == '=' || *c == ',' || *c == ':' || *c == '\t') *c = ' ';
    if (sscanf(line, "%127s %f", name, &value) != 2) return false;

    for (int i = 0; i < kParamCount; ++i) {
        if (strcmp(kParamTable[i].name, name) != 0) continue;
        if (value < kParamTable[i].lo || value > kParamTable[i].hi) {
            fprintf(stderr, "[params] %s = %g は範囲 [%g, %g] 外なので丸めました\n",
                    name, value, kParamTable[i].lo, kParamTable[i].hi);
        }
        *ParamPtr(p, i) = value;
        return true;
    }
    fprintf(stderr, "[params] unknown key: %s\n", name);
    return false;
}

bool LoadParams(Params& p, const char** usedPathOut) {
    const char* found = nullptr;
    if (s_path[0]) {
        found = s_path;
    } else {
        for (const char* cand : kCandidates) {
            FILE* f = fopen(cand, "rb");
            if (f) { fclose(f); found = cand; break; }
        }
    }
    if (!found) { if (usedPathOut) *usedPathOut = nullptr; return false; }

    FILE* f = fopen(found, "rb");
    if (!f) { if (usedPathOut) *usedPathOut = nullptr; return false; }

    snprintf(s_path, sizeof(s_path), "%s", found);
    s_mtime = FileMTime(s_path);

    char line[512];
    int  applied = 0;
    while (fgets(line, sizeof(line), f)) {
        if (ApplyLine(p, line)) applied++;
    }
    fclose(f);
    ClampParams(p);

    if (usedPathOut) *usedPathOut = s_path;
    printf("[params] loaded %d values from %s\n", applied, s_path);
    return true;
}

bool ReloadParamsIfChanged(Params& p) {
    if (!s_path[0]) return false;
    long long t = FileMTime(s_path);
    if (t == 0 || t == s_mtime) return false;
    return LoadParams(p, nullptr);
}
