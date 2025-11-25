#include "displayDriver.h"

// 仅在定义了 ST7789 版本的宏时编译
#if defined(TFT_ESP32_S3_ST7789)

#include <TFT_eSPI.h>
#include "media/images_480_170.h"
#include "media/images_bottom_320_70.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include <FastLED.h>
#include "drivers/storage/nvMemory.h"
#include "rotation.h"
// 使用标准 SD 库
#include <SD_MMC.h> 
#include <FS.h>

#define G_WIDTH 320
#define G_HEIGHT 240

// 如果没有定义 SD_CS_PIN，默认设为 SS
// #ifndef SD_CS_PIN
//   #define SD_CS_PIN SS
// #endif

// 本地旋转辅助函数，解决未定义问题
static uint8_t local_flipRotation(uint8_t rotation) {
  return (rotation + 2) % 4;
}

extern nvMemory nvMem;

#ifdef RGB_LED_PIN
CRGB leds[NUM_LEDS];
#endif

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();
// 使用指针或直接对象，这里用直接对象，但我们会动态管理内存
TFT_eSprite background = TFT_eSprite(&tft);

extern monitor_data mMonitor;
extern pool_data pData;
extern DisplayDriver *currentDisplayDriver;
extern bool invertColors;
extern TSettings Settings;

bool hasChangedScreen = true;
bool bottomScreenBlue = true;
char currentScreenIdx = 0;

// 函数声明
void tftEsp32S3St7789_Init();
void tftEsp32S3St7789_AlternateScreenState();
void tftEsp32S3St7789_AlternateRotation();
void tftEsp32S3St7789_LoadingScreen();
void tftEsp32S3St7789_SetupScreen();
void tftEsp32S3St7789_MinerScreen(unsigned long mElapsed);
void tftEsp32S3St7789_ClockScreen(unsigned long mElapsed);
void tftEsp32S3St7789_GlobalHashScreen(unsigned long mElapsed);
void tftEsp32S3St7789_BTCprice(unsigned long mElapsed);
void tftEsp32S3St7789_AnimateCurrentScreen(unsigned long frame);
void tftEsp32S3St7789_DoLedStuff(unsigned long frame);

// --- 核心函数：显存动态分配管理 ---
// 每次绘制前创建，绘制后立即删除，防止S3无PSRAM时重启
bool createBackgroundSprite(int16_t wdt, int16_t hgt) {
  if (background.created()) background.deleteSprite();
  
  if (background.createSprite(wdt, hgt) == nullptr) {
    Serial.println("## Sprite Alloc Failed ##");
    return false;
  }
  background.setSwapBytes(true); // ST7789 同样需要颜色字节交换
  render.setDrawer(background);
  render.setLineSpaceRatio(0.9);
  return true;
}

// --- 初始化 ---
void tftEsp32S3St7789_Init(void)
{
  tft.init();
  
  if (nvMem.loadConfig(&Settings)) {
    invertColors = Settings.invertColors;
  }
  
  // 修正反色逻辑，通常 ST7789 需要取反，如果颜色像底片请尝试去掉 !
  tft.invertDisplay(invertColors);
  
  // 设置旋转方向 (ST7789 常规横屏是 1 或 3)
  tft.setRotation(3); 
  tft.setSwapBytes(true);

  tft.writecommand(0x36); // MADCTL 寄存器
  tft.writedata(0xA0 | 0x08); // 0xA0是典型横屏，0x08是颜色翻转。如果是镜像了试试 0x68

#ifdef TFT_BL
  if (TFT_BL != -1) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }
#endif

  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers))) {
    Serial.println("Initialise error");
    return;
  }

#ifdef RGB_LED_PIN
  FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
  leds[0] = CRGB::Red;
  FastLED.show();
#endif

  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;

  // 初始化 SD (SPI)
  Serial.println("Init SD (SPI)...");
  if (!SD_MMC.begin()) {
    Serial.println("SD Failed");
  } else {
    Serial.println("SD OK");
  }
  
  // 注意：这里删除了原代码中的 background.createSprite，改为在各个屏幕函数中动态创建
}

void tftEsp32S3St7789_AlternateScreenState(void)
{
  #ifdef TFT_BL
  if (TFT_BL != -1) {
    int screen_state = digitalRead(TFT_BL);
    digitalWrite(TFT_BL, !screen_state);
  }
  #endif
}

void tftEsp32S3St7789_AlternateRotation(void)
{
  tft.setRotation(local_flipRotation(tft.getRotation()));
  hasChangedScreen = true;
}

extern unsigned long mPoolUpdate;

// --- 独立的底部数据绘制函数 (修复矿工对齐问题) ---
void printPoolDataSt7789() {
  if ((mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000) || hasChangedScreen) {
    // 逻辑判断放在绘图前
    if (Settings.PoolAddress != "tn.vkbit.com") {
        pData = getPoolData();
    } else {
        // Testnet default
    }

    // 创建 320x50 的底部 Sprite (避免全屏刷新重启)
    if (!createBackgroundSprite(320, 50)) return;

    if (Settings.PoolAddress != "tn.vkbit.com") {
        // 绘制图片
        if (bottomScreenBlue) {
           background.pushImage(0, -20, 320, 70, bottonPoolScreen); // 裁剪显示
           // 填补屏幕底部空隙
           if(hasChangedScreen) tft.pushImage(0, 170, 320, 20, bottonPoolScreen);
        } else {
           background.pushImage(0, -20, 320, 70, bottonPoolScreen_g);
           if(hasChangedScreen) tft.pushImage(0, 170, 320, 20, bottonPoolScreen_g);
        }

        // **修复对齐**: 重置 Alignment
        render.setDrawer(background);
        render.setAlignment(Align::TopCenter);
        
        render.setFontSize(24);
        // (157, 16) 相对 Sprite 顶部坐标，配合 TopCenter 完美居中对齐矿工数
        render.cdrawString(String(pData.workersCount).c_str(), 157, 16, TFT_BLACK);
        
        render.setFontSize(18);
        render.setAlignment(Align::BottomRight);
        render.cdrawString(pData.workersHash.c_str(), 265, 14, TFT_BLACK);
        
        render.setAlignment(Align::BottomLeft);
        render.cdrawString(pData.bestDifficulty.c_str(), 54, 14, TFT_BLACK);

        // 推送到屏幕 Y=190
        background.pushSprite(0, 190);
    } else {
        // Testnet
        if(hasChangedScreen) tft.fillRect(0, 170, 320, 70, TFT_DARKGREEN);
        background.fillSprite(TFT_DARKGREEN);
        background.setFreeFont(FF24);
        background.setTextDatum(TL_DATUM);
        background.setTextSize(1);
        background.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        background.drawString("TESTNET", 50, 0, GFXFF); // y=0 relative to sprite
        background.pushSprite(0, 185);
    }
    
    background.deleteSprite(); // 释放显存
    mPoolUpdate = millis();
  }
}

// --- 挖矿主屏幕 (使用分块刷新) ---
void tftEsp32S3St7789_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  
  if (hasChangedScreen) {
     tft.pushImage(0, 0, MinerWidth, 170, MinerScreen);
  }
  
  // 更新底部
  printPoolDataSt7789();
  
  hasChangedScreen = false;
  
  int wdtOffset = 190; // 对齐偏移量

  // 分块 1: 右侧数据区 (130x170)
  if (!createBackgroundSprite(130, 170)) return;
  
  // 画背景图，左移 190 像素
  background.pushImage(-190, 0, MinerWidth, 170, MinerScreen);
  
  // Total Hashes
  render.setFontSize(18);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.totalMHashes.c_str(), 268-wdtOffset, 138, TFT_BLACK);
  
  // Block Templates (Left Align)
  render.setFontSize(18);
  render.setAlignment(Align::TopLeft);
  render.drawString(data.templates.c_str(), 189-wdtOffset, 20, 0xDEDB);
  render.drawString(data.bestDiff.c_str(), 189-wdtOffset, 48, 0xDEDB);
  render.drawString(data.completedShares.c_str(), 189-wdtOffset, 76, 0xDEDB);
  
  // Time (Right Align)
  render.setFontSize(14);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.timeMining.c_str(), 315-wdtOffset, 104, 0xDEDB);

  // Valids (Center)
  render.setFontSize(24);
  render.setAlignment(Align::TopCenter);
  render.drawString(data.valids.c_str(), 290-wdtOffset, 56, 0xDEDB);

  // Temp
  render.setFontSize(10);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.temp.c_str(), 239-wdtOffset, 1, TFT_BLACK);
  render.setFontSize(4);
  render.rdrawString(String(0).c_str(), 244-wdtOffset, 3, TFT_BLACK);

  // Hour
  render.setFontSize(10);
  render.rdrawString(data.currentTime.c_str(), 286-wdtOffset, 1, TFT_BLACK);

  // 推送到右侧
  background.pushSprite(190, 0);
  background.deleteSprite();


  // 分块 2: 左侧算力区 (190x80) - 节约显存
  if (!createBackgroundSprite(190, 80)) return;
  background.pushImage(0, -90, MinerWidth, 170, MinerScreen);
  
  render.setFontSize(35);
  render.setCursor(19, 118);
  render.setAlignment(Align::TopLeft);
  render.setFontColor(TFT_BLACK);
  // 114 - 90 = 24 (Relative Y)
  render.rdrawString(data.currentHashRate.c_str(), 118, 114-90, TFT_BLACK);
  
  background.pushSprite(0, 90);
  background.deleteSprite();
}


void tftEsp32S3St7789_ClockScreen(unsigned long mElapsed)
{
    if (hasChangedScreen) tft.pushImage(0, 0, minerClockWidth, 170, minerClockScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    clock_data data = getClockData(mElapsed);
    
    // Clock Screen 一般能一次性放下，如果崩溃可参考 MinerScreen 拆分
    if (!createBackgroundSprite(minerClockWidth, 170)) return;
    background.pushImage(0, 0, minerClockWidth, 170, minerClockScreen);

    render.setFontSize(25);
    render.setFontColor(TFT_BLACK);
    render.setCursor(19, 122); // 重置 Cursor
    render.rdrawString(data.currentHashRate.c_str(), 94, 129, TFT_BLACK);

    background.setFreeFont(FSSB9);
    background.setTextSize(1);
    background.setTextDatum(TL_DATUM);
    background.setTextColor(TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 202, 3, GFXFF);

    render.setFontSize(18);
    render.setAlignment(Align::BottomRight);
    render.rdrawString(data.blockHeight.c_str(), 254, 140, TFT_BLACK);

    background.setFreeFont(FF23);
    background.setTextSize(2);
    background.setTextColor(0xDEDB, TFT_BLACK);
    background.drawString(data.currentTime.c_str(), 130, 50, GFXFF);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32S3St7789_GlobalHashScreen(unsigned long mElapsed)
{
    if (hasChangedScreen) tft.pushImage(0, 0, globalHashWidth, 170, globalHashScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    coin_data data = getCoinData(mElapsed);

    if (!createBackgroundSprite(globalHashWidth, 170)) return;
    background.pushImage(0, 0, globalHashWidth, 170, globalHashScreen);

    background.setFreeFont(FSSB9);
    background.setTextSize(1);
    background.setTextDatum(TL_DATUM);
    background.setTextColor(TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 198, 3, GFXFF);
    background.drawString(data.currentTime.c_str(), 268, 3, GFXFF);

    background.setFreeFont(FSS9);
    background.setTextDatum(TR_DATUM);
    background.setTextColor(0x9C92);
    background.drawString(data.halfHourFee.c_str(), 302, 52, GFXFF);
    background.drawString(data.netwrokDifficulty.c_str(), 302, 88, GFXFF);

    render.setFontSize(17);
    render.setAlignment(Align::BottomRight);
    render.rdrawString(data.globalHashRate.c_str(), 274, 145, TFT_BLACK);

    render.setFontSize(28);
    render.setAlignment(Align::BottomLeft);
    render.rdrawString(data.blockHeight.c_str(), 140, 104, 0xDEDB);

    int x2 = 2 + (138 * data.progressPercent / 100);
    background.fillRect(2, 149, x2, 168, 0xDEDB);

    background.setTextFont(FONT2);
    background.setTextSize(1);
    background.setTextDatum(MC_DATUM);
    background.setTextColor(TFT_BLACK);
    background.drawString(data.remainingBlocks.c_str(), 72, 159, FONT2);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32S3St7789_BTCprice(unsigned long mElapsed)
{
    if (hasChangedScreen) tft.pushImage(0, 0, priceScreenWidth, 170, priceScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    clock_data data = getClockData(mElapsed);

    if (!createBackgroundSprite(priceScreenWidth, 170)) return;
    background.pushImage(0, 0, priceScreenWidth, 170, priceScreen);

    render.setFontSize(25);
    render.setCursor(19, 122); 
    render.setFontColor(TFT_BLACK);
    render.rdrawString(data.currentHashRate.c_str(), 94, 129, TFT_BLACK);

    render.setFontSize(18);
    render.rdrawString(data.blockHeight.c_str(), 254, 138, TFT_WHITE);

    background.setFreeFont(FSSB9);
    background.setTextSize(1);
    background.setTextDatum(TL_DATUM);
    background.setTextColor(TFT_BLACK);
    background.drawString(data.currentTime.c_str(), 222, 3, GFXFF);

    background.setFreeFont(FF24);
    background.setTextDatum(TR_DATUM);
    background.setTextSize(1);
    background.setTextColor(0xDEDB, TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 300, 58, GFXFF);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32S3St7789_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 33, initWidth, initHeight, initScreen);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(CURRENT_VERSION, 24, 147, FONT2);
}

void tftEsp32S3St7789_SetupScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 33, setupModeWidth, setupModeHeight, setupModeScreen);
}

void tftEsp32S3St7789_AnimateCurrentScreen(unsigned long frame)
{
}

void tftEsp32S3St7789_DoLedStuff(unsigned long frame)
{
  if (currentScreenIdx != currentDisplayDriver->current_cyclic_screen) {
      hasChangedScreen = true;
      currentScreenIdx = currentDisplayDriver->current_cyclic_screen;
  }
#ifdef RGB_LED_PIN
  if (mMonitor.NerdStatus == NM_waitingConfig) {
    leds[0] = CRGB::Blue;   // 蓝色：等待配置
  } else{
    leds[0] = CRGB::Green;  // 绿色：正在挖矿
  }
  FastLED.show();
#endif
}

CyclicScreenFunction tftEsp32S3St7789CyclicScreens[] = {tftEsp32S3St7789_MinerScreen, tftEsp32S3St7789_ClockScreen, tftEsp32S3St7789_GlobalHashScreen, tftEsp32S3St7789_BTCprice};

DisplayDriver tftEsp32S3St7789DisplayDriver = {
    tftEsp32S3St7789_Init,
    tftEsp32S3St7789_AlternateScreenState,
    tftEsp32S3St7789_AlternateRotation,
    tftEsp32S3St7789_LoadingScreen,
    tftEsp32S3St7789_SetupScreen,
    tftEsp32S3St7789CyclicScreens,
    tftEsp32S3St7789_AnimateCurrentScreen,
    tftEsp32S3St7789_DoLedStuff,
    SCREENS_ARRAY_SIZE(tftEsp32S3St7789CyclicScreens),
    0,
    G_WIDTH,
    G_HEIGHT};
#endif