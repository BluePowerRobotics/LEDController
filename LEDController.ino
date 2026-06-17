/*
 * Blinkin-to-WS2812B Controller
 * Reads PWM signal from Blinkin (1105-2095 us) and drives WS2812B LED strip
 * with 100 different animation patterns.
 *
 * Hardware:
 *   - PWM input: Pin 2 (interrupt-capable)
 *   - LED data:  Pin 6
 *   - LED count: 60 (adjustable below)
 *
 * Adjustable defaults for pattern parameters (set here, not from Blinkin):
 *   DENSITY  - Pattern Density (0-255)
 *   SPEED    - Animation Speed (0-255, higher = faster)
 *   WIDTH    - Pattern Width for Larson/Light Chase (1-255)
 *   DIMMING  - Dimming speed for Light Chase (0-255)
 *   COLOR1   - Default primary color (R,G,B)
 *   COLOR2   - Default secondary color (R,G,B)
 */

#include <FastLED.h>
#include <Wire.h>

// ============================================================================
// CONFIGURABLE DEFAULTS
// ============================================================================
#define LED_PIN      6        // WS2812B data pin
#define I2C_ADDRESS  0x08     // Arduino I2C slave address
#define LED_COUNT    100       // Number of WS2812B LEDs
#define LED_TYPE     WS2812B  // LED chip type
#define COLOR_ORDER  GBR      // Typical for WS2812B

// Default pattern parameters (used as substitutes for animation knobs)
#define DEFAULT_DENSITY   128   // Pattern Density (0-255)
#define DEFAULT_SPEED     128   // Animation Speed (0-255, higher = faster)
#define DEFAULT_WIDTH     50    // Pattern Width (1-255)
#define DEFAULT_DIMMING   128   // Dimming amount (0-255)

// Default Color1 & Color2 (used by patterns 49-78)
CRGB defaultColor1 = CRGB::Red;
CRGB defaultColor2 = CRGB::Blue;

// ============================================================================
// GLOBALS
// ============================================================================
CRGB leds[LED_COUNT];

// --- I2C receive buffer ---
volatile uint8_t i2cCmd[4] = {0};
volatile bool    i2cDataReady = false;

// --- Current pattern ---
int  currentPattern   = 0;     // 0-99
int  previousPattern  = -1;
bool patternChanged   = true;  // true on change to reset animation state

// --- Global brightness (0-255) ---
uint8_t globalBrightness = 255;

// ============================================================================
// SOLID COLOR LOOKUP (patterns 78-99, 22 colors, indexed 0..21)
// ============================================================================
const CRGB solidColors[22] PROGMEM = {
    CRGB::HotPink,    // 78: Hot Pink
    CRGB::DarkRed,    // 79: Dark Red
    CRGB::Red,        // 80: Red
    CRGB::OrangeRed,  // 81: Red Orange
    CRGB::Orange,     // 82: Orange
    CRGB::Gold,       // 83: Gold
    CRGB::Yellow,     // 84: Yellow
    CRGB::LawnGreen,  // 85: Lawn Green
    CRGB::Lime,       // 86: Lime
    CRGB::DarkGreen,  // 87: Dark Green
    CRGB::Green,      // 88: Green
    CRGB::Cyan,       // 89: Blue Green (Cyan)
    CRGB::Aqua,       // 90: Aqua
    CRGB::SkyBlue,    // 91: Sky Blue
    CRGB::DarkBlue,   // 92: Dark Blue
    CRGB::Blue,       // 93: Blue
    CRGB::BlueViolet, // 94: Blue Violet
    CRGB::Violet,     // 95: Violet
    CRGB::White,      // 96: White
    CRGB::Gray,       // 97: Gray
    CRGB::DarkGray,   // 98: Dark Gray
    CRGB::Black       // 99: Black
};

// ============================================================================
// I2C RECEIVE HANDLER (slave at I2C_ADDRESS)
// ============================================================================
void receiveEvent(int howMany) {
    if (howMany >= 4) {
        for (int i = 0; i < 4; i++) {
            i2cCmd[i] = Wire.read();
        }
        i2cDataReady = true;
    } else {
        // Discard incomplete frame
        while (Wire.available()) Wire.read();
    }
}

