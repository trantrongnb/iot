/*
 * NHAN NUT DE THU LOA - ESP32-S3 + MAX98357A
 * Arduino-ESP32 3.x. Khong can Wi-Fi, camera hoac PSRAM.
 *
 * GPIO41 -> BCLK (tim); GPIO40 -> LRC/WS (xam); GPIO42 -> DIN (xanh).
 * ESP32 GND -> GND module. Loa -> OUT+ va OUT- (khong noi vao GND).
 * VIN cua MAX98357A can nguon on dinh trong dai dien ap module cho phep.
 *
 * HC-6136: OUT -> GPIO47 (BUTTON_PIN), VCC -> 3V3, GND -> GND.
 * Moi lan nhan nut: sin 1000 Hz trong 1 giay. Giu nut chi keu mot lan.
 * Can nha nut roi nhan lai. Nut giu luc khoi dong khong tu phat am.
 * Gui mau lien tuc trong ca luc im lang de giu clock I2S.
 * Serial 115200: '+' / '-' = tang/giam am luong; 's' = trang thai.
 */
#include <Arduino.h>
#include <ESP_I2S.h>
#include <driver/gpio.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Hay chon board ESP32S3 Dev Module"
#endif

constexpr int I2S_BCLK = 41;
constexpr int I2S_WS = 40;
constexpr int I2S_DOUT = 42;
constexpr int BUTTON_PIN = 47;  // Day vang OUT cua HC-6136 dang noi GPIO47.
constexpr int BUTTON_PRESSED_LEVEL = LOW;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr int32_t INITIAL_AMPLITUDE = 3000;
constexpr int32_t MIN_AMPLITUDE = 500;
constexpr int32_t MAX_AMPLITUDE = 6000;
constexpr uint32_t BEEP_DURATION_MS = 1000;
constexpr size_t BUFFER_FRAMES = 256;
constexpr uint32_t FADE_FRAMES = SAMPLE_RATE / 100;  // 10 ms, giam tieng tach o bien.
static_assert(MIN_AMPLITUDE > 0 && INITIAL_AMPLITUDE >= MIN_AMPLITUDE &&
              INITIAL_AMPLITUDE <= MAX_AMPLITUDE && MAX_AMPLITUDE <= 32767,
              "Gioi han am luong khong hop le");
static_assert(GPIO_IS_VALID_GPIO(BUTTON_PIN) && BUTTON_PIN != I2S_BCLK &&
              BUTTON_PIN != I2S_WS && BUTTON_PIN != I2S_DOUT,
              "BUTTON_PIN phai la GPIO hop le, khong trung chan loa");
static_assert(BUTTON_PRESSED_LEVEL == LOW || BUTTON_PRESSED_LEVEL == HIGH,
              "BUTTON_PRESSED_LEVEL phai la LOW hoac HIGH");

// 16 mau / chu ky o 16000 Hz -> am sin 1000 Hz.
constexpr int16_t SINE_TABLE[16] = {
  0, 12540, 23170, 30274, 32767, 30274, 23170, 12540,
  0, -12540, -23170, -30274, -32767, -30274, -23170, -12540
};

I2SClass audio;
bool audioReady = false;
bool buttonArmed = false;
int buttonRaw = HIGH;
int buttonStable = HIGH;
uint32_t buttonChangedAt = 0;
int32_t amplitude = INITIAL_AMPLITUDE;
int16_t samples[BUFFER_FRAMES * 2];

void resetButton() {
  buttonRaw = digitalRead(BUTTON_PIN);
  buttonStable = buttonRaw;
  buttonChangedAt = millis();
  buttonArmed = false;
}

bool buttonPressed() {
  const int raw = digitalRead(BUTTON_PIN);
  if (raw != buttonRaw) {
    buttonRaw = raw;
    buttonChangedAt = millis();
  }
  if (millis() - buttonChangedAt < BUTTON_DEBOUNCE_MS) return false;
  if (buttonRaw != buttonStable) {
    buttonStable = buttonRaw;
    Serial.printf("[NUT GPIO] GPIO%d=%s (%d) - %s\n", BUTTON_PIN,
                  buttonStable == HIGH ? "HIGH" : "LOW", buttonStable,
                  buttonStable == BUTTON_PRESSED_LEVEL ? "NHAN" : "NHA");
  }
  if (buttonRaw != BUTTON_PRESSED_LEVEL) {
    buttonArmed = true;
    return false;
  }
  if (!buttonArmed) return false;
  buttonArmed = false;
  return true;
}

