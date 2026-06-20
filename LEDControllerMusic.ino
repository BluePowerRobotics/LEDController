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
#include "notes_data.h"

// ============================================================================
// 硬件配置
// ============================================================================
#define LED_PIN       6
#define I2C_ADDRESS   0x08
#define LED_COUNT     100
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GBR

// ============================================================================
// BREATH 模式参数
// ============================================================================
#define BREATH_COLOR         CRGB(0, 180, 255)  // 预设颜色 (冰蓝)
#define BREATH_DECAY_SEC     0.8f               // 衰减时长 (秒)
#define BREATH_MAINTENANCE   40                 // 维持亮度 (0-255)
#define BREATH_FULL          255                // 触发瞬间亮度
#define BREATH_MAX_TRIGGERS  64                 // 最大同时活跃触发数

// ============================================================================
// FLOW 模式参数
// ============================================================================
#define FLOW_SPEED      50.0f   // 脉冲流动速度 (LEDs/秒)
#define FLOW_TAIL       25      // 拖尾长度 (LED 个数)
#define FLOW_MAX_PULSES 50      // 最大同时脉冲数

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
int        musicIndex    = 0;      // 当前音符索引 (0..MUSIC_NOTE_COUNT-1)

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
// 从 PROGMEM 读取浮点数
// ============================================================================
float pgmReadFloat(const float* ptr) {
    float val;
    memcpy_P(&val, ptr, sizeof(float));
    return val;
}

// ============================================================================
// BREATH 触发状态
// ============================================================================
struct BreathTrigger {
    float triggerSec;  // 触发时刻 (相对于 startTime, 秒)
    bool  active;
};

BreathTrigger breathTriggers[BREATH_MAX_TRIGGERS];
int            breathTriggerCount = 0;
int            breathNextNote     = 0;  // 下一个待触发的音符索引

// ============================================================================
// FLOW 脉冲状态
// ============================================================================
struct FlowPulse {
    float   startSec;   // 脉冲出发时刻 (相对于 startTime, 秒)
    uint8_t hue;        // 色相
    bool    active;
};

FlowPulse pulses[FLOW_MAX_PULSES];
int       pulseCount = 0;  // 已触发的脉冲数
int       nextNote   = 0;  // 下一个待触发的音符索引

// ============================================================================
// 音乐效果函数
// ============================================================================

// --- BREATH 初始化 (enable=0 时调用) ---
void breathInit() {
    breathTriggerCount = 0;
    breathNextNote     = 0;
    for (int i = 0; i < BREATH_MAX_TRIGGERS; i++) {
        breathTriggers[i].active = false;
    }
}

// --- BREATH 呼吸效果 ---
// 整条灯带统一颜色，每次触发后亮度从 FULL 指数衰减至 MAINTENANCE
// 多次触发可叠加，取所有活跃包络的最大亮度
void musicBreath() {
    uint32_t elapsed = millis() - startTime;
    float elapsedSec = elapsed / 1000.0f;

    // 1. 注册新触发
    while (breathNextNote < MUSIC_NOTE_COUNT &&
           breathTriggerCount < BREATH_MAX_TRIGGERS) {
        float t = pgmReadFloat(&musicBreathTime[breathNextNote]);
        if (elapsedSec >= t) {
            // 复用或新增触发槽
            int slot = -1;
            for (int i = 0; i < BREATH_MAX_TRIGGERS; i++) {
                if (!breathTriggers[i].active) {
                    slot = i;
                    break;
                }
            }
            if (slot == -1) slot = breathTriggerCount;  // 所有槽满则追加

            breathTriggers[slot].triggerSec = t;
            breathTriggers[slot].active     = true;
            if (slot == breathTriggerCount) breathTriggerCount++;
            breathNextNote++;
        } else {
            break;
        }
    }

    // 2. 计算每个触发的当前亮度，取最大值
    uint8_t maxBrightness = BREATH_MAINTENANCE;  // 维持亮度保底

    for (int i = 0; i < breathTriggerCount; i++) {
        if (!breathTriggers[i].active) continue;

        float dt = elapsedSec - breathTriggers[i].triggerSec;
        if (dt < 0.0f) dt = 0.0f;

        // 衰减超过 3 倍衰减时长则标记失活
        if (dt > BREATH_DECAY_SEC * 3.0f) {
            breathTriggers[i].active = false;
            continue;
        }

        // 指数衰减: FULL → MAINTENANCE
        float ratio = exp(-dt / BREATH_DECAY_SEC);
        uint8_t bri = BREATH_MAINTENANCE +
                      (uint8_t)((BREATH_FULL - BREATH_MAINTENANCE) * ratio);

        if (bri > maxBrightness) maxBrightness = bri;
    }

    // 3. 整条灯带统一着色 + 亮度
    CRGB color = BREATH_COLOR;
    fill_solid(leds, LED_COUNT, color);
    nscale8(leds, LED_COUNT, maxBrightness);
}