// ============================================================================
// UTILITY: map 0-255 speed to millisecond-based or 8-bit beat timing
// ============================================================================
uint8_t speedToBeats(uint8_t speed) {
    // Higher speed = higher beats per minute equivalent
    // speed 128 ~= 60 BPM; speed 255 ~= 120 BPM; speed 1 ~= 1 BPM
    return map(speed, 0, 255, 1, 120);
}

uint8_t speedToDelay(uint8_t speed) {
    // Higher speed = shorter delay
    return map(speed, 0, 255, 200, 5);
}

// ============================================================================
// ANIMATION FUNCTIONS
// ============================================================================

// --- Helper: Glitter ---
void addGlitter(fract8 chanceOfGlitter) {
    if (random8() < chanceOfGlitter) {
        leds[random16(LED_COUNT)] += CRGB::White;
    }
}

// --- 1-6: Rainbow with various palettes ---
void rainbowWithPalette(const CRGBPalette16& palette, uint8_t density, uint8_t speed) {
    uint8_t beat = beatsin8(speedToBeats(speed), 0, 255);
    fill_palette(leds, LED_COUNT, beat, density, palette, globalBrightness, LINEARBLEND);
}

// --- 7: Confetti ---
void confetti(uint8_t speed) {
    fadeToBlackBy(leds, LED_COUNT, speedToDelay(speed) / 2 + 1);
    int pos = random16(LED_COUNT);
    leds[pos] += CHSV(random8(), 255, 255);
}

// --- 8-10: Shot (single color flash that fades) ---
void shot(CRGB color, uint8_t speed) {
    fadeToBlackBy(leds, LED_COUNT, map(speed, 0, 255, 1, 40));
    if (random8() < 20) {
        fill_solid(leds, LED_COUNT, color);
    }
}

// --- 11-15: Sinelon (traveling sine pulse) ---
void sinelonWithPalette(const CRGBPalette16& palette, uint8_t density, uint8_t speed) {
    fadeToBlackBy(leds, LED_COUNT, map(speed, 0, 255, 1, 30));
    int pos = beatsin16(speedToBeats(speed), 0, LED_COUNT - 1);
    leds[pos] += ColorFromPalette(palette, millis() / 10, globalBrightness);
    // Add density-based width
    int width = map(density, 0, 255, 1, 8);
    for (int i = 1; i <= width; i++) {
        if (pos + i < LED_COUNT) leds[pos + i] += ColorFromPalette(palette, millis() / 10, globalBrightness / (i + 1));
        if (pos - i >= 0)        leds[pos - i] += ColorFromPalette(palette, millis() / 10, globalBrightness / (i + 1));
    }
}

// --- 16-20: Beats per Minute ---
void bpmWithPalette(const CRGBPalette16& palette, uint8_t density, uint8_t speed) {
    uint8_t beat = beatsin8(speedToBeats(speed), 64, 255);
    CRGB color = ColorFromPalette(palette, millis() / 50, globalBrightness);
    // Density controls how many LEDs light up on the beat
    uint8_t numLit = map(density, 0, 255, 1, LED_COUNT);
    fadeToBlackBy(leds, LED_COUNT, 20);
    if (beat > 240) {
        for (int i = 0; i < numLit; i++) {
            leds[random16(LED_COUNT)] = color;
        }
    }
}

// --- 21-22: Fire ---
void fireEffect(uint8_t size, uint8_t speed) {
    // Simplified fire using palette
    CRGBPalette16 firePal = HeatColors_p;
    uint8_t cooling = map(speed, 0, 255, 20, 100);
    uint8_t sparking = map(size, 0, 255, 30, 120);

    // Cool down every cell
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = leds[i].fadeToBlackBy(random8(cooling));
    }

    // Heat rises from bottom
    for (int k = LED_COUNT - 1; k >= 2; k--) {
        leds[k] = (leds[k - 1] + leds[k - 2] + leds[k - 2]) / 3;
    }

    // Random sparks at bottom
    if (random8() < sparking) {
        int y = random8(3);
        leds[y] += CRGB(random8(160, 255), random8(0, 80), 0);
    }

    // Map heat to colors
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = ColorFromPalette(firePal, scale8(leds[i].r, 240), globalBrightness);
    }
}

