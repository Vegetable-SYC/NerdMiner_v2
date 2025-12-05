#include "displayDriver.h"

#if defined(TFT_ESP32_ST7789)

#include <TFT_eSPI.h>

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

// ****************************************************
// UI调试宏定义，当宏定义开启时，画布将绘制带颜色的框
// ****************************************************
// #define POOL_DEBUG
// #define MINER_SCREEN_DEBUG
// #define CLOCK_SCREEN_DEBUG
// #define GLOBAL_SCREEN_DEBUG
// #define BTCPRICE_SCREEN_DEBUG

#define G_WIDTH 480
#define G_HEIGHT 320
#define TOP_HEIGHT 230
#define BOT_HEIGHT 90

const int Blue_PIN = 17;
const int Green_PIN = 16;

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
  tft.setRotation(1); 
  tft.setSwapBytes(true);

  tft.writecommand(0x36); 
  tft.writedata(0x20 | 0x08); 

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

// *********************************************************************
// 挖矿底部界面
// *********************************************************************
void printPoolDataSt7789() {
  if (hasChangedScreen || mPoolUpdate == 0 || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000)) {
    if (Settings.PoolAddress != "tn.vkbit.com") {
        pData = getPoolData(); 
    }
    mPoolUpdate = millis();
    int pushY = TOP_HEIGHT; 

    if (Settings.PoolAddress != "tn.vkbit.com") {
        const uint16_t* currentBg = bottomScreenBlue ? bottonPoolScreen : bottonPoolScreen_g;
        int base_Y = TOP_HEIGHT; 

        if (hasChangedScreen) {
             tft.pushImage(0, base_Y, G_WIDTH, BOT_HEIGHT, currentBg);
        }

        // =====================================================================
        // 1、左侧 Best Difficulty
        // =====================================================================
        int diff_w = 120; int diff_h = 40;
        int diff_x = 20;  int diff_y = base_Y + 45;

        if (createBackgroundSprite(diff_w, diff_h)) {
            background.pushImage(-diff_x, -(diff_y - base_Y), G_WIDTH, BOT_HEIGHT, currentBg);
            render.setDrawer(background);
            render.setFontSize(22);
            render.setAlignment(Align::BottomLeft);
            render.cdrawString(pData.bestDifficulty.c_str(), 80 - diff_x, 278 - diff_y, TFT_BLACK);
            #ifdef POOL_DEBUG
              background.drawRect(0, 0, diff_w, diff_h, TFT_RED); // 调试用
            #endif
            background.pushSprite(diff_x, diff_y);
            background.deleteSprite();
        }

        // =====================================================================
        // 2、中间 Workers Count
        // =====================================================================
        int work_w = 90; int work_h = 50; 
        int work_x = 196; int work_y = base_Y + 45;

        if (createBackgroundSprite(work_w, work_h)) {
            background.pushImage(-work_x, -(work_y - base_Y), G_WIDTH, BOT_HEIGHT, currentBg);
            render.setDrawer(background);
            render.setFontSize(30); 
            render.setAlignment(Align::TopCenter);
            render.cdrawString(String(pData.workersCount).c_str(), 236 - work_x, 281 - work_y, TFT_BLACK);
            #ifdef POOL_DEBUG
              background.drawRect(0, 0, work_w, work_h, TFT_BLUE); // 调试用
            #endif
            background.pushSprite(work_x, work_y);
            background.deleteSprite();
        }

        // =====================================================================
        // 3、右侧 Workers Hash
        // =====================================================================
        int hash_w = 120; int hash_h = 40;
        int hash_x = 340; int hash_y = base_Y + 45;

        if (createBackgroundSprite(hash_w, hash_h)) {
            background.pushImage(-hash_x, -(hash_y - base_Y), G_WIDTH, BOT_HEIGHT, currentBg);
            render.setDrawer(background);
            render.setFontSize(22);
            render.setAlignment(Align::BottomRight);
            render.cdrawString(pData.workersHash.c_str(), 400 - hash_x, 278 - hash_y, TFT_BLACK);
            #ifdef POOL_DEBUG
              background.drawRect(0, 0, hash_w, hash_h, TFT_GREEN); // 调试用
            #endif
            background.pushSprite(hash_x, hash_y);
            background.deleteSprite();
        }

    } else {
        if (createBackgroundSprite(G_WIDTH, BOT_HEIGHT)) {
            background.fillSprite(TFT_DARKGREEN);
            background.setFreeFont(FF24); 
            background.setTextDatum(MC_DATUM); 
            background.setTextSize(1);
            background.setTextColor(TFT_WHITE, TFT_DARKGREEN);
            background.drawString("TESTNET", G_WIDTH/2, BOT_HEIGHT/2, GFXFF);
            background.pushSprite(0, pushY);
            background.deleteSprite();
        }
    }
  }
}

