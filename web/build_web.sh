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
set -e
cd "$(dirname "$0")/.."

emcmake cmake -S . -B build-web -DPLATFORM=Web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j"$(nproc 2>/dev/null || echo 4)"

echo
echo "できました: build-web/astro_proto.html"
echo "ブラウザで開くだけで動きます（ローカルサーバは不要）。"