// --- 23-27: Twinkles ---
void twinklesWithPalette(const CRGBPalette16& palette, uint8_t speed) {
    uint8_t fadeAmount = map(speed, 0, 255, 5, 40);
    fadeToBlackBy(leds, LED_COUNT, fadeAmount);
    if (random8() < map(speed, 0, 255, 10, 60)) {
        leds[random16(LED_COUNT)] = ColorFromPalette(palette, random8(), globalBrightness);
    }
}

// --- 28-32: Color Waves ---
void colorWavesWithPalette(const CRGBPalette16& palette, uint8_t speed) {
    uint8_t beat = beatsin8(speedToBeats(speed), 0, 255);
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = ColorFromPalette(palette, beat + i * 3, globalBrightness);
    }
}

// --- 33-34: Larson Scanner ---
void larsonScanner(CRGB color, uint8_t width, uint8_t speed) {
    fadeToBlackBy(leds, LED_COUNT, map(speed, 0, 255, 1, 25));
    int pos = beatsin16(speedToBeats(speed), 0, LED_COUNT - 1);
    int halfW = width / 2;
    for (int i = pos - halfW; i <= pos + halfW; i++) {
        if (i >= 0 && i < LED_COUNT) {
            uint8_t dist = abs(i - pos);
            uint8_t brightness = (dist < halfW) ? 255 - (255 * dist / (halfW + 1)) : 0;
            leds[i] = color;
            leds[i].nscale8(brightness);
        }
    }
}

// --- 35-37: Light Chase (theater marquee) ---
void lightChase(CRGB color, uint8_t dimming, uint8_t speed) {
    uint8_t spacing = map(dimming, 0, 255, 2, 8);
    static uint8_t offset = 0;
    uint8_t advanceRate = map(speed, 0, 255, 1, 20);

    EVERY_N_MILLIS_I(thistimer, speedToDelay(speed)) {
        offset = (offset + 1) % spacing;
    }

    fadeToBlackBy(leds, LED_COUNT, map(speed, 0, 255, 5, 50));
    for (int i = offset; i < LED_COUNT; i += spacing) {
        leds[i] = color;
    }
}

// --- 38-41: Heartbeat ---
void heartbeat(CRGB color, uint8_t speed) {
    // Two quick beats followed by a pause
    uint8_t bpm = speedToBeats(speed);
    uint8_t beat = beatsin8(bpm, 0, 255);

    uint8_t brightness;
    if (beat < 40) {
        brightness = 0;
    } else if (beat < 80) {
        brightness = map(beat, 40, 80, 0, 255);   // First beat up
    } else if (beat < 100) {
        brightness = map(beat, 80, 100, 255, 50);  // First beat down
    } else if (beat < 140) {
        brightness = map(beat, 100, 140, 50, 255); // Second beat up
    } else if (beat < 160) {
        brightness = map(beat, 140, 160, 255, 0);  // Second beat down
    } else {
        brightness = 0;  // Rest period
    }

    fill_solid(leds, LED_COUNT, color);
    nscale8(leds, LED_COUNT, brightness);
}

// --- 42-44: Breath (smooth sine wave) ---
void breath(CRGB color, uint8_t speed) {
    uint8_t brightness = beatsin8(speedToBeats(speed), 10, 255);
    fill_solid(leds, LED_COUNT, color);
    nscale8(leds, LED_COUNT, brightness);
}

// --- 45-48: Strobe ---
void strobe(CRGB color, uint8_t speed) {
    uint8_t beat = beatsin8(speedToBeats(speed) * 2, 0, 255);
    if (beat > 200) {
        fill_solid(leds, LED_COUNT, color);
    } else {
        fill_solid(leds, LED_COUNT, CRGB::Black);
    }
}

// --- 49, 59: End to End Blend to Black ---
void endToEndBlendToBlack(CRGB color, uint8_t speed) {
    static int pos = 0;
    EVERY_N_MILLIS_I(timer, speedToDelay(speed)) {
        pos++;
        if (pos >= LED_COUNT) pos = 0;
    }
    fadeToBlackBy(leds, LED_COUNT, 20);
    leds[pos] = color;
}