void handleSerialCommands() {
  while (Serial.available()) {
    const char command = char(Serial.read());
    if (command == '+' || command == '-') {
      amplitude += command == '+' ? 500 : -500;
      amplitude = constrain(amplitude, MIN_AMPLITUDE, MAX_AMPLITUDE);
      Serial.printf("[AM LUONG] %ld/32767, ap dung tu bip tiep theo.\n", long(amplitude));
    } else if (command == 's' || command == 'S') {
      const int level = digitalRead(BUTTON_PIN);
      Serial.printf("[TRANG THAI] Nut GPIO%d=%s (%d), %s, armed=%d, am luong %ld/32767, BCLK=%d WS=%d DIN=%d\n",
                    BUTTON_PIN, level == HIGH ? "HIGH" : "LOW", level,
                    level == BUTTON_PRESSED_LEVEL ? "NHAN" : "NHA", int(buttonArmed),
                    long(amplitude), I2S_BCLK, I2S_WS, I2S_DOUT);
    }
  }
}

bool writeSamples(size_t frames) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(samples);
  const size_t total = frames * 2 * sizeof(int16_t);
  size_t sent = 0;
  while (sent < total) {
    const size_t n = audio.write(bytes + sent, total - sent);
    if (!n) {
      Serial.printf("[LOI] Ghi I2S that bai: %d\n", audio.lastError());
      audioReady = false;
      return false;
    }
    sent += n;
  }
  return true;
}

bool sendSegment(bool audible, uint32_t durationMs) {
  const uint32_t totalFrames = SAMPLE_RATE * durationMs / 1000;
  const int32_t toneAmplitude = amplitude;
  for (uint32_t sent = 0; sent < totalFrames;) {
    handleSerialCommands();
    const size_t frames = min(size_t(totalFrames - sent), BUFFER_FRAMES);
    for (size_t i = 0; i < frames; ++i) {
      const uint32_t index = sent + i;
      int32_t value = 0;
      if (audible) {
        value = int32_t(SINE_TABLE[index % 16]) * toneAmplitude / 32767;
        if (index < FADE_FRAMES)
          value = value * int32_t(index) / int32_t(FADE_FRAMES);
        const uint32_t tail = totalFrames - 1 - index;
        if (tail < FADE_FRAMES)
          value = value * int32_t(tail) / int32_t(FADE_FRAMES);
      }
      samples[i * 2] = int16_t(value);
      samples[i * 2 + 1] = int16_t(value);
    }
    if (!writeSamples(frames)) return false;
    sent += frames;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nTHU LOA 1000 Hz - BCLK=41, WS=40, DIN=42");
  pinMode(BUTTON_PIN, BUTTON_PRESSED_LEVEL == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  resetButton();
  Serial.printf("HC-6136: OUT -> GPIO%d, VCC -> 3V3, GND -> GND.\n", BUTTON_PIN);
  Serial.printf("[NUT GPIO] Ban dau GPIO%d=%s (%d); nhan du kien %s.\n",
                BUTTON_PIN, buttonRaw == HIGH ? "HIGH" : "LOW", buttonRaw,
                BUTTON_PRESSED_LEVEL == LOW ? "LOW" : "HIGH");
  Serial.println("I2S Philips, PCM16 stereo, 16000 Hz, cung tin hieu hai kenh.");
  audio.setPins(I2S_BCLK, I2S_WS, I2S_DOUT);
  audioReady = audio.begin(I2S_MODE_STD, SAMPLE_RATE,
                           I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                           I2S_STD_SLOT_BOTH);
  if (!audioReady) {
    Serial.printf("[LOI] Khoi tao I2S: %d\n", audio.lastError());
    return;
  }
  Serial.println("[OK] Nhan nut de phat bip. Giu nut chi phat mot lan, nha roi nhan tiep.");
  Serial.println("Serial: '+/-' chinh am luong, 's' xem trang thai nut va loa.");
  Serial.printf("Am luong ban dau: %ld/32767.\n", long(amplitude));
  sendSegment(false, 500);
}

void loop() {
  if (!audioReady) {
    delay(1000);
    return;
  }
  handleSerialCommands();
  if (buttonPressed()) {
    Serial.printf("[NUT] Phat bip 1000 Hz trong %lu ms, am luong %ld\n",
                  static_cast<unsigned long>(BEEP_DURATION_MS), long(amplitude));
    if (!sendSegment(true, BEEP_DURATION_MS)) return;
    resetButton();  // Khong xep hang cac lan nhan khi dang phat; cho nha nut.
    Serial.println("[SAN SANG] Nha nut roi nhan lai de phat tiep.");
  }
  sendSegment(false, 16);  // Im lang giua cac lan nhan, van giu clock I2S.
}
