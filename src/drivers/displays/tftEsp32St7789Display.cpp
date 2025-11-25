#include "displayDriver.h"

// 确保使用的是 ST7789 且在 ESP32 上
#if defined(TFT_ESP32_ST7789)

#include <TFT_eSPI.h>

/* =========================================================
   注意：你需要用图片转换工具重新生成以下两个文件
   1. images_480_230.h -> 存放 MinerScreen 数组 (大小 480x230)
   2. images_bottom_480_90.h -> 存放 bottonPoolScreen 数组 (大小 480x90)
   ========================================================= */
// 假设你修改后的文件名如下，请根据实际文件名修改这里：
#include "media/images_480_230.h"       // 对应原来的 MinerScreen
#include "media/images_bottom_480_90.h" // 对应原来的 bottonPoolScreen

#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include <FastLED.h>
#include "drivers/storage/nvMemory.h"
#include "rotation.h"

// 修改分辨率定义
#define G_WIDTH 480
#define G_HEIGHT 320

// 定义新的分屏高度常量
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

// Functions
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
  tft.init();
  
  if (nvMem.loadConfig(&Settings)) {
    invertColors = Settings.invertColors;
  }
  
  tft.invertDisplay(invertColors);
  
  tft.setRotation(3); 
  tft.setSwapBytes(true);

  // 480x320 屏幕常用配置，解决颜色反转和镜像问题
  // 如果颜色不对，修改 0x08 为 0x00；如果镜像不对，修改 0xE8 为 0xA0 或其他
  tft.writecommand(0x36); // MADCTL
  tft.writedata(0xE8 | 0x08); 

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

  pinMode(16, OUTPUT);
  pinMode(17, OUTPUT);

  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;
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

extern unsigned long mPoolUpdate;

// 修改：适配 480x90 的底部信息栏
void printPoolDataSt7789() {
  if ((mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000) || hasChangedScreen) {
    if (Settings.PoolAddress != "tn.vkbit.com") {
        pData = getPoolData();
    }

    // 创建 480x90 的 Sprite
    if (!createBackgroundSprite(G_WIDTH, BOT_HEIGHT)) return;
    
    // 绘制Y坐标起点为 TOP_HEIGHT (230)
    int pushY = TOP_HEIGHT; 

    if (Settings.PoolAddress != "tn.vkbit.com") {
        if (bottomScreenBlue) {
           background.pushImage(0, 0, G_WIDTH, BOT_HEIGHT, bottonPoolScreen);
           if(hasChangedScreen) tft.pushImage(0, pushY, G_WIDTH, BOT_HEIGHT, bottonPoolScreen);
        } 
        else {
           background.pushImage(0, 0, G_WIDTH, BOT_HEIGHT, bottonPoolScreen_g);
           if(hasChangedScreen) tft.pushImage(0, pushY, G_WIDTH, BOT_HEIGHT, bottonPoolScreen_g);
        }

        render.setDrawer(background);
        
        // --- 调整文字位置 ---
        
        // 工人数量 (中间上方)
        render.setAlignment(Align::TopCenter);
        render.setFontSize(30); // 放大字体
        // 480的中心是240，适当偏移适配图片背景
        render.cdrawString(String(pData.workersCount).c_str(), 236, 51, TFT_BLACK);
        
        // 哈希率 (右下角)
        render.setFontSize(22);
        render.setAlignment(Align::BottomRight);
        render.cdrawString(pData.workersHash.c_str(), 400, 48, TFT_BLACK);
        
        // 难度 (左下角)
        render.setAlignment(Align::BottomLeft);
        render.cdrawString(pData.bestDifficulty.c_str(), 80, 48, TFT_BLACK);

        background.pushSprite(0, pushY);
    } else {
        if(hasChangedScreen) tft.fillRect(0, pushY, G_WIDTH, BOT_HEIGHT, TFT_DARKGREEN);
        background.fillSprite(TFT_DARKGREEN);
        background.setFreeFont(FF24); // 加大字体
        background.setTextDatum(MC_DATUM); // 居中
        background.setTextSize(1);
        background.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        background.drawString("TESTNET", G_WIDTH/2, BOT_HEIGHT/2, GFXFF);
        background.pushSprite(0, pushY);
    }
    
    background.deleteSprite();
    mPoolUpdate = millis();
  }
}