// --- 50, 60: Single-color Larson Scanner ---
void larsonScannerSingle(CRGB color, uint8_t width, uint8_t speed) {
    larsonScanner(color, width, speed);
}

// --- 51, 61: Single-color Light Chase ---
void lightChaseSingle(CRGB color, uint8_t dimming, uint8_t speed) {
    lightChase(color, dimming, speed);
}

// --- 52-54, 62-64: Heartbeat Slow/Medium/Fast ---
void heartbeatSpeed(CRGB color, uint8_t speedPreset) {
    uint8_t bpm;
    switch (speedPreset) {
        case 0: bpm = 30; break;  // Slow
        case 1: bpm = 60; break;  // Medium
        case 2: bpm = 90; break;  // Fast
        default: bpm = 60; break;
    }
    uint8_t beat = beatsin8(bpm, 0, 255);
    uint8_t brightness;
    if (beat < 40) brightness = 0;
    else if (beat < 80) brightness = map(beat, 40, 80, 0, 255);
    else if (beat < 100) brightness = map(beat, 80, 100, 255, 50);
    else if (beat < 140) brightness = map(beat, 100, 140, 50, 255);
    else if (beat < 160) brightness = map(beat, 140, 160, 255, 0);
    else brightness = 0;
    fill_solid(leds, LED_COUNT, color);
    nscale8(leds, LED_COUNT, brightness);
}

// --- 55-56, 65-66: Breath Slow/Fast ---
void breathSpeed(CRGB color, uint8_t speedPreset) {
    uint8_t bpm = (speedPreset == 0) ? 30 : 80;
    uint8_t brightness = beatsin8(bpm, 10, 255);
    fill_solid(leds, LED_COUNT, color);
    nscale8(leds, LED_COUNT, brightness);
}

// --- 57, 67: Shot (single color) ---
void shotSingle(CRGB color, uint8_t speed) {
    shot(color, speed);
}

// --- 58, 68: Strobe (single color) ---
void strobeSingle(CRGB color, uint8_t speed) {
    strobe(color, speed);
}

// --- 69-70: Sparkle, Color1 on Color2 / Color2 on Color1 ---
void sparkleTwoColor(CRGB bgColor, CRGB sparkleColor, uint8_t speed) {
    fill_solid(leds, LED_COUNT, bgColor);
    uint8_t fadeSpeed = map(speed, 0, 255, 5, 30);
    if (random8() < map(speed, 0, 255, 10, 50)) {
        leds[random16(LED_COUNT)] = sparkleColor;
    }
}

// --- 71: Color Gradient, Color 1 and 2 ---
void colorGradient(CRGB c1, CRGB c2) {
    fill_gradient_RGB(leds, LED_COUNT, c1, c2);
}

// --- 72: Beats per Minute, Color 1 and 2 ---
void bpmTwoColor(CRGB c1, CRGB c2, uint8_t density, uint8_t speed) {
    uint8_t beat = beatsin8(speedToBeats(speed), 64, 255);
    uint8_t numLit = map(density, 0, 255, 1, LED_COUNT);
    fadeToBlackBy(leds, LED_COUNT, 20);
    if (beat > 240) {
        for (int i = 0; i < numLit; i++) {
            leds[random16(LED_COUNT)] = (random8() < 128) ? c1 : c2;
        }
    }
}

// --- 73: End to End Blend, Color 1 to 2 ---
void endToEndBlendTwoColor(CRGB c1, CRGB c2, uint8_t speed) {
    static int pos = 0;
    EVERY_N_MILLIS_I(timer, speedToDelay(speed)) {
        pos++;
        if (pos >= LED_COUNT) pos = 0;
    }
    fadeToBlackBy(leds, LED_COUNT, 15);
    // Blend from c1 to c2 based on position
    uint8_t blendAmt = map(pos, 0, LED_COUNT - 1, 0, 255);
    leds[pos] = blend(c1, c2, blendAmt);
}

// --- 74: End to End Blend (uses Color1 to Color2 internally, same as 73 but named differently) ---
void endToEndBlendTwoColorAlt(CRGB c1, CRGB c2, uint8_t speed) {
    endToEndBlendTwoColor(c1, c2, speed);
}