// *********************************************************************
// 挖矿主界面（第一个页面）
// *********************************************************************
void tftEsp32St7789_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  
  // 1. 同步底部栏（保证不闪烁）
  printPoolDataSt7789();
  
  // 2. 如果切屏，画底图
  if (hasChangedScreen) {
     tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, MinerScreen);
     hasChangedScreen = false;
  }
  
  // 定义坐标常量
  int rightSpriteW = 190;
  int rightX = 480 - rightSpriteW; // 290
  int txtBaseX = -2;
  int offsetY = -5;

  // -----------------------------------------------------------------------
  // 1、绘制温度、时间
  // -----------------------------------------------------------------------
  // 定义的框体参数
  int temp_picw = 100; // 框宽度
  int temp_pich = 25;  // 框高度
  int temp_picx = 335; // 框起始 X
  int temp_picy = 0;   // 框起始 Y
  
  if (createBackgroundSprite(temp_picw, temp_pich)) { 
    // 背景对齐
    background.pushImage(-temp_picx, -temp_picy, G_WIDTH, TOP_HEIGHT, MinerScreen);

    render.setFontSize(14);
    render.setAlignment(Align::BottomRight);

    render.rdrawString(data.temp.c_str(), 360 - temp_picx, 2, TFT_BLACK); 
    
    // 温度符号
    render.setFontSize(8);
    render.rdrawString("0", 370 - temp_picx, 2, TFT_BLACK); 
    
    // 时间
    render.setFontSize(14);
    render.rdrawString(data.currentTime.c_str(), 430 - temp_picx, 2, TFT_BLACK); 

    // 调试框
    #ifdef MINER_SCREEN_DEBUG
      background.drawRect(0, 0, background.width(), background.height(), TFT_RED);
    #endif
    
    background.pushSprite(temp_picx, 0);
    background.deleteSprite();
  }

  // -----------------------------------------------------------------------
  // 2、绘制Template
  // -----------------------------------------------------------------------
  // 定义的框体参数
  int templ_w = 100; // 框宽度
  int templ_h = 35;  // 框高度
  int templ_x = 290; // 框起始 X
  int templ_y = 25;  // 框起始 Y

  if (createBackgroundSprite(templ_w, templ_h)) {
      // 背景对齐
      background.pushImage(-templ_x, -templ_y, G_WIDTH, TOP_HEIGHT, MinerScreen);

      //设置字体大小和对齐方式
      render.setFontSize(22);
      render.setAlignment(Align::TopLeft);
      
      // 绘制  
      render.drawString(data.templates.c_str(), 
                        288 - templ_x, 
                        30 - templ_y, 
                        0xDEDB);

      // 调试框（蓝色）
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, background.width(), background.height(), TFT_BLUE);
      #endif

      background.pushSprite(templ_x, templ_y);
      background.deleteSprite();
  }

  // -----------------------------------------------------------------------
  // 3、绘制Best Difficulty
  // -----------------------------------------------------------------------
  // 定义的框体参数
  int bd_w = 100; int bd_h = 35;
  int bd_x = 290; int bd_y = 65;
  
  if (createBackgroundSprite(bd_w, bd_h)) {
      // 背景对齐
      background.pushImage(-bd_x, -bd_y, G_WIDTH, TOP_HEIGHT, MinerScreen);
      
      //设置字体大小和对齐方式
      render.setFontSize(22);
      render.setAlignment(Align::TopLeft);

      // 绘制
      render.drawString(data.bestDiff.c_str(), 288 - bd_x, 67 - bd_y, 0xDEDB);

      // 调试框 (绿色)
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, bd_w, bd_h, TFT_GREEN);
      #endif
      background.pushSprite(bd_x, bd_y);
      background.deleteSprite();
  }

  // -----------------------------------------------------------------------
  // 4、绘制 Valids Blocks
  // -----------------------------------------------------------------------
  // 定义的框体参数
  int val_w = 55; int val_h = 40;
  int val_x = 410; int val_y = 80;

  if (createBackgroundSprite(val_w, val_h)) {
      // 背景对齐
      background.pushImage(-val_x, -val_y, G_WIDTH, TOP_HEIGHT, MinerScreen);

      //设置字体大小和对齐方式
      render.setFontSize(30);
      render.setAlignment(Align::TopCenter);

      // 绘制
      render.drawString(data.valids.c_str(), 435 - val_x, 80 - val_y, 0xDEDB);

      // 调试框 (黄色)
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, val_w, val_h, TFT_YELLOW);
      #endif
      background.pushSprite(val_x, val_y);
      background.deleteSprite();
  }

  // -----------------------------------------------------------------------
  // 5、绘制 32Bits Shares
  // -----------------------------------------------------------------------
  // 定义的框体参数
  int cs_w = 100; int cs_h = 30;
  int cs_x = 290; int cs_y = 105;

  if (createBackgroundSprite(cs_w, cs_h)) {
      // 背景对齐
      background.pushImage(-cs_x, -cs_y, G_WIDTH, TOP_HEIGHT, MinerScreen);

      // 设置字体大小和对齐方式
      render.setFontSize(22);
      render.setAlignment(Align::TopLeft);
      render.drawString(data.completedShares.c_str(), 288 - cs_x, 105 - cs_y, 0xDEDB);

      // 调试框 (洋红)
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, cs_w, cs_h, TFT_MAGENTA);
      #endif
      background.pushSprite(cs_x, cs_y);
      background.deleteSprite();
  }

  // -----------------------------------------------------------------------
  // 6、绘制 Time Mining
  // -----------------------------------------------------------------------
  // 定义的框体参数
  int tm_w = 200; int tm_h = 30;
  int tm_x = 260; int tm_y = 140;

  if (createBackgroundSprite(tm_w, tm_h)) {
      // 背景对齐
      background.pushImage(-tm_x, -tm_y, G_WIDTH, TOP_HEIGHT, MinerScreen);

      // 设置字体大小和对齐方式
      render.setFontSize(18);
      render.setAlignment(Align::BottomRight); 
      render.rdrawString(data.timeMining.c_str(), 450 - tm_x, 143 - tm_y, 0xDEDB);

      // 调试框 (青色)
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, tm_w, tm_h, TFT_CYAN);
      #endif
      background.pushSprite(tm_x, tm_y);
      background.deleteSprite();
  }

  // =========================================================================
  // 7、Million Hashes
  // =========================================================================
  // 定义的框体参数
  int hash_w = 140; 
  int hash_h = 45;  
  int hash_x = 270; // 框的左上角 X (原 rightX)
  int hash_y = 180; // 框的左上角 Y (原 150)

  if (createBackgroundSprite(hash_w, hash_h)) {
      // 背景对齐
      background.pushImage(-hash_x, -hash_y, G_WIDTH, TOP_HEIGHT, MinerScreen);

      // 设置字体大小和对齐方式
      render.setFontSize(22);
      render.setAlignment(Align::BottomRight); 
      
      // 绘制
      render.rdrawString(data.totalMHashes.c_str(), 
                         390 - hash_x, 
                         190 - hash_y, 
                         TFT_BLACK);

      // 调试框 (橙色)
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, background.width(), background.height(), TFT_ORANGE);
      #endif

      background.pushSprite(hash_x, hash_y);
      background.deleteSprite();
  }

  // =========================================================================
  // 8、HashRate（左侧主算力）
  // =========================================================================
  // 定义的框体参数
  int mainHash_w = 170; 
  int mainHash_h = 90; 
  int mainHash_x = 20; 
  int mainHash_y = 140;

  if (createBackgroundSprite(mainHash_w, mainHash_h)) {
      // 背景对齐
      background.pushImage(-mainHash_x, -mainHash_y, G_WIDTH, TOP_HEIGHT, MinerScreen);

      // 设置字体大小和对齐方式
      render.setFontSize(50); 
      render.setAlignment(Align::TopLeft);
      render.setFontColor(TFT_BLACK);

      // 绘制
      render.rdrawString(data.currentHashRate.c_str(), 
                         175 - mainHash_x, 
                         150 - mainHash_y, 
                         TFT_BLACK);
      
      // 调试框 (紫色)
      #ifdef MINER_SCREEN_DEBUG
        background.drawRect(0, 0, background.width(), background.height(), TFT_PURPLE);
      #endif

      background.pushSprite(mainHash_x, mainHash_y);
      background.deleteSprite();
  }
}

