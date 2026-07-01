#pragma once
#include <Arduino.h>
#include <stdint.h>

/**
 * @brief 音声出力ピン: A0 = 真の 12bit DAC（Arduino UNO R4 WiFi / RA4M1）
 *
 * 倍音合成した波形を DAC からアナログ出力し、小型アンプキット TA7368 を経て
 * ダイナミックスピーカー（50mmφ 8Ω 0.2W WYGD50D-8-03）を駆動します。
 *
 * 【配線】
 *   A0 ──[カップリングコンデンサ ~10µF]── TA7368 入力(+)
 *   TA7368 出力 ── スピーカー(8Ω) ── GND
 *   ※ DAC 出力は 0〜約3.3V を中心 1.65V で振動。直流カットのため
 *     カップリングコンデンサ経由でアンプへ入れること。
 *
 * 【注意】UNO R4 WiFi の DAC は A0 専用。他ピンでは tone() 相当の矩形波しか
 *         出せないため、倍音合成には A0 を使用する。
 */
constexpr uint8_t AUDIO_PIN = A0;

/**
 * @brief 計測用シリアルログの ON/OFF
 *
 * true にすると、PLAY 受信・発音イベント・SYNC 誤差を CSV 形式で
 * 毎回シリアル出力する（輪唱の入りタイミングやテンポ精度の計測用）。
 * 本番演奏でシリアル出力のオーバーヘッドを避けたい場合は false にする。
 */
constexpr bool ENABLE_TIMING_LOG = true;