// --- 75: Color 1 and Color 2 no blending (alternating setup) ---
void twoColorNoBlending(CRGB c1, CRGB c2) {
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = (i % 2 == 0) ? c1 : c2;
    }
}

// --- 76: Twinkles, Color 1 and 2 ---
void twinklesTwoColor(CRGB c1, CRGB c2, uint8_t speed) {
    fadeToBlackBy(leds, LED_COUNT, map(speed, 0, 255, 5, 40));
    if (random8() < map(speed, 0, 255, 10, 60)) {
        leds[random16(LED_COUNT)] = (random8() < 128) ? c1 : c2;
    }
}

// --- 77: Color Waves, Color 1 and 2 ---
void colorWavesTwoColor(CRGB c1, CRGB c2, uint8_t speed) {
    uint8_t beat = beatsin8(speedToBeats(speed), 0, 255);
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t blendAmt = beatsin8(6, 0, 255, 0, i * 8);
        leds[i] = blend(c1, c2, blendAmt);
        leds[i].nscale8(beatsin8(speedToBeats(speed), 128, 255, 0, i * 4));
    }
}

// --- 78: Sinelon, Color 1 and 2 ---
void sinelonTwoColor(CRGB c1, CRGB c2, uint8_t density, uint8_t speed) {
    fadeToBlackBy(leds, LED_COUNT, map(speed, 0, 255, 1, 30));
    int pos = beatsin16(speedToBeats(speed), 0, LED_COUNT - 1);
    uint8_t blendAmt = map(pos, 0, LED_COUNT - 1, 0, 255);
    CRGB color = blend(c1, c2, blendAmt);
    leds[pos] = color;
    int width = map(density, 0, 255, 1, 8);
    for (int i = 1; i <= width; i++) {
        if (pos + i < LED_COUNT) leds[pos + i] += color;
        if (pos - i >= 0)        leds[pos - i] += color;
        color.fadeToBlackBy(40);
    }
}