// *********************************************************************
// 挖矿时钟界面（第二个页面）
// *********************************************************************
void tftEsp32St7789_ClockScreen(unsigned long mElapsed)
{
    // 处理底部栏
    printPoolDataSt7789();

    if (hasChangedScreen) {
        tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, minerClockScreen);
        hasChangedScreen = false;
    }

    clock_data data = getClockData(mElapsed);

    // =========================================================================
    // 1、左下角 [算力 HashRate]
    // =========================================================================
    
    // 定义的框体参数
    int hr_w = 160;
    int hr_h = 60; 
    int hr_x = 20; 
    int hr_y = 160;

    if (createBackgroundSprite(hr_w, hr_h)) {
        // 背景对齐
        background.pushImage(-hr_x, -hr_y, G_WIDTH, TOP_HEIGHT, minerClockScreen);
        
        // 设置字体大小和对齐方式
        render.setFontSize(38);
        render.setFontColor(TFT_BLACK);
        render.setAlignment(Align::BottomLeft); // 左对齐
        
        // 绘制
        render.drawString(data.currentHashRate.c_str(), 
                          25 - hr_x, 
                          210 - hr_y, 
                          TFT_BLACK);
        
        // 调试框 (红色)
        #ifdef CLOCK_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_RED);
        #endif
        
        background.pushSprite(hr_x, hr_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 2、右上角 [BTC 价格]
    // =========================================================================
    // 定义的框体参数
    int btc_w = 180; 
    int btc_h = 35;  
    int btc_x = 300; 
    int btc_y = 0;   

    if (createBackgroundSprite(btc_w, btc_h)) {
        // 背景对齐
        background.pushImage(-btc_x, -btc_y, G_WIDTH, TOP_HEIGHT, minerClockScreen);

        // 设置字体大小和对齐方式
        background.setFreeFont(FSSB12);
        background.setTextSize(1);
        background.setTextDatum(TR_DATUM); 
        background.setTextColor(TFT_BLACK);
        
        // --- BTC 价格绘制 ---
        background.drawString(data.btcPrice.c_str(), 
                              330 - btc_x, 
                              6 - btc_y, 
                              GFXFF);

        // 调试框 (蓝色)
        #ifdef CLOCK_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_BLUE);
        #endif

        background.pushSprite(btc_x, btc_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 3、右上角 [当前时间]
    // =========================================================================
    // 定义的框体参数
    int time_w = 250;
    int time_h = 120;
    int time_x = 220;
    int time_y = 45; 

    if (createBackgroundSprite(time_w, time_h)) {
        // 背景对齐
        background.pushImage(-time_x, -time_y, G_WIDTH, TOP_HEIGHT, minerClockScreen);

        // 设置字体大小和对齐方式
        background.setFreeFont(FF24);
        background.setTextSize(2); 
        background.setTextColor(0xDEDB, TFT_BLACK);
        background.setTextDatum(TR_DATUM); // 右上对齐
        
        // 绘制
        background.drawString(data.currentTime.c_str(), 
                              460 - time_x, 
                              70 - time_y, 
                              GFXFF);

        // 调试框 (青色)
        #ifdef CLOCK_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_CYAN);
        #endif

        background.pushSprite(time_x, time_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 4、右下角 [区块高度]
    // =========================================================================
    // 定义的框体参数
    int blk_w = 170; 
    int blk_h = 50;  
    int blk_x = 210; 
    int blk_y = 180; 

    if (createBackgroundSprite(blk_w, blk_h)) {
        // 背景对齐
        background.pushImage(-blk_x, -blk_y, G_WIDTH, TOP_HEIGHT, minerClockScreen);

        // 设置字体大小和对齐方式
        render.setFontSize(26);
        render.setAlignment(Align::BottomRight); // 右对齐
        
        // 绘制
        render.rdrawString(data.blockHeight.c_str(), 
                           370 - blk_x, 
                           188 - blk_y, 
                           TFT_BLACK);

        // 调试框 (绿色)
        #ifdef CLOCK_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_GREEN);
        #endif

        background.pushSprite(blk_x, blk_y);
        background.deleteSprite();
    }
}

// *********************************************************************
// 挖矿全球算力界面（第三个页面）
// *********************************************************************
void tftEsp32St7789_GlobalHashScreen(unsigned long mElapsed)
{
    // 处理底部栏
    printPoolDataSt7789();
    
    if (hasChangedScreen) {
        tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, globalHashScreen);
        hasChangedScreen = false;
    }

    coin_data data = getCoinData(mElapsed);

    // =========================================================================
    // 1、顶部拆分为左右两块
    // =========================================================================
    // 定义的框体参数
    int price_w = 100; int price_h = 30;
    int price_x = 300;  int price_y = 0;
    
    if (createBackgroundSprite(price_w, price_h)) {
        // 背景对齐
        background.pushImage(-price_x, -price_y, G_WIDTH, TOP_HEIGHT, globalHashScreen);
        
        // 设置字体大小和对齐方式
        background.setFreeFont(FSSB9);
        background.setTextSize(1);
        background.setTextDatum(TL_DATUM);
        background.setTextColor(TFT_BLACK);

        background.drawString(data.btcPrice.c_str(), 300 - price_x, 5 - price_y, GFXFF);
        
        // 调试框 (红色)
        #ifdef GLOBAL_SCREEN_DEBUG
          background.drawRect(0, 0, price_w, price_h, TFT_RED);
        #endif
        background.pushSprite(price_x, price_y);
        background.deleteSprite();
    }

    // 定义的框体参数
    int time_w = 50; int time_h = 30;
    int time_x = 420; int time_y = 0;

    if (createBackgroundSprite(time_w, time_h)) {
        // 背景对齐
        background.pushImage(-time_x, -time_y, G_WIDTH, TOP_HEIGHT, globalHashScreen);

        // 设置字体大小和对齐方式
        background.setTextDatum(TR_DATUM);
        background.drawString(data.currentTime.c_str(), 465 - time_x, 5 - time_y, GFXFF);

        // 调试框 (蓝色)
        #ifdef GLOBAL_SCREEN_DEBUG
          background.drawRect(0, 0, time_w, time_h, TFT_BLUE);
        #endif
        background.pushSprite(time_x, time_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 2、右侧中间信息 [手续费/难度]
    // =========================================================================
    // 定义的框体参数
    int info_w = 130; int info_h = 100;
    int info_x = 330; int info_y = 60; 

    if (createBackgroundSprite(info_w, info_h)) {
        // 背景对齐
        background.pushImage(-info_x, -info_y, G_WIDTH, TOP_HEIGHT, globalHashScreen);

        // 设置字体大小和对齐方式
        background.setFreeFont(FSS9);
        background.setTextColor(0x9C92);
        background.setTextDatum(TR_DATUM); // 右上对齐
        
        // 绘制
        background.drawString(data.halfHourFee.c_str(), 430 - info_x, 70 - info_y, GFXFF);
        background.drawString(data.netwrokDifficulty.c_str(), 430 - info_x, 120 - info_y, GFXFF);
        
        // 调试框 (绿色)
        #ifdef GLOBAL_SCREEN_DEBUG
          background.drawRect(0, 0, info_w, info_h, TFT_GREEN);
        #endif
        background.pushSprite(info_x, info_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 3、算力
    // =========================================================================
    // 定义的框体参数
    int hr_w = 140;
    int hr_h = 40; 
    int hr_x = 290;
    int hr_y = 190;

    if (createBackgroundSprite(hr_w, hr_h)) {
        // 背景对齐
        background.pushImage(-hr_x, -hr_y, G_WIDTH, TOP_HEIGHT, globalHashScreen);
        
        // 设置字体大小和对齐方式
        render.setFontSize(28);
        render.setFontColor(TFT_BLACK);
        render.setAlignment(Align::BottomLeft); // 左对齐
        
        // 绘制
        render.drawString(data.currentHashRate.c_str(), 
                          320 - hr_x, 
                          220 - hr_y, 
                          TFT_BLACK);
        
        // 调试框 (红色)
        #ifdef GLOBAL_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_RED);
        #endif
        background.pushSprite(hr_x, hr_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 4、左下角 [区块高度] 
    // =========================================================================
    // 定义的框体参数
    int blk_w = 160; int blk_h = 50;
    int blk_x = 30;  int blk_y = 140; 

    if (createBackgroundSprite(blk_w, blk_h)) {
        // 背景对齐
        background.pushImage(-blk_x, -blk_y, G_WIDTH, TOP_HEIGHT, globalHashScreen);
        
        // 设置字体大小和对齐方式
        render.setFontSize(28);
        render.setAlignment(Align::BottomLeft);
        
        // 绘制
        render.drawString(data.blockHeight.c_str(), 35 - blk_x, 180 - blk_y, 0xDEDB);
        
        // 调试框 (黄色)
        #ifdef GLOBAL_SCREEN_DEBUG
          background.drawRect(0, 0, blk_w, blk_h, TFT_YELLOW);
        #endif
        background.pushSprite(blk_x, blk_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 5、[全球算力 + 进度条 + 剩余区块]
    // =========================================================================
    // 定义的框体参数
    int ctr_x = 0;  
    int ctr_y = 200;
    int ctr_w = 220;
    int ctr_h = 30; 

    int abs_barX = 0;  
    int abs_barY = 200;
    int barFullWidth = 180;
    int barHeight = 30;    

    if (createBackgroundSprite(ctr_w, ctr_h)) {
      // 背景对齐
      background.pushImage(-ctr_x, -ctr_y, G_WIDTH, TOP_HEIGHT, globalHashScreen);
      
      // --- 1. 全球算力 ---
      // 设置字体大小和对齐方式
      render.setFontSize(22);
      render.setAlignment(Align::BottomLeft); 
      render.setFontColor(TFT_BLACK);
      
      // 绘制
      render.drawString(data.globalHashRate.c_str(), 
                        abs_barX - ctr_x, 
                        abs_barY - ctr_y, 
                        TFT_BLACK);

      // --- 2. 绘制进度条 ---
      int currentBarWidth = (barFullWidth * data.progressPercent / 100);
      if(currentBarWidth > barFullWidth) currentBarWidth = barFullWidth;
      
      // 绘制
      background.fillRect(abs_barX - ctr_x, 
                          abs_barY - ctr_y, 
                          currentBarWidth, 
                          barHeight, 
                          0xDEDB);

      // --- 3. 剩余区块 ---
      // 设置字体大小和对齐方式
      background.setFreeFont(FSSB9); 
      background.setTextSize(1);
      background.setTextDatum(MC_DATUM); // 中心对齐
      background.setTextColor(TFT_BLACK);
      
      // 绘制
      background.drawString(data.remainingBlocks.c_str(), 
                            (abs_barX + barFullWidth/2) - ctr_x, 
                            (abs_barY + barHeight/2) - ctr_y, 
                            GFXFF);
      
      // 调试框 (洋红色)
      #ifdef GLOBAL_SCREEN_DEBUG
        background.drawRect(0, 0, ctr_w, ctr_h, TFT_MAGENTA);
      #endif  
      background.pushSprite(ctr_x, ctr_y);
      background.deleteSprite();
    }
}

// *********************************************************************
// 挖矿比特币价格界面（第四个页面）
// *********************************************************************
void tftEsp32St7789_BTCprice(unsigned long mElapsed)
{
    printPoolDataSt7789();
    
    if (hasChangedScreen) {
        tft.pushImage(0, 0, G_WIDTH, TOP_HEIGHT, priceScreen);
        hasChangedScreen = false;
    }

    clock_data data = getClockData(mElapsed);

    // =========================================================================
    // 1、右上角 [时间]
    // =========================================================================
    // 定义的框体参数
    int time_w = 80; 
    int time_h = 30; 
    int time_x = 310;
    int time_y = 0;  

    if (createBackgroundSprite(time_w, time_h)) {
        // 背景对齐
        background.pushImage(-time_x, -time_y, G_WIDTH, TOP_HEIGHT, priceScreen);
        
        // 设置字体大小和对齐方式
        background.setFreeFont(FSSB12);
        background.setTextSize(1);
        background.setTextDatum(TL_DATUM); // 左上对齐
        background.setTextColor(TFT_BLACK);
        
        // 绘制
        background.drawString(data.currentTime.c_str(), 
                              315 - time_x, 
                              5 - time_y, 
                              GFXFF);

        // 调试框 (绿色)
        #ifdef BTCPRICE_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_GREEN);
        #endif
        background.pushSprite(time_x, time_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 2、中间右侧大价格 [BTC Price] 
    // =========================================================================
    // 定义的框体参数
    int price_w = 260; 
    int price_h = 80;  
    int price_x = 210; 
    int price_y = 60;  

    if (createBackgroundSprite(price_w, price_h)) {
        // 背景对齐
        background.pushImage(-price_x, -price_y, G_WIDTH, TOP_HEIGHT, priceScreen);

        // 设置字体大小和对齐方式
        background.setFreeFont(FF24); // 使用最大的数字字体
        background.setTextDatum(MR_DATUM); // 垂直居中，靠右
        background.setTextSize(2); 
        background.setTextColor(0xDEDB, TFT_BLACK);
        
        // 绘制
        background.drawString(data.btcPrice.c_str(), 
                              460 - price_x, 
                              100 - price_y, 
                              GFXFF);

        // 调试框 (蓝色)
        #ifdef BTCPRICE_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_BLUE);
        #endif
        background.pushSprite(price_x, price_y);
        background.deleteSprite();
    }

    // =========================================================================
    // 3、左下角 [算力 HashRate]
    // =========================================================================
    // 定义的框体参数
    int hr_w = 200;
    int hr_h = 60; 
    int hr_x = 20; 
    int hr_y = 160;

    if (createBackgroundSprite(hr_w, hr_h)) {
        // 背景对齐
        background.pushImage(-hr_x, -hr_y, G_WIDTH, TOP_HEIGHT, priceScreen);
        
        // 设置字体大小和对齐方式
        render.setFontSize(35); 
        render.setFontColor(TFT_BLACK);
        render.setAlignment(Align::BottomLeft); // 左对齐
        
        // 绘制
        render.drawString(data.currentHashRate.c_str(), 
                          25 - hr_x, 
                          210 - hr_y, 
                          TFT_BLACK);
        
        // 调试框 (红色)
        #ifdef BTCPRICE_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_RED);
        #endif
        
        background.pushSprite(hr_x, hr_y);
        background.deleteSprite();
    }
    
    // =========================================================================
    // 4、右下角 [区块高度]
    // =========================================================================
    // 定义的框体参数
    int blk_w = 160; 
    int blk_h = 40;
    int blk_x = 250; 
    int blk_y = 185; 

    if (createBackgroundSprite(blk_w, blk_h)) {
        // 背景对齐
        background.pushImage(-blk_x, -blk_y, G_WIDTH, TOP_HEIGHT, priceScreen);

        // 设置字体大小和对齐方式
        render.setFontSize(22);
        render.setFontColor(TFT_WHITE); 
        render.setAlignment(Align::BottomRight); 
        
        // 绘制
        render.rdrawString(data.blockHeight.c_str(), 
                           370 - blk_x, 
                           190 - blk_y, 
                           TFT_WHITE);
        
        // 调试框 (灰色)
        #ifdef BTCPRICE_SCREEN_DEBUG
          background.drawRect(0, 0, background.width(), background.height(), TFT_LIGHTGREY);
        #endif
        
        background.pushSprite(blk_x, blk_y);
        background.deleteSprite();
    }
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
  // 设置彩灯颜色
  switch (mMonitor.NerdStatus)
  {
      // 等待配置模式（蓝色）
      case NM_waitingConfig:
      digitalWrite(Blue_PIN, LOW); 
      digitalWrite(Green_PIN, HIGH);
      break;

      // 开始挖矿（绿色）
      case NM_Connecting:
      digitalWrite(Blue_PIN, HIGH);
      digitalWrite(Green_PIN, LOW);  
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