// 修改：适配 480x230 的主挖矿界面
void tftEsp32St7789_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  
  if (hasChangedScreen) {
     // 背景全图刷新，宽度480，高度230
     tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, MinerScreen);
  }
  
  printPoolDataSt7789();
  
  hasChangedScreen = false;
  
  /* 
     计算分屏区域: 
     假设原来的布局左右分割比例。现在480宽。
     左侧用来显示大算力图，右侧显示详情。
     我们设定右侧面板从 x=270 开始，宽度 210。
  */
  
  int rightSpriteW = 190;
  int rightSpriteH = TOP_HEIGHT; // 230
  int rightSpriteX = 480 - rightSpriteW; // x = 270

  // --- 右侧数据列表部分 ---
  if (!createBackgroundSprite(rightSpriteW, rightSpriteH)) return;
  
  // 技巧：偏移绘制图片，负x坐标等于该Sprite在屏幕的x坐标
  background.pushImage(-rightSpriteX, 0, G_WIDTH, TOP_HEIGHT, MinerScreen);
  
  // 文字基础X位置（相对Sprite内部）
  int txtBaseX = -2;
  // 顶部偏移量，因为图片拉伸了，所有文字下移
  int offsetY = -5;

  render.setFontSize(22); // 字体由18增大
  render.setAlignment(Align::BottomRight);
  // Total Hashrate 右下大数字
  render.rdrawString(data.totalMHashes.c_str(), rightSpriteW - 90, 190, TFT_BLACK);
  
  render.setFontSize(22);
  render.setAlignment(Align::TopLeft);
  
  // 中间的三行小字
  render.drawString(data.templates.c_str(), txtBaseX, 35 + offsetY, 0xDEDB);
  render.drawString(data.bestDiff.c_str(), txtBaseX, 72 + offsetY, 0xDEDB);
  render.drawString(data.completedShares.c_str(), txtBaseX, 110 + offsetY, 0xDEDB);
  
  // 运行时间
  render.setFontSize(18);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.timeMining.c_str(), rightSpriteW - 30, 143, 0xDEDB);

  // Valids (Accepted Shares) 顶部的绿色大字
  render.setFontSize(30);
  render.setAlignment(Align::TopCenter);
  render.drawString(data.valids.c_str(), rightSpriteW/2 + 50, 80, 0xDEDB);

  // 顶部状态栏（温度、时间）
  render.setFontSize(14);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.temp.c_str(), rightSpriteW - 120, 2, TFT_BLACK); // 温度
  render.setFontSize(8);
  render.rdrawString(String(0).c_str(), rightSpriteW - 110, 2, TFT_BLACK); // ℃符号

  render.setFontSize(14);
  render.rdrawString(data.currentTime.c_str(), rightSpriteW - 50, 2, TFT_BLACK); // 时间

  background.pushSprite(rightSpriteX, 0);
  background.deleteSprite();


  // --- 左下侧 大算力文字部分 ---
  // 原图逻辑是在左下角，高度较矮。现在高度增加到 230，我们大概在 y=120 处开始局部刷新
  int leftSpriteH = 100;
  int leftSpriteW = 260; // 宽一点
  int leftSpriteY = 120;

  if (!createBackgroundSprite(leftSpriteW, leftSpriteH)) return;
  // 扣图
  background.pushImage(0, -leftSpriteY, G_WIDTH, TOP_HEIGHT, MinerScreen);
  
  render.setFontSize(50); // 特大字体
  render.setAlignment(Align::TopLeft);
  render.setFontColor(TFT_BLACK);
  // 调整文字位置
  render.rdrawString(data.currentHashRate.c_str(), 175, 30, TFT_BLACK);
  
  background.pushSprite(0, leftSpriteY);
  background.deleteSprite();
}


// --- 下面的其他页面如果只是暂时过渡，必须更新pushImage尺寸防止错位，但内容布局需要单独再精修 ---

void tftEsp32St7789_ClockScreen(unsigned long mElapsed)
{
    // 修改尺寸适应屏幕
    if (hasChangedScreen) tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, minerClockScreen);
    printPoolDataSt7789();
    hasChangedScreen = false;

    clock_data data = getClockData(mElapsed);
    
    // 修改尺寸
    if (!createBackgroundSprite(G_WIDTH, TOP_HEIGHT)) return;
    background.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, minerClockScreen);

    // 下面的坐标可能需要微调，先大致缩放位置
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
    background.setTextSize(2); // 大一点
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

    // 简单更新位置，使其不完全重叠
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
    background.setTextSize(2); // 字号大一点
    background.setTextColor(0xDEDB, TFT_BLACK);
    background.drawString(data.btcPrice.c_str(), 460, 80, GFXFF);

    background.pushSprite(0, 0);
    background.deleteSprite();
}

void tftEsp32St7789_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  // 确保这里的 initWidth/initHeight 宏在其他地方也更新为了 480/320，或者手动写死
  tft.pushImage(0, 0, 480, 255, initScreen);
  tft.setTextColor(TFT_BLACK);
  // tft.drawString(CURRENT_VERSION, 36, 210, FONT2);
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