// ============================================================================
// MAIN PATTERN DISPATCHER
// ============================================================================
void runPattern(int pattern, CRGB c1, CRGB c2,
                uint8_t density, uint8_t speed,
                uint8_t width, uint8_t dimming) {

    switch (pattern) {
        // ================================================================
        // FIXED PALETTE patterns (0-31)
        // ================================================================

        // 1-6: Rainbow with different palettes
        case 0:  rainbowWithPalette(RainbowColors_p, density, speed); break;
        case 1:  rainbowWithPalette(PartyColors_p, density, speed); break;
        case 2:  rainbowWithPalette(OceanColors_p, density, speed); break;
        case 3:  rainbowWithPalette(LavaColors_p, density, speed); break;
        case 4:  rainbowWithPalette(ForestColors_p, density, speed); break;

        // 6: Rainbow with Glitter
        case 5:
            rainbowWithPalette(RainbowColors_p, density, speed);
            addGlitter(80);
            break;

        // 7: Confetti
        case 6:  confetti(speed); break;

        // 8-10: Shot (Red, Blue, White)
        case 7:  shot(CRGB::Red, speed); break;
        case 8:  shot(CRGB::Blue, speed); break;
        case 9:  shot(CRGB::White, speed); break;

        // 11-15: Sinelon
        case 10: sinelonWithPalette(RainbowColors_p, density, speed); break;
        case 11: sinelonWithPalette(PartyColors_p, density, speed); break;
        case 12: sinelonWithPalette(OceanColors_p, density, speed); break;
        case 13: sinelonWithPalette(LavaColors_p, density, speed); break;
        case 14: sinelonWithPalette(ForestColors_p, density, speed); break;

        // 16-20: Beats per Minute
        case 15: bpmWithPalette(RainbowColors_p, density, speed); break;
        case 16: bpmWithPalette(PartyColors_p, density, speed); break;
        case 17: bpmWithPalette(OceanColors_p, density, speed); break;
        case 18: bpmWithPalette(LavaColors_p, density, speed); break;
        case 19: bpmWithPalette(ForestColors_p, density, speed); break;

        // 21-22: Fire
        case 20: fireEffect(128, speed); break;  // Medium
        case 21: fireEffect(200, speed); break;  // Large

        // 23-27: Twinkles
        case 22: twinklesWithPalette(RainbowColors_p, speed); break;
        case 23: twinklesWithPalette(PartyColors_p, speed); break;
        case 24: twinklesWithPalette(OceanColors_p, speed); break;
        case 25: twinklesWithPalette(LavaColors_p, speed); break;
        case 26: twinklesWithPalette(ForestColors_p, speed); break;

        // 28-32: Color Waves
        case 27: colorWavesWithPalette(RainbowColors_p, speed); break;
        case 28: colorWavesWithPalette(PartyColors_p, speed); break;
        case 29: colorWavesWithPalette(OceanColors_p, speed); break;
        case 30: colorWavesWithPalette(LavaColors_p, speed); break;
        case 31: colorWavesWithPalette(ForestColors_p, speed); break;

        // 33-34: Larson Scanner
        case 32: larsonScanner(CRGB::Red, width, speed); break;
        case 33: larsonScanner(CRGB::Gray, width, speed); break;

        // 35-37: Light Chase
        case 34: lightChase(CRGB::Red, dimming, speed); break;
        case 35: lightChase(CRGB::Blue, dimming, speed); break;
        case 36: lightChase(CRGB::Gray, dimming, speed); break;

        // 38-41: Heartbeat (fixed palette colors)
        case 37: heartbeat(CRGB::Red, speed); break;
        case 38: heartbeat(CRGB::Blue, speed); break;
        case 39: heartbeat(CRGB::White, speed); break;
        case 40: heartbeat(CRGB::Gray, speed); break;

        // 42-44: Breath
        case 41: breath(CRGB::Red, speed); break;
        case 42: breath(CRGB::Blue, speed); break;
        case 43: breath(CRGB::Gray, speed); break;

        // 45-48: Strobe
        case 44: strobe(CRGB::Red, speed); break;
        case 45: strobe(CRGB::Blue, speed); break;
        case 46: strobe(CRGB::Gold, speed); break;
        case 47: strobe(CRGB::White, speed); break;

        // ================================================================
        // COLOR 1 PATTERNS (48-57) - uses c1
        // ================================================================

        // 49: End to End Blend to Black
        case 48: endToEndBlendToBlack(c1, speed); break;
        // 50: Larson Scanner
        case 49: larsonScannerSingle(c1, width, speed); break;
        // 51: Light Chase
        case 50: lightChaseSingle(c1, dimming, speed); break;
        // 52-54: Heartbeat Slow/Medium/Fast
        case 51: heartbeatSpeed(c1, 0); break;  // Slow
        case 52: heartbeatSpeed(c1, 1); break;  // Medium
        case 53: heartbeatSpeed(c1, 2); break;  // Fast
        // 55-56: Breath Slow/Fast
        case 54: breathSpeed(c1, 0); break;  // Slow
        case 55: breathSpeed(c1, 1); break;  // Fast
        // 57: Shot
        case 56: shotSingle(c1, speed); break;
        // 58: Strobe
        case 57: strobeSingle(c1, speed); break;

        // ================================================================
        // COLOR 2 PATTERNS (58-67) - uses c2
        // ================================================================

        // 59: End to End Blend to Black
        case 58: endToEndBlendToBlack(c2, speed); break;
        // 60: Larson Scanner
        case 59: larsonScannerSingle(c2, width, speed); break;
        // 61: Light Chase
        case 60: lightChaseSingle(c2, dimming, speed); break;
        // 62-64: Heartbeat Slow/Medium/Fast
        case 61: heartbeatSpeed(c2, 0); break;
        case 62: heartbeatSpeed(c2, 1); break;
        case 63: heartbeatSpeed(c2, 2); break;
        // 65-66: Breath Slow/Fast
        case 64: breathSpeed(c2, 0); break;
        case 65: breathSpeed(c2, 1); break;
        // 67: Shot
        case 66: shotSingle(c2, speed); break;
        // 68: Strobe
        case 67: strobeSingle(c2, speed); break;

        // ================================================================
        // COLOR 1 & 2 PATTERNS (68-77) - uses both c1 and c2
        // ================================================================

        // 69: Sparkle, Color 1 on Color 2
        case 68: sparkleTwoColor(c2, c1, speed); break;
        // 70: Sparkle, Color 2 on Color 1
        case 69: sparkleTwoColor(c1, c2, speed); break;
        // 71: Color Gradient
        case 70: colorGradient(c1, c2); break;
        // 72: Beats per Minute
        case 71: bpmTwoColor(c1, c2, density, speed); break;
        // 73: End to End Blend, Color 1 to 2
        case 72: endToEndBlendTwoColor(c1, c2, speed); break;
        // 74: End to End Blend (alternate name, same as 73)
        case 73: endToEndBlendTwoColorAlt(c1, c2, speed); break;
        // 75: No blending setup
        case 74: twoColorNoBlending(c1, c2); break;
        // 76: Twinkles, Color 1 and 2
        case 75: twinklesTwoColor(c1, c2, speed); break;
        // 77: Color Waves, Color 1 and 2
        case 76: colorWavesTwoColor(c1, c2, speed); break;
        // 78: Sinelon, Color 1 and 2
        case 77: sinelonTwoColor(c1, c2, density, speed); break;

        // ================================================================
        // SOLID COLORS (78-99)
        // ================================================================
        case 78 ... 99:
            {
                CRGB color;
                ::memcpy_P(&color, &solidColors[pattern - 78], 3);
                fill_solid(leds, LED_COUNT, color);
            }
            break;

        default:
            // Fallback: Rainbow
            rainbowWithPalette(RainbowColors_p, density, speed);
            break;
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("Blinkin-to-WS2812B Controller"));
    Serial.print(F("LED Count: "));
    Serial.println(LED_COUNT);

    // Initialize LED strip
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
    FastLED.setBrightness(globalBrightness);
    FastLED.clear();
    FastLED.show();

    // Initialize I2C slave
    Wire.begin(I2C_ADDRESS);
    Wire.onReceive(receiveEvent);

    Serial.print(F("I2C address: 0x"));
    Serial.println(I2C_ADDRESS, HEX);
    Serial.println(F("Ready. Waiting for I2C commands..."));
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    // --- Process I2C commands ---
    if (i2cDataReady) {
        noInterrupts();
        uint8_t cmd = i2cCmd[0];
        uint8_t p1  = i2cCmd[1];
        uint8_t p2  = i2cCmd[2];
        uint8_t p3  = i2cCmd[3];
        i2cDataReady = false;
        interrupts();

        switch (cmd) {
            case 0x01:  // Set Pattern (0–99)
                if (p1 <= 99) {
                    if (currentPattern != p1) {
                        previousPattern = currentPattern;
                        currentPattern = p1;
                        patternChanged = true;
                        Serial.print(F("Pattern set to "));
                        Serial.println(p1);
                    }
                }
                break;

            case 0x02:  // Set Color1 (R,G,B)
                defaultColor1 = CRGB(p1, p2, p3);
                patternChanged = true;
                Serial.print(F("Color1 set to "));
                Serial.print(p1); Serial.print(F(","));
                Serial.print(p2); Serial.print(F(","));
                Serial.println(p3);
                break;

            case 0x03:  // Set Color2 (R,G,B)
                defaultColor2 = CRGB(p1, p2, p3);
                patternChanged = true;
                Serial.print(F("Color2 set to "));
                Serial.print(p1); Serial.print(F(","));
                Serial.print(p2); Serial.print(F(","));
                Serial.println(p3);
                break;

            case 0x04:  // Set Brightness (0–255)
                globalBrightness = p1;
                FastLED.setBrightness(globalBrightness);
                Serial.print(F("Brightness set to "));
                Serial.println(globalBrightness);
                break;

            default:
                Serial.print(F("Unknown command: 0x"));
                Serial.println(cmd, HEX);
                break;
        }
    }

    // --- Run current animation ---
    uint8_t density = DEFAULT_DENSITY;
    uint8_t speed   = DEFAULT_SPEED;
    uint8_t width   = DEFAULT_WIDTH;
    uint8_t dimming = DEFAULT_DIMMING;

    runPattern(currentPattern, defaultColor1, defaultColor2,
               density, speed, width, dimming);

    // --- Apply global brightness and show ---
    FastLED.setBrightness(globalBrightness);
    FastLED.show();

    if (patternChanged) {
        patternChanged = false;
    }

    // Small yield to prevent tight-loop issues
    FastLED.delay(5);
}