#include <M5CoreS3.h>
#include <SPI.h>
#include <SD.h>

// ==== SDピン設定 ====
#define SD_SPI_SCK_PIN  (36)
#define SD_SPI_MISO_PIN (35)
#define SD_SPI_MOSI_PIN (37)
#define SD_SPI_CS_PIN   (4)

// ==== WAVヘッダ構造体 ====
struct WAVHeader {
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

// ==== ダブルバッファリング設定 ====
static int16_t* stereoBuffer;
static int16_t* monoBuffer[2];  // 2つのバッファを交互に使用
static constexpr size_t BUFFER_SIZE = 16384;  // さらに増量（約1秒分 @ 16kHz）
static int currentBuffer = 0;

// ==== WAV再生関数（ダブルバッファリング版）====
void playWAVfromSD(const char* filePath) {
    File file = SD.open(filePath);
    if (!file) {
        Serial.printf("Failed to open %s\n", filePath);
        return;
    }

    // WAVヘッダ読み込み
    WAVHeader header;
    file.read((uint8_t*)&header, sizeof(WAVHeader));

    // ヘッダチェック
    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        Serial.println("Invalid WAV file!");
        file.close();
        return;
    }

    Serial.printf("SampleRate: %lu Hz, Bits: %d, Channels: %d\n",
                  header.sampleRate, header.bitsPerSample, header.numChannels);

    // スピーカー初期化
    M5.Speaker.begin();
    M5.Speaker.setVolume(200);
    M5.Mic.end();

    // === 最初のチャンクを読み込み ===
    size_t bytesToRead = BUFFER_SIZE * sizeof(int16_t) * header.numChannels;
    size_t bytesRead = file.read((uint8_t*)stereoBuffer, bytesToRead);
    if (bytesRead == 0) {
        Serial.println("No data to play!");
        file.close();
        return;
    }

    size_t samples = bytesRead / (sizeof(int16_t) * header.numChannels);

    // モノラル変換（バッファ0）
    for (size_t i = 0; i < samples; i++) {
        if (header.numChannels == 2) {
            int32_t mixed = ((int32_t)stereoBuffer[i * 2] + (int32_t)stereoBuffer[i * 2 + 1]) / 2;
            monoBuffer[0][i] = (int16_t)mixed;
        } else {
            monoBuffer[0][i] = stereoBuffer[i];
        }
    }

    // 最初のチャンク再生開始
    M5.Speaker.playRaw(monoBuffer[0], samples, header.sampleRate, false, 1, 0);
    currentBuffer = 1;  // 次はバッファ1を使う

    // === ダブルバッファリングループ ===
    while (file.available()) {
        // 次のチャンクを読み込み（現在再生中のバッファではない方へ）
        bytesRead = file.read((uint8_t*)stereoBuffer, bytesToRead);
        if (bytesRead == 0) break;

        samples = bytesRead / (sizeof(int16_t) * header.numChannels);

        // モノラル変換（次のバッファへ）
        for (size_t i = 0; i < samples; i++) {
            if (header.numChannels == 2) {
                int32_t mixed = ((int32_t)stereoBuffer[i * 2] + (int32_t)stereoBuffer[i * 2 + 1]) / 2;
                monoBuffer[currentBuffer][i] = (int16_t)mixed;
            } else {
                monoBuffer[currentBuffer][i] = stereoBuffer[i];
            }
        }

        // 前のチャンクの再生完了を待つ
        while (M5.Speaker.isPlaying()) {
            delay(1);
            M5.update();
        }

        // 次のチャンクを再生
        M5.Speaker.playRaw(monoBuffer[currentBuffer], samples, header.sampleRate, false, 1, 0);
        
        // バッファを切り替え（0→1→0...）
        currentBuffer = 1 - currentBuffer;
    }

    // 最後のチャンク再生完了を待つ
    while (M5.Speaker.isPlaying()) {
        delay(1);
        M5.update();
    }

    // 後処理
    M5.Speaker.end();
    M5.Mic.begin();
    file.close();

    Serial.println("Playback done.");
}

// ==== セットアップ ====
void setup() {
    M5.begin();
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.println("WAV Player - M5CoreS3");
    M5.Display.println("Double Buffering Mode");

    // ==== SD初期化 ====
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
        Serial.println("SD Card mount failed!");
        M5.Display.println("SD FAILED!");
        while (1) delay(100);
    }
    Serial.println("SD Card initialized.");
    M5.Display.println("SD OK");

    // ==== メモリ確保（ダブルバッファ）====
    stereoBuffer = (int16_t*)heap_caps_malloc(BUFFER_SIZE * 2 * sizeof(int16_t), MALLOC_CAP_8BIT);
    monoBuffer[0] = (int16_t*)heap_caps_malloc(BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
    monoBuffer[1] = (int16_t*)heap_caps_malloc(BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
    
    if (!stereoBuffer || !monoBuffer[0] || !monoBuffer[1]) {
        Serial.println("Failed to allocate buffers!");
        M5.Display.println("MEMORY ERROR!");
        while (1);
    }

    Serial.printf("Buffer allocated: %d samples x 2\n", BUFFER_SIZE);
    M5.Display.println("Playing...");

    // ==== WAVファイル再生 ====
    playWAVfromSD("/test.wav");
    
    M5.Display.println("Done!");
}

// ==== ループ ====
void loop() {
    M5.update();
    delay(100);
}