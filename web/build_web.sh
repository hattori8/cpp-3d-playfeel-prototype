#!/usr/bin/env bash
# ブラウザ版（WebAssembly）をビルドする。
#
# 必要なもの: emsdk（https://emscripten.org/docs/getting_started/downloads.html）
#   git clone https://github.com/emscripten-core/emsdk.git
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh
#
# 実行すると build-web/astro_proto.html が 1 ファイルだけ出ます。
# wasm を中に埋め込んでいるので、そのままブラウザにドラッグ&ドロップで動きます。
#
# リポジトリ直下に level.txt があれば wasm に同梱されて既定レベルになります
# （エディタの F4 → F6 で書き出したものを置くだけ）。
# 第1引数に --publish を付けると docs/index.html まで更新します（GitHub Pages 用）。
set -e
cd "$(dirname "$0")/.."

emcmake cmake -S . -B build-web -DPLATFORM=Web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j"$(nproc 2>/dev/null || echo 4)"

echo
echo "できました: build-web/astro_proto.html"
echo "ブラウザで開くだけで動きます（ローカルサーバは不要）。"

if [ "${1:-}" = "--publish" ]; then
    cp build-web/astro_proto.html docs/index.html
    # レベルエディタのページも一緒に公開する（デモからリンクしている先）。
    # 実体は tools/level_editor.html のほう。docs/ 側はその複製。
    cp tools/level_editor.html docs/editor.html
    # ゲームが起動時に読みに行くので、これも一緒に置く。
    # この2つだけを差し替えて push すれば、リビルドなしで公開版が変わる。
    cp level.txt params.txt docs/
    echo "docs/index.html と docs/editor.html を更新しました。commit & push すると公開版に反映されます。"
else
    echo "公開版へ反映するには: web/build_web.sh --publish"
fi
