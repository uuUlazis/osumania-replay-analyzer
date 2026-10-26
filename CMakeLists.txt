cmake_minimum_required(VERSION 3.24)
project(mania_replay_analyzer LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only.")
endif()

add_compile_options(/W4 /utf-8 /EHsc /permissive- /Zc:__cplusplus)
add_compile_definitions(
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    MANIA_REPLAY_ANALYZER_VERSION="0.1.0")

set(MRA_BACKEND ${CMAKE_CURRENT_SOURCE_DIR}/backend)
set(MRA_GENERATED ${CMAKE_BINARY_DIR}/generated)

include(${CMAKE_CURRENT_SOURCE_DIR}/backend/cmake/embed_frontend.cmake)

# ---- LZMA decoder (vendored, compiled as C) ----
add_library(mra_lzma STATIC
    ${MRA_BACKEND}/third_party/lzma/LzmaDec.c)
target_include_directories(mra_lzma PUBLIC ${MRA_BACKEND}/third_party/lzma)

# ---- Frontend embedding ----
set(MRA_FRONTEND_DIST ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist)
if(EXISTS ${MRA_FRONTEND_DIST}/index.html)
    message(STATUS "Embedding frontend from ${MRA_FRONTEND_DIST}")
else()
    message(STATUS "No frontend dist found at ${MRA_FRONTEND_DIST}; building shell-only (dev)")
endif()
mra_generate_embedded(${MRA_FRONTEND_DIST} ${MRA_GENERATED})

# ---- Core static library (all backend modules except app entry) ----
set(MRA_CORE_SOURCES
    ${MRA_BACKEND}/src/beatmap/osu_parser.cpp
    ${MRA_BACKEND}/src/http/api_server.cpp
    ${MRA_BACKEND}/src/io/md5.cpp
    ${MRA_BACKEND}/src/io/paths.cpp
    ${MRA_BACKEND}/src/lazer/lazer_format.cpp
    ${MRA_BACKEND}/src/lazer/lazer_scan.cpp
    ${MRA_BACKEND}/src/mania/analysis.cpp
    ${MRA_BACKEND}/src/mania/accuracy.cpp
    ${MRA_BACKEND}/src/mania/hit_matcher.cpp
    ${MRA_BACKEND}/src/mania/lazer_matcher.cpp
    ${MRA_BACKEND}/src/mania/score_record.cpp
    ${MRA_BACKEND}/src/mania/source_engine.cpp
    ${MRA_BACKEND}/src/mania/official_sr.cpp
    ${MRA_BACKEND}/src/mania/srr_sr.cpp
    ${MRA_BACKEND}/src/mania/judgement.cpp
    ${MRA_BACKEND}/src/util/debug_log.cpp
    ${MRA_BACKEND}/src/replay/replay_decoder.cpp
    ${MRA_BACKEND}/src/replay/replay_frames.cpp
    ${MRA_BACKEND}/src/replay/osr_reader.cpp
    ${MRA_BACKEND}/src/scores/scores_db.cpp
    ${MRA_BACKEND}/src/session/session_store.cpp
    ${MRA_BACKEND}/src/skin/skin_loader.cpp
    ${MRA_BACKEND}/src/app/dialogs.cpp
    ${MRA_BACKEND}/src/app/embedded_assets.cpp
    ${MRA_BACKEND}/src/webview/webview2_shell.cpp)

add_library(mra_core STATIC ${MRA_CORE_SOURCES})
target_include_directories(mra_core
    PUBLIC ${MRA_BACKEND}/src ${MRA_BACKEND}/third_party ${MRA_GENERATED})
target_link_libraries(mra_core PUBLIC mra_lzma dxgi)

# ---- WebView2 (optional static linkage) ----
set(MRA_WEBVIEW2_DIR ${MRA_BACKEND}/third_party/webview2)
if(EXISTS ${MRA_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib AND
   EXISTS ${MRA_WEBVIEW2_DIR}/include/WebView2.h)
    message(STATUS "WebView2 static loader found; window shell enabled")
    target_compile_definitions(mra_core PUBLIC MANIA_HAS_WEBVIEW2=1)
    target_include_directories(mra_core PUBLIC ${MRA_WEBVIEW2_DIR}/include)
    target_link_libraries(mra_core PUBLIC
        ${MRA_WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib)
else()
    message(STATUS "WebView2 SDK not found; browser fallback only")
endif()

# ---- Executable ----
add_executable(ManiaReplayAnalyzer WIN32
    ${MRA_BACKEND}/src/app/main.cpp
    ${MRA_BACKEND}/src/app/app.rc
    ${MRA_GENERATED}/mra_resources.rc)
target_link_libraries(ManiaReplayAnalyzer PRIVATE mra_core
    ws2_32 shell32 ole32 oleaut32 shlwapi)
set_target_properties(ManiaReplayAnalyzer PROPERTIES
    OUTPUT_NAME ManiaReplayAnalyzer
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Unit tests (console) ----
add_executable(mra_tests
    ${MRA_BACKEND}/tests/test_mania_core.cpp)
target_link_libraries(mra_tests PRIVATE mra_core)
# E2E fixture data lives inside the version snapshot (root fixtures/ removed
# 2026-10-25) — compile-time absolute dir, same pattern as MRA_SR_FIXTURE_DIR.
target_compile_definitions(mra_tests PRIVATE
    MRA_E2E_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_target_properties(mra_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Star-rating port tests (separate console target: test_mania_core.cpp owns the
# ---- mra_tests main and test files outside my scope cannot be edited) ----
add_executable(mra_tests_star_rating
    ${MRA_BACKEND}/tests/test_star_rating.cpp)
target_link_libraries(mra_tests_star_rating PRIVATE mra_core)
target_compile_definitions(mra_tests_star_rating PRIVATE
    MRA_SR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/backend/tests/fixtures/srr"
    MRA_SR_OFFICIAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/backend/tests/fixtures/official")
set_target_properties(mra_tests_star_rating PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_custom_target(run_tests
    COMMAND ${CMAKE_BINARY_DIR}/bin/mra_tests
    COMMAND ${CMAKE_BINARY_DIR}/bin/mra_tests_star_rating
    COMMAND ${CMAKE_BINARY_DIR}/bin/mra_tests_replay_lanes
    COMMAND ${CMAKE_BINARY_DIR}/bin/mra_tests_lazer_format
    COMMAND ${CMAKE_BINARY_DIR}/bin/mra_tests_lazer_scan
    DEPENDS mra_tests mra_tests_star_rating mra_tests_replay_lanes
            mra_tests_lazer_format mra_tests_lazer_scan
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running mra_tests + star_rating + replay_lanes + lazer")

# ---- I-6 偶数键 lane bit 顺序测试 (同样单独 target, 不动 mra_tests 的 main) ----
add_executable(mra_tests_replay_lanes
    ${MRA_BACKEND}/tests/test_replay_lanes.cpp)
target_link_libraries(mra_tests_replay_lanes PRIVATE mra_core)
set_target_properties(mra_tests_replay_lanes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- L1 lazer 数据通道测试 (各自单独 target, mra_tests 的 main 归
# ---- test_mania_core.cpp; blob 由 backend/tests/gen_lazer_blob.py 生成) ----
add_executable(mra_tests_lazer_format
    ${MRA_BACKEND}/tests/lazer_format_tests.cpp)
target_link_libraries(mra_tests_lazer_format PRIVATE mra_core)
set_target_properties(mra_tests_lazer_format PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(mra_tests_lazer_scan
    ${MRA_BACKEND}/tests/lazer_scan_tests.cpp)
target_link_libraries(mra_tests_lazer_scan PRIVATE mra_core)
set_target_properties(mra_tests_lazer_scan PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- L2 lazer 判定重建引擎测试 (引擎子代理追加; 照 mra_tests_lazer_scan 的写法) ----
add_executable(mra_tests_lazer_matcher
    ${MRA_BACKEND}/tests/lazer_matcher_tests.cpp)
target_link_libraries(mra_tests_lazer_matcher PRIVATE mra_core)
set_target_properties(mra_tests_lazer_matcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(mra_tests_lazer_e2e
    ${MRA_BACKEND}/tests/lazer_e2e.cpp)
target_link_libraries(mra_tests_lazer_e2e PRIVATE mra_core)
target_compile_definitions(mra_tests_lazer_e2e PRIVATE
    MRA_LAZER_SAMPLES_JSON="${CMAKE_CURRENT_SOURCE_DIR}/build/tmp_lazer/lazer_samples.json"
    MRA_LAZER_SAMPLE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/test/sample/lazer")
set_target_properties(mra_tests_lazer_e2e PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
