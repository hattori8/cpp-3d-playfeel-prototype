# CMakeLists.txt ─ astro_proto のビルド定義
#
# 依存は raylib 5.5 と Jolt Physics 5.6 の2つだけ。どちらも FetchContent で
# 取ってくるので、事前に何かを入れておく必要はない（初回だけ通信する）。
#
#   デスクトップ:  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#                  cmake --build build -j
#                  ./build/astro_proto            ← 実行は「リポジトリ直下」から
#
#   ブラウザ:      web/build_web.sh               ← emsdk を有効にしてから
#
# ブラウザ版は params.txt / level.txt を wasm の中に同梱する（下の EMBED_FILES）。
# level.txt はエディタ（F4 → F6）が書き出したものを直下に置けば、次のビルドから
# 公開版の既定レベルになる。無ければ BuildLevel() のコードのレベルで起動する。
cmake_minimum_required(VERSION 3.16)
project(astro_proto CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(PLATFORM STREQUAL "Web" OR EMSCRIPTEN)
    set(ASTRO_WEB ON)
else()
    set(ASTRO_WEB OFF)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ────────────────────────────────────────────── raylib 5.5
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
if(ASTRO_WEB)
    set(PLATFORM "Web" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(raylib)

# 古い emsdk（3.1.30 未満）の GLFW シムには raylib 5.5 が呼ぶ関数が足りない。
# 宣言を raylib 自身のコンパイルにも押し込む（実体は web/glfw_compat.c のスタブ）。
# 新しい emsdk では glfw_compat.h の中身が空になるので、付けたままで害はない。
if(ASTRO_WEB)
    target_compile_options(raylib PRIVATE -include "${CMAKE_SOURCE_DIR}/web/glfw_compat.h")
endif()

# ────────────────────────────────────────────── Jolt Physics 5.6
#
# 破片（world_physics.cpp）だけが使う。物理をやめたいときは、このブロックと
# src/world_physics.cpp を消せば元に戻る。
set(TARGET_UNIT_TESTS       OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD      OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES          OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER           OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS     OFF CACHE BOOL "" FORCE)
# Jolt 5.6 は Windows 向けに DirectX12 の compute バックエンドを一緒に建てようとする。
# このプロジェクトでは破片の剛体しか使わないので切る（DirectX SDK が要らなくなる）。
set(JPH_USE_DX12            OFF CACHE BOOL "" FORCE)
if(ASTRO_WEB)
    # wasm には SSE/AVX が無いので全部切る（Jolt は既定で有効にしようとする）
    foreach(opt USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512
                USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
        set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.6.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build)
FetchContent_MakeAvailable(JoltPhysics)

# ────────────────────────────────────────────── 本体
#
# src/*.cpp を全部拾う。ファイルを足したときに、ここを直し忘れて
# 「なぜかリンクが通らない」になるのを避けるため（CONFIGURE_DEPENDS 付き）。
file(GLOB ASTRO_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
if(ASTRO_WEB)
    list(APPEND ASTRO_SOURCES "${CMAKE_SOURCE_DIR}/web/glfw_compat.c")
endif()

add_executable(astro_proto ${ASTRO_SOURCES})
target_include_directories(astro_proto PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(astro_proto PRIVATE raylib Jolt)

if(MSVC)
    target_compile_options(astro_proto PRIVATE /W3 /utf-8)
else()
    target_compile_options(astro_proto PRIVATE -Wall -Wno-missing-field-initializers)
endif()

# ────────────────────────────────────────────── ブラウザ版の設定
if(ASTRO_WEB)
    target_compile_definitions(astro_proto PRIVATE PLATFORM_WEB)
    target_include_directories(astro_proto PRIVATE "${CMAKE_SOURCE_DIR}/web")
    # 古い emsdk の GLFW シムに足りない関数を補う（web/glfw_compat.h を参照）
    target_compile_options(astro_proto PRIVATE -include "${CMAKE_SOURCE_DIR}/web/glfw_compat.h")

    # shell.html を書き換えただけのときも必ずリンクし直す
    # （--shell-file は CMake から見えない依存なので、明示しないと取りこぼす）
    set_target_properties(astro_proto PROPERTIES
        SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")

    # wasm に同梱するファイル。これは「取りに行けなかったときの控え」で、
    # 公開版では shell.html が docs/ の params.txt / level.txt を取ってきて上書きする。
    # そのおかげで、レベルを差し替えるのに wasm のリビルドは要らない。
    set(EMBED_FLAGS "")
    foreach(f params.txt level.txt)
        if(EXISTS "${CMAKE_SOURCE_DIR}/docs/${f}")
            message(STATUS "embedding docs/${f}")
            list(APPEND EMBED_FLAGS "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/docs/${f}@/${f}")
        endif()
    endforeach()

    # スタックの指定は 3.1.27 で TOTAL_STACK → STACK_SIZE に改名された。
    # 古い emsdk（apt で入る 3.1.6 など）でも通るように出し分ける。
    if(DEFINED EMSCRIPTEN_VERSION AND EMSCRIPTEN_VERSION VERSION_LESS 3.1.27)
        set(ASTRO_STACK_FLAG "SHELL:-s TOTAL_STACK=1048576")
    else()
        set(ASTRO_STACK_FLAG "SHELL:-s STACK_SIZE=1048576")
    endif()

    target_link_options(astro_proto PRIVATE
        "SHELL:-s USE_GLFW=3"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "${ASTRO_STACK_FLAG}"
        "SHELL:-s FORCE_FILESYSTEM=1"
        # 出力 HTML の圧縮は切る。中身は自前の shell.html なので効果が薄いうえ、
        # apt 版 emscripten 3.1.6 では htmlmin の呼び出しが壊れていてリンクが失敗する。
        "SHELL:-s MINIFY_HTML=0"
        # レベルの受け渡しに shell.html から呼ぶ入口（level_io.cpp）
        "SHELL:-s EXPORTED_FUNCTIONS=['_main','_AstroLoadLevelText','_AstroSaveLevelDownload','_AstroToggleEditor']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','addRunDependency','removeRunDependency']"
        "SHELL:-s SINGLE_FILE=1"           # wasm を html に埋め込んで1ファイルにする
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        ${EMBED_FLAGS})
else()
    # 実行ファイルの隣ではなく、リポジトリ直下から起動する前提。
    # params.txt / level.txt は "../params.txt" のような候補も探しにいく。
    if(UNIX AND NOT APPLE)
        target_link_libraries(astro_proto PRIVATE m pthread dl)
    endif()
    # MinGW（GCC）で Windows 向けに建てるときは、ランタイム DLL を中に取り込んで
    # exe 1つで動くようにする。MSVC で建てる場合は関係ない。
    if(WIN32 AND NOT MSVC)
        # -mwindows は付けない。[params]/[level] の読み込みログを見たいので、
        # コンソールも一緒に出る形にしておく（調整用の試作なので、そのほうが便利）。
        target_link_options(astro_proto PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()
endif()
