/*
 * LEDControllerMusic.ino
 * I2C 音乐模式 LED 控制器
 * 基于 LEDController.ino 的 I2C 架构，专门处理音乐联动逻辑。
 *
 * I2C 命令格式:
 *   data[0] = 0x05  (音乐模式命令，继原 LEDController.ino 中 0x01/0x02/0x03 之后的扩展命令)
 *   data[1] = 模式: 0 = BREATH (呼吸), 1 = FLOW (流光)
 *   data[2] = 使能: 0 = 记录当前时刻为 startTime 并启用; 1 = 禁用
 *   data[3] = 保留 (未使用)
 */

#include <FastLED.h>
#include <Wire.h>

// ============================================================================
// 硬件配置
// ============================================================================
#define LED_PIN       6
#define I2C_ADDRESS   0x08
#define LED_COUNT     100
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GBR

// ============================================================================
// 默认参数
// ============================================================================
#define DEFAULT_BRIGHTNESS  128

// ============================================================================
// 全局变量
// ============================================================================
CRGB leds[LED_COUNT];

// --- I2C 接收缓冲区 ---
volatile uint8_t i2cCmd[4] = {0};
volatile bool    i2cDataReady = false;

// --- 音乐模式状态 ---
enum MusicMode {
    MUSIC_BREATH = 0,
    MUSIC_FLOW   = 1
};

MusicMode  musicMode     = MUSIC_BREATH;
bool       musicEnabled  = false;
uint32_t   startTime     = 0;      // 启用时刻 (millis)

// --- 全局亮度 ---
uint8_t globalBrightness = DEFAULT_BRIGHTNESS;

// ============================================================================
// I2C 接收回调
// ============================================================================
void receiveEvent(int howMany) {
    if (howMany >= 4) {
        for (int i = 0; i < 4; i++) {
            i2cCmd[i] = Wire.read();
        }
        i2cDataReady = true;
    } else {
        // 丢弃不完整的数据帧
        while (Wire.available()) Wire.read();
    }
}

// ============================================================================
// 音乐效果函数 (占位)
// ============================================================================

// --- BREATH 呼吸效果 ---
void musicBreath() {
    // TODO: 实现呼吸效果逻辑
    // 使用 startTime 和 millis() 计算相位
    uint32_t elapsed = millis() - startTime;
    uint8_t brightness = beatsin8(30, 10, 255, 0, elapsed >> 16);  // 简单示例
    fill_solid(leds, LED_COUNT, CRGB::Red);
    nscale8(leds, LED_COUNT, brightness);
}

// --- FLOW 流光效果 ---
void musicFlow() {
    // TODO: 实现流光效果逻辑
    // 使用 startTime 和 millis() 计算相位
    uint32_t elapsed = millis() - startTime;
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = CHSV((elapsed / 10 + i * 3) % 255, 255, 255);
    }
}

// ============================================================================
// 音乐命令处理
// ============================================================================
void processMusicCommand(uint8_t mode, uint8_t enable) {
    // 设置模式
    if (mode == 0) {
        musicMode = MUSIC_BREATH;
        Serial.println(F("Music mode: BREATH"));
    } else if (mode == 1) {
        musicMode = MUSIC_FLOW;
        Serial.println(F("Music mode: FLOW"));
    } else {
        Serial.print(F("Unknown music mode: "));
        Serial.println(mode);
        return;
    }

    // 设置使能状态
    if (enable == 0) {
        startTime    = millis();
        musicEnabled = true;
        Serial.print(F("Music enabled, startTime = "));
        Serial.println(startTime);
    } else {
        musicEnabled = false;
        FastLED.clear();
        FastLED.show();
        Serial.println(F("Music disabled"));
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("LEDControllerMusic - Music Mode Controller"));
    Serial.print(F("LED Count: "));
    Serial.println(LED_COUNT);

    // 初始化 LED 灯带
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
    FastLED.setBrightness(globalBrightness);
    FastLED.clear();
    FastLED.show();

    // 初始化 I2C 从机
    Wire.begin(I2C_ADDRESS);
    Wire.onReceive(receiveEvent);

    Serial.print(F("I2C address: 0x"));
    Serial.println(I2C_ADDRESS, HEX);
    Serial.println(F("Ready. Waiting for music commands..."));
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    // --- 处理 I2C 命令 ---
    if (i2cDataReady) {
        noInterrupts();
        uint8_t cmd = i2cCmd[0];
        uint8_t p1  = i2cCmd[1];
        uint8_t p2  = i2cCmd[2];
        // uint8_t p3 = i2cCmd[3];  // 保留
        i2cDataReady = false;
        interrupts();

        // 音乐模式命令: 0x05 (继原 LEDController.ino 0x01/0x02/0x03 之后)
        if (cmd == 0x05) {
            processMusicCommand(p1, p2);
        } else {
            Serial.print(F("Unknown command: 0x"));
            Serial.println(cmd, HEX);
        }
    }

    // --- 运行动画 ---
    if (musicEnabled) {
        switch (musicMode) {
            case MUSIC_BREATH:
                musicBreath();
                break;
            case MUSIC_FLOW:
                musicFlow();
                break;
        }
    } else {
        // 禁用时保持空亮
        FastLED.clear();
    }

    // --- 刷新 LED ---
    FastLED.setBrightness(globalBrightness);
    FastLED.show();
    FastLED.delay(5);
}