// --- FLOW 初始化 (enable=0 时调用) ---
void flowInit() {
    pulseCount = 0;
    nextNote   = 0;
    for (int i = 0; i < FLOW_MAX_PULSES; i++) {
        pulses[i].active = false;
    }
}

// --- FLOW 流光效果 ---
// 多脉冲拖尾单向流动: 每个音符触发一个光斑，从索引0向末端流动
// 流动方向头部最亮，向尾部线性递减
void musicFlow() {
    uint32_t elapsed = millis() - startTime;
    float elapsedSec = elapsed / 1000.0f;

    // 1. 触发新脉冲: 遍历音符表中所有到时间的音符
    while (nextNote < MUSIC_NOTE_COUNT && pulseCount < FLOW_MAX_PULSES) {
        float t = pgmReadFloat(&musicFlowTime[nextNote]);
        if (elapsedSec >= t) {
            // 触发新脉冲
            pulses[pulseCount].startSec = t;
            // 频率 → 色相
            float freq = pgmReadFloat(&musicFlowFreq[nextNote]);
            uint8_t hue = (uint8_t)constrain(
                map((long)(freq * 10), 670, 14000, 0, 255),
                0, 255
            );
            pulses[pulseCount].hue   = hue;
            pulses[pulseCount].active = true;
            pulseCount++;
            nextNote++;
        } else {
            break;
        }
    }

    // 2. 清空灯带
    fill_solid(leds, LED_COUNT, CRGB::Black);

    // 3. 绘制每个活跃脉冲
    for (int p = 0; p < pulseCount; p++) {
        if (!pulses[p].active) continue;

        // 脉冲已流动的秒数
        float dt = elapsedSec - pulses[p].startSec;
        if (dt < 0.0f) dt = 0.0f;

        // 当前头部位置 (浮点 LED 索引)
        float headPos = dt * FLOW_SPEED;  // 单位: LEDs

        // 头部超出灯带范围 -> 标记失活
        if (headPos >= LED_COUNT + FLOW_TAIL) {
            pulses[p].active = false;
            continue;
        }

        // 遍历拖尾范围
        int tailStart = (int)(headPos - FLOW_TAIL);
        if (tailStart < 0) tailStart = 0;
        int tailEnd = (int)headPos;
        if (tailEnd >= LED_COUNT) tailEnd = LED_COUNT - 1;

        for (int i = tailStart; i <= tailEnd; i++) {
            // 该 LED 与头部的距离
            float dist = headPos - (float)i;
            if (dist < 0.0f) dist = 0.0f;
            if (dist > (float)FLOW_TAIL) dist = (float)FLOW_TAIL;

            // 亮度线性递减: 头部 255 → 尾部 0
            uint8_t bri = (uint8_t)(255.0f * (1.0f - dist / (float)FLOW_TAIL));

            if (bri > 0) {
                CRGB c = CHSV(pulses[p].hue, 255, 255);
                c.nscale8(bri);
                leds[i] += c;
            }
        }
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
    musicIndex   = 0;
    if (musicMode == MUSIC_FLOW) {
        flowInit();
    } else if (musicMode == MUSIC_BREATH) {
        breathInit();
    }
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