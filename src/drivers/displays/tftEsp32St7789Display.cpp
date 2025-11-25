#include "displayDriver.h"

// 确保使用的是 ST7789 且在 ESP32 上
#if defined(TFT_ESP32_ST7789)

#include <TFT_eSPI.h>

/* =========================================================
   注意：确保文件名与实际一致
   ========================================================= */
#include "media/images_480_230.h"       
#include "media/images_bottom_480_90.h" 

#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include <FastLED.h>
#include "drivers/storage/nvMemory.h"
#include "rotation.h"

#define G_WIDTH 480
#define G_HEIGHT 320
#define TOP_HEIGHT 230
#define BOT_HEIGHT 90

static uint8_t local_flipRotation(uint8_t rotation) {
  return (rotation + 2) % 4;
}

extern nvMemory nvMem;

#ifdef RGB_LED_PIN
CRGB leds[NUM_LEDS];
#endif

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite background = TFT_eSprite(&tft);

extern monitor_data mMonitor;
extern pool_data pData;
extern DisplayDriver *currentDisplayDriver;
extern bool invertColors;
extern TSettings Settings;

bool hasChangedScreen = true;
bool bottomScreenBlue = true;
char currentScreenIdx = 0;

extern unsigned long mPoolUpdate;

void tftEsp32St7789_Init();
void tftEsp32St7789_AlternateScreenState();
void tftEsp32St7789_AlternateRotation();
void tftEsp32St7789_LoadingScreen();
void tftEsp32St7789_SetupScreen();
void tftEsp32St7789_MinerScreen(unsigned long mElapsed);
void tftEsp32St7789_ClockScreen(unsigned long mElapsed);
void tftEsp32St7789_GlobalHashScreen(unsigned long mElapsed);
void tftEsp32St7789_BTCprice(unsigned long mElapsed);
void tftEsp32St7789_AnimateCurrentScreen(unsigned long frame);
void tftEsp32St7789_DoLedStuff(unsigned long frame);

bool createBackgroundSprite(int16_t wdt, int16_t hgt) {
  if (background.created()) background.deleteSprite();
  if (background.createSprite(wdt, hgt) == nullptr) {
    Serial.println("## Sprite Alloc Failed ##");
    return false;
  }
  background.setSwapBytes(true);
  render.setDrawer(background);
  render.setLineSpaceRatio(0.9);
  return true;
}

void tftEsp32St7789_Init(void)
{
  #ifdef TFT_BL
  if (TFT_BL != -1) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW); 
  }
  #endif

  tft.init();
  
  if (nvMem.loadConfig(&Settings)) {
    invertColors = Settings.invertColors;
  }
  
  tft.invertDisplay(invertColors);
  tft.setRotation(3); 
  tft.setSwapBytes(true);

  tft.writecommand(0x36); 
  tft.writedata(0xE8 | 0x08); 

  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers))) {
    Serial.println("Initialise error");
    return;
  }

  pinMode(16, OUTPUT);
  pinMode(17, OUTPUT);
  digitalWrite(16, HIGH);
  digitalWrite(17, HIGH);

  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;

  tft.fillScreen(TFT_BLACK);

  #ifdef TFT_BL
  if (TFT_BL != -1) {
    digitalWrite(TFT_BL, HIGH);
  }
  #endif
}

void tftEsp32St7789_AlternateScreenState(void)
{
  #ifdef TFT_BL
  if (TFT_BL != -1) {
    int screen_state = digitalRead(TFT_BL);
    digitalWrite(TFT_BL, !screen_state);
  }
  #endif
}

void tftEsp32St7789_AlternateRotation(void)
{
  tft.setRotation(local_flipRotation(tft.getRotation()));
  hasChangedScreen = true;
}

// =========================================================================
// 【核心修复】完美的切屏不卡顿逻辑
// 逻辑说明：如果正在切换屏幕(hasChangedScreen)，强制只绘图、不联网。
// 联网操作顺延到下一帧执行，这样界面会瞬间显示完整，不再出现半截。
// =========================================================================
void printPoolDataSt7789() {
  bool needUpdateData = false;
  bool needDraw = false;
  
  // 计算是否到期
  bool timeExpired = (mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000);

  if (hasChangedScreen) {
     // 【策略 1】如果是切换屏幕瞬间
     // 无论数据是否过时，强制先用旧数据绘图，绝不在此刻联网
     needDraw = true;
     // needUpdateData 保持为 false，将联网推迟到下一帧（那时 hasChangedScreen 为 false）
  } 
  else if (timeExpired) {
     // 【策略 2】如果是普通帧且时间到了
     // 此时界面是稳定的，可以放心联网
     needUpdateData = true;
  }

  // 执行联网（阻塞操作，仅在非切屏状态下触发）
  if (needUpdateData) {
    if (Settings.PoolAddress != "tn.vkbit.com") {
        pData = getPoolData();
    }
    mPoolUpdate = millis();
    needDraw = true; 
  }

  // 执行绘图（快速）
  if (needDraw) {
    if (!createBackgroundSprite(G_WIDTH, BOT_HEIGHT)) return;
    int pushY = TOP_HEIGHT; 

    if (Settings.PoolAddress != "tn.vkbit.com") {
        if (bottomScreenBlue) {
           background.pushImage(0, 0, G_WIDTH, BOT_HEIGHT, bottonPoolScreen);
           // 注意：为了避免画面撕裂，如果刚才没 push 上半部分，这里确保只更新区域
           if(hasChangedScreen) tft.pushImage(0, pushY, G_WIDTH, BOT_HEIGHT, bottonPoolScreen);
        } 
        else {
           background.pushImage(0, 0, G_WIDTH, BOT_HEIGHT, bottonPoolScreen_g);
           if(hasChangedScreen) tft.pushImage(0, pushY, G_WIDTH, BOT_HEIGHT, bottonPoolScreen_g);
        }

        render.setDrawer(background);
        render.setAlignment(Align::TopCenter);
        render.setFontSize(30); 
        render.cdrawString(String(pData.workersCount).c_str(), 236, 51, TFT_BLACK);
        render.setFontSize(22);
        render.setAlignment(Align::BottomRight);
        render.cdrawString(pData.workersHash.c_str(), 400, 48, TFT_BLACK);
        render.setAlignment(Align::BottomLeft);
        render.cdrawString(pData.bestDifficulty.c_str(), 80, 48, TFT_BLACK);

        background.pushSprite(0, pushY);
    } else {
        if(hasChangedScreen) tft.fillRect(0, pushY, G_WIDTH, BOT_HEIGHT, TFT_DARKGREEN);
        background.fillSprite(TFT_DARKGREEN);
        background.setFreeFont(FF24); 
        background.setTextDatum(MC_DATUM); 
        background.setTextSize(1);
        background.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        background.drawString("TESTNET", G_WIDTH/2, BOT_HEIGHT/2, GFXFF);
        background.pushSprite(0, pushY);
    }
    
    background.deleteSprite();
  }
}

void tftEsp32St7789_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  
  if (hasChangedScreen) {
     // 1. 瞬间画出上半部分
     tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, MinerScreen);
  }
  
  // 2. 瞬间画出下半部分（基于上面的逻辑修改，此时不联网，直接画图）
  // 效果：屏幕在 100ms 内完全显示
  // 如果数据过期，联网更新会在下一帧自动进行
  printPoolDataSt7789();
  
  hasChangedScreen = false;
  
  int rightSpriteW = 190;
  int rightSpriteH = TOP_HEIGHT; 
  int rightSpriteX = 480 - rightSpriteW; 

  if (!createBackgroundSprite(rightSpriteW, rightSpriteH)) return;
  
  background.pushImage(-rightSpriteX, 0, G_WIDTH, TOP_HEIGHT, MinerScreen);
  int txtBaseX = -2;
  int offsetY = -5;
  render.setFontSize(22); 
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.totalMHashes.c_str(), rightSpriteW - 90, 190, TFT_BLACK);
  render.setFontSize(22);
  render.setAlignment(Align::TopLeft);
  render.drawString(data.templates.c_str(), txtBaseX, 35 + offsetY, 0xDEDB);
  render.drawString(data.bestDiff.c_str(), txtBaseX, 72 + offsetY, 0xDEDB);
  render.drawString(data.completedShares.c_str(), txtBaseX, 110 + offsetY, 0xDEDB);
  render.setFontSize(18);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.timeMining.c_str(), rightSpriteW - 30, 143, 0xDEDB);
  render.setFontSize(30);
  render.setAlignment(Align::TopCenter);
  render.drawString(data.valids.c_str(), rightSpriteW/2 + 50, 80, 0xDEDB);
  render.setFontSize(14);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.temp.c_str(), rightSpriteW - 120, 2, TFT_BLACK); 
  render.setFontSize(8);
  render.rdrawString(String(0).c_str(), rightSpriteW - 110, 2, TFT_BLACK); 
  render.setFontSize(14);
  render.rdrawString(data.currentTime.c_str(), rightSpriteW - 50, 2, TFT_BLACK); 

  background.pushSprite(rightSpriteX, 0);
  background.deleteSprite();


  int leftSpriteH = 100;
  int leftSpriteW = 260; 
  int leftSpriteY = 120;

  if (!createBackgroundSprite(leftSpriteW, leftSpriteH)) return;
  background.pushImage(0, -leftSpriteY, G_WIDTH, TOP_HEIGHT, MinerScreen);
  render.setFontSize(50); 
  render.setAlignment(Align::TopLeft);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 175, 30, TFT_BLACK);
  background.pushSprite(0, leftSpriteY);
  background.deleteSprite();
}


void tftEsp32St7789_ClockScreen(unsigned long mElapsed)
{
    if (hasChangedScreen) tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, minerClockScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    clock_data data = getClockData(mElapsed);
    
    if (!createBackgroundSprite(G_WIDTH, TOP_HEIGHT)) return;
    background.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, minerClockScreen);

    render.setFontSize(28);
    render.setFontColor(TFT_BLACK);
    render.setCursor(30, 150);
    render.rdrawString(data.currentHashRate.c_str(), 140, 170, TFT_BLACK);

    background.setFreeFont(FSSB9);
    background.setTextSize(1);
    background.setTextDatum(TL_DATUM);
    background.setTextColor(TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 300, 5, GFXFF);

    render.setFontSize(22);
    render.setAlignment(Align::BottomRight);
    render.rdrawString(data.blockHeight.c_str(), 380, 190, TFT_BLACK);

    background.setFreeFont(FF23);
    background.setTextSize(2); 
    background.setTextColor(0xDEDB, TFT_BLACK);
    background.drawString(data.currentTime.c_str(), 180, 60, GFXFF);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32St7789_GlobalHashScreen(unsigned long mElapsed)
{
    if (hasChangedScreen) tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, globalHashScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    coin_data data = getCoinData(mElapsed);

    if (!createBackgroundSprite(G_WIDTH, TOP_HEIGHT)) return;
    background.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, globalHashScreen);

    background.setFreeFont(FSSB9);
    background.setTextSize(1);
    background.setTextDatum(TL_DATUM);
    background.setTextColor(TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 290, 5, GFXFF);
    background.drawString(data.currentTime.c_str(), 400, 5, GFXFF);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32St7789_BTCprice(unsigned long mElapsed)
{
    if (hasChangedScreen) tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, priceScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    clock_data data = getClockData(mElapsed);

    if (!createBackgroundSprite(G_WIDTH, TOP_HEIGHT)) return;
    background.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, priceScreen);

    render.setFontSize(28);
    render.setCursor(30, 150); 
    render.setFontColor(TFT_BLACK);
    render.rdrawString(data.currentHashRate.c_str(), 140, 170, TFT_BLACK);

    render.setFontSize(22);
    render.rdrawString(data.blockHeight.c_str(), 380, 190, TFT_WHITE);

    background.setFreeFont(FF24);
    background.setTextDatum(TR_DATUM);
    background.setTextSize(2); 
    background.setTextColor(0xDEDB, TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 460, 80, GFXFF);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32St7789_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 0, 480, 255, initScreen);
  tft.setTextColor(TFT_BLACK);
}

void tftEsp32St7789_SetupScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 0, 480, 255, setupModeScreen);
}

void tftEsp32St7789_AnimateCurrentScreen(unsigned long frame)
{
}

void tftEsp32St7789_DoLedStuff(unsigned long frame)
{
  if (currentScreenIdx != currentDisplayDriver->current_cyclic_screen) {
      hasChangedScreen = true;
      currentScreenIdx = currentDisplayDriver->current_cyclic_screen;
  }
  switch (mMonitor.NerdStatus)
  {
      case NM_waitingConfig:
      digitalWrite(17, LOW); // LED encendido de forma continua
      digitalWrite(16, HIGH);
      break;

      case NM_Connecting:
      digitalWrite(17, HIGH);
      digitalWrite(16, LOW); // LED encendido de forma continua
      break;
  }
}

CyclicScreenFunction tftEsp32St7789CyclicScreens[] = {tftEsp32St7789_MinerScreen, tftEsp32St7789_ClockScreen, tftEsp32St7789_GlobalHashScreen, tftEsp32St7789_BTCprice};

DisplayDriver tftEsp32St7789DisplayDriver = {
    tftEsp32St7789_Init,
    tftEsp32St7789_AlternateScreenState,
    tftEsp32St7789_AlternateRotation,
    tftEsp32St7789_LoadingScreen,
    tftEsp32St7789_SetupScreen,
    tftEsp32St7789CyclicScreens,
    tftEsp32St7789_AnimateCurrentScreen,
    tftEsp32St7789_DoLedStuff,
    SCREENS_ARRAY_SIZE(tftEsp32St7789CyclicScreens),
    0,
    G_WIDTH,
    G_HEIGHT};
#endif