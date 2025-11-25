#include "displayDriver.h"

// 仅在定义了 TFT_ESP32_S3_BOARD 宏时编译此文件，适用于 ESP32-S3 开发板
#if defined(TFT_ESP32_S3_BOARD)

#include <TFT_eSPI.h>
#include "media/images_320_170.h"
#include "media/images_bottom_320_70.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include <FS.h>
// 引入标准 SD 库而不是 SD_MMC。
// SD_MMC 在某些引脚配置下会导致 ESP32-S3 内核崩溃重启。SPI 模式兼容性更好。
#include <SD.h> 
#include <FastLED.h>
#include "drivers/storage/nvMemory.h"
#include "rotation.h" // 确保包含屏幕旋转相关的定义

// 定义屏幕的分辨率常量
// 原始宽为 320，高为 240，但在后续的分块渲染中我们使用了 130 等小尺寸以节省内存
#define WIDTH 130 // 注意：这里定义的WIDTH并未用于全屏初始化，实际全屏是 320x240
#define HEIGHT 170 

// 定义 SD 卡的片选 (Chip Select) 引脚
// ESP32-S3 的默认 SPI SS 引脚视具体板子而定（通常是 GPIO 10, 34 或 5）
#ifndef SD_CS_PIN
  #define SD_CS_PIN SS
#endif

// [辅助函数] 本地旋转计算函数
// 解决编译时找不到 flipRotation 的问题。
// 作用是计算旋转 180 后的角度索引 (0-3)
static uint8_t local_flipRotation(uint8_t rotation) {
  return (rotation + 2) % 4;
}

extern nvMemory nvMem;

// LED 灯珠配置
#ifdef RGB_LED_PIN
CRGB leds[NUM_LEDS];
#endif

// 字体渲染器与 TFT 驱动实例
OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();

// 创建一个名为 background 的 Sprite (显存画布)
// 这里的策略是：不要创建全屏 Sprite，而是按需创建小 Sprite，画完即毁，以节省内存防止重启
TFT_eSprite background = TFT_eSprite(&tft);

// 外部变量声明（数据源）
extern monitor_data mMonitor; // 监控状态
extern pool_data pData;       // 矿池数据
extern DisplayDriver *currentDisplayDriver;
extern bool invertColors;     // 颜色反转标志
extern TSettings Settings;    // 系统设置

// 屏幕状态标志
bool hasChangedScreen = true; // 标记是否刚刚切换过屏幕，用于重绘静态背景
bool bottomScreenBlue = true; // 底部栏颜色风格切换
char currentScreenIdx = 0;    // 当前屏幕索引

// 函数前向声明
void tftEsp32S3_Init();
void tftEsp32S3_AlternateScreenState();
void tftEsp32S3_AlternateRotation();
void tftEsp32S3_LoadingScreen();
void tftEsp32S3_SetupScreen();
void tftEsp32S3_MinerScreen(unsigned long mElapsed);
void tftEsp32S3_ClockScreen(unsigned long mElapsed);
void tftEsp32S3_GlobalHashScreen(unsigned long mElapsed);
void tftEsp32S3_BTCprice(unsigned long mElapsed);
void tftEsp32S3_AnimateCurrentScreen(unsigned long frame);
void tftEsp32S3_DoLedStuff(unsigned long frame);
void printPoolData();

// ---------------- [核心函数] 显存动态分配管理 ----------------
// 这个函数是为了防止 S3 在没有 PSRAM 的情况下因显存不足而无限重启。
// 逻辑：如果旧的 Sprite 没删掉，先删掉 -> 尝试申请新内存 -> 失败报错/成功设置参数。
bool createBackgroundSprite(int16_t wdt, int16_t hgt) {
  // 1. 内存清理：确保没有残留的显存占用
  if (background.created()) background.deleteSprite();
  
  // 2. 尝试创建指定大小的画布
  // 如果内存不足，createSprite 会返回 NULL，此时必须中止绘制，否则后续操作会触发看门狗复位
  if (background.createSprite(wdt, hgt) == nullptr) {
    Serial.println("## Sprite Alloc Failed (Heap Low) ##");
    return false;
  }
  
  // 3. 设置 Sprite 属性
  // setSwapBytes(true) 对于纠正“红蓝颜色互换”问题至关重要
  background.setSwapBytes(true); 
  // 将字体渲染器绑定到当前的 Sprite 上
  render.setDrawer(background);
  render.setLineSpaceRatio(0.9);
  return true;
}

// ---------------- 初始化函数 ----------------
void tftEsp32S3_Init(void)
{
  tft.init();
  
  // 加载用户配置
  if (nvMem.loadConfig(&Settings)) {
    invertColors = Settings.invertColors;
  }
  
  // [修复反色] 根据你的硬件需求，对配置取反 (!Settings.invertColors)
  // 某些屏幕（如 IPS）和驱动芯片（ST7789/ILI9341）对反色指令的响应相反
  tft.invertDisplay(!invertColors);
  
  tft.setRotation(3);    // 设置为横屏模式
  tft.setSwapBytes(true); // 设置全局字节序

  // 初始化背光
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // 拉高电平点亮屏幕
#endif

  // 加载数字字体
  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers))) {
    Serial.println("Initialise error");
    return;
  }

  // 初始化状态指示灯 (WS2812)
#ifdef RGB_LED_PIN
  FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
  leds[0] = CRGB::Red; // 初始设为红色（未连接）
  FastLED.show();
#endif

  // [关键修复] 初始化 SD 卡
  // 强制使用 SPI 模式的 SD.begin，而非 SD_MMC。
  // 许多 S3 开发板并没有连接 MMC 专用引脚，调用 SD_MMC 会直接崩溃。
  Serial.println("Initializing SD (SPI Mode)...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Mount Failed");
  } else {
    Serial.println("SD Mounted");
  }

  // 初始化数据结构
  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;
}

// 切换背光开关（模拟息屏）
void tftEsp32S3_AlternateScreenState(void)
{
  int screen_state = digitalRead(TFT_BL);
  digitalWrite(TFT_BL, !screen_state);
}

// 屏幕旋转切换 (180度翻转)
void tftEsp32S3_AlternateRotation(void)
{
  // 调用之前定义的本地辅助函数 local_flipRotation
  tft.setRotation(local_flipRotation(tft.getRotation()));
  hasChangedScreen = true; // 标记屏幕已变动，需强制重绘背景
}

extern unsigned long mPoolUpdate; // 外部变量：记录上次更新矿池数据的时间

// ---------------- 底部矿池数据栏渲染 ----------------
void printPoolData()
{
  // 只有在切换屏幕 或 数据过期(超过指定分钟) 时才更新底部
  if ((hasChangedScreen) || (mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000))
  {
    if (Settings.PoolAddress != "tn.vkbit.com") // 非测试网逻辑
    {
      pData = getPoolData(); // 获取矿池数据
      
      // [分块渲染] 创建底部 Sprite (320x50)
      // 失败则直接返回，不进行后续操作
      if (!createBackgroundSprite(320, 50)) return;

      // 绘制底部背景图
      if (bottomScreenBlue) {
        // Y坐标设为 -20 是因为原始图片可能高度为 70，我们需要截取中间部分
        background.pushImage(0, -20, 320, 70, bottonPoolScreen);
        // 如果刚切换过屏幕，需要先把静态背景刷到屏幕对应位置，防止空隙闪烁
        if(hasChangedScreen) tft.pushImage(0, 170, 320, 20, bottonPoolScreen);
      } else {
        background.pushImage(0, -20, 320, 70, bottonPoolScreen_g);
        if(hasChangedScreen) tft.pushImage(0, 170, 320, 20, bottonPoolScreen_g);
      }

      render.setDrawer(background);
      render.setLineSpaceRatio(1);
      
      // [关键位置修复 1] 强制重置文字对齐方式
      // 之前在其他函数中可能设为了 BottomRight，导致这里的 "workersCount" 跑偏
      render.setAlignment(Align::TopCenter); 

      // [关键位置修复 2] 绘制矿工数量
      render.setFontSize(24);
      // (157, 16) 是参考代码中的准确坐标
      render.cdrawString(String(pData.workersCount).c_str(), 157, 16, TFT_BLACK);
      
      // 绘制右下角总算力
      render.setFontSize(18);
      render.setAlignment(Align::BottomRight);
      render.cdrawString(pData.workersHash.c_str(), 265, 14, TFT_BLACK);
      
      // 绘制左下角最佳难度
      render.setAlignment(Align::BottomLeft);
      render.cdrawString(pData.bestDifficulty.c_str(), 54, 14, TFT_BLACK);
      
      // [推屏] 将画好的 Sprite 推送到屏幕的 Y=190 位置
      background.pushSprite(0, 190);
      // [清理] 立即释放显存，给接下来的渲染腾出空间
      background.deleteSprite(); 
    }
    else // 测试网逻辑 (TESTNET)
    {
      pData.workersCount = 1;
      // 如果切屏，先清除区域
      if(hasChangedScreen) tft.fillRect(0, 170, 320, 70, TFT_DARKGREEN);
      
      if (!createBackgroundSprite(320, 40)) return;
      background.fillSprite(TFT_DARKGREEN);
      
      background.setFreeFont(FF24);
      background.setTextDatum(TL_DATUM);
      background.setTextSize(1);
      background.setTextColor(TFT_WHITE, TFT_DARKGREEN);
      background.drawString("TESTNET", 50, 0, GFXFF);
      
      background.pushSprite(0, 185);
      background.deleteSprite();
      mPoolUpdate = millis(); // 更新时间戳
    }
  }
}

// ---------------- [主界面] 挖矿状态屏幕 ----------------
// 策略：为了节省显存，不一次性渲染全屏。分为 "右侧数据区" 和 "左侧算力区" 两次渲染。
void tftEsp32S3_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed); // 获取挖矿实时数据
  
  // 1. 静态背景绘制 (不消耗显存)
  // 只有在首次进入此界面时，才直接向屏幕写入全屏背景图
  if (hasChangedScreen) {
    tft.pushImage(0, 0, MinerWidth, 170, MinerScreen);
  }
  
  // 2. 更新底部栏
  printPoolData();
  
  hasChangedScreen = false;
 
  int wdtOffset = 190; // 右侧区域的X轴偏移量 (320 - 130)
  
  // -------- 区域 1: 右侧数据列表 (Templates, Hashes等) --------
  // 创建 130x170 的 Sprite
  if(!createBackgroundSprite(130, 170)) return;
  
  // [技巧] 为了保持背景连贯，我们画入全屏背景图，但在X轴负偏移 -190
  // 这样 Sprite 里显示的就是图片右侧的那一部分
  background.pushImage(-190, 0, MinerWidth, 170, MinerScreen);
  
  // 绘制数据项，注意坐标都要减去 wdtOffset (190)
  
  // Total Hashes (总哈希量)
  render.setFontSize(18);
  render.setAlignment(Align::BottomRight); 
  render.rdrawString(data.totalMHashes.c_str(), 268-wdtOffset, 138, TFT_BLACK);

  // Block Templates (模版数量)
  render.setFontSize(18);
  render.setAlignment(Align::TopLeft);
  render.drawString(data.templates.c_str(), 189-wdtOffset, 20, 0xDEDB);
  
  // Best Diff (最佳难度)
  render.setAlignment(Align::TopLeft); 
  render.drawString(data.bestDiff.c_str(), 189-wdtOffset, 48, 0xDEDB);
  
  // 32Bit Shares (验证数量)
  render.setFontSize(18);
  render.drawString(data.completedShares.c_str(), 189-wdtOffset, 76, 0xDEDB);
  
  // Time (运行时间) - 右下对齐
  render.setFontSize(14);
  render.setAlignment(Align::BottomRight); 
  render.rdrawString(data.timeMining.c_str(), 315-wdtOffset, 104, 0xDEDB);

  // Valid Blocks (虽然极少有，但如果有爆块会显示) - 顶部居中
  render.setFontSize(24);
  render.setAlignment(Align::TopCenter);
  render.drawString(data.valids.c_str(), 290-wdtOffset, 56, 0xDEDB);

  // 温度
  render.setFontSize(10);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.temp.c_str(), 239-wdtOffset, 1, TFT_BLACK);

  render.setFontSize(4);
  render.rdrawString(String(0).c_str(), 244-wdtOffset, 3, TFT_BLACK);

  // 当前时间
  render.setFontSize(10);
  render.rdrawString(data.currentTime.c_str(), 286-wdtOffset, 1, TFT_BLACK);

  // 推送到屏幕 X=190 的位置
  background.pushSprite(190, 0);
  background.deleteSprite(); // 立即释放


  // -------- 区域 2: 左侧大字算力 (Hashrate) --------
  // 为了进一步省内存，只刷新左侧下半部分 190x80
  if(!createBackgroundSprite(190, 80)) return;

  // 背景向上负偏移 -90
  background.pushImage(0, -90, MinerWidth, 170, MinerScreen);

  // Hashrate 数值
  render.setFontSize(35);
  render.setCursor(19, 118);
  render.setAlignment(Align::TopLeft); 
  render.setFontColor(TFT_BLACK);
  // Y轴坐标 114 要减去背景的 Y 偏移 90 => 24
  render.rdrawString(data.currentHashRate.c_str(), 118, 114-90, TFT_BLACK);
  
  // 推送到屏幕 X=0, Y=90
  background.pushSprite(0, 90);
  background.deleteSprite(); 
}

// ---------------- 时钟界面 ----------------
void tftEsp32S3_ClockScreen(unsigned long mElapsed)
{
  // 刷新全屏背景到屏幕（仅首次）
  if (hasChangedScreen) tft.pushImage(0, 0, minerClockWidth, 170, minerClockScreen);
  printPoolData();
  hasChangedScreen = false;

  // 获取数据
  clock_data data = getClockData(mElapsed);

  // 对于 Clock 界面，结构较简单，可以使用全屏 Sprite (320x170) 
  // 如果此处内存依然紧张导致崩溃，可参照 MinerScreen 拆分渲染
  if(!createBackgroundSprite(minerClockWidth, 170)) return;

  background.pushImage(0, 0, minerClockWidth, 170, minerClockScreen);
  
  // 绘制小字算力
  render.setFontSize(25);
  render.setCursor(19, 122);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 94, 129, TFT_BLACK);

  // 绘制 BTC 价格 (使用 FreeFonts)
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.btcPrice.c_str(), 202, 3, GFXFF);

  // 绘制区块高度
  render.setFontSize(18);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.blockHeight.c_str(), 254, 140, TFT_BLACK);

  // 绘制大字时间
  background.setFreeFont(FF23);
  background.setTextSize(2);
  background.setTextColor(0xDEDB, TFT_BLACK);
  background.drawString(data.currentTime.c_str(), 130, 50, GFXFF);
  
  background.pushSprite(0, 0);
  background.deleteSprite();
}

// ---------------- 全球算力统计界面 ----------------
void tftEsp32S3_GlobalHashScreen(unsigned long mElapsed)
{
  if (hasChangedScreen) tft.pushImage(0, 0, globalHashWidth, 170, globalHashScreen);
  
  printPoolData();
  hasChangedScreen = false;
  
  coin_data data = getCoinData(mElapsed);

  if(!createBackgroundSprite(globalHashWidth, 170)) return;
  background.pushImage(0, 0, globalHashWidth, 170, globalHashScreen);

  // 顶部信息：价格、时间
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.btcPrice.c_str(), 198, 3, GFXFF);
  background.drawString(data.currentTime.c_str(), 268, 3, GFXFF);

  // 网络状态：Fee、Difficulty
  background.setFreeFont(FSS9);
  background.setTextDatum(TR_DATUM);
  background.setTextColor(0x9C92);
  background.drawString(data.halfHourFee.c_str(), 302, 52, GFXFF);
  background.drawString(data.netwrokDifficulty.c_str(), 302, 88, GFXFF);

  // 全球哈希率
  render.setFontSize(17);
  render.setAlignment(Align::BottomRight);
  render.rdrawString(data.globalHashRate.c_str(), 274, 145, TFT_BLACK);

  // 区块高度
  render.setFontSize(28);
  render.setAlignment(Align::BottomLeft);
  render.rdrawString(data.blockHeight.c_str(), 140, 104, 0xDEDB);

  // 进度条绘制 (根据进度画一个覆盖层)
  int x2 = 2 + (138 * data.progressPercent / 100);
  background.fillRect(2, 149, x2, 168, 0xDEDB);

  // 剩余区块数
  background.setTextFont(FONT2);
  background.setTextSize(1);
  background.setTextDatum(MC_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.remainingBlocks.c_str(), 72, 159, FONT2);

  background.pushSprite(0, 0);
  background.deleteSprite();
}

// ---------------- BTC 价格界面 ----------------
void tftEsp32S3_BTCprice(unsigned long mElapsed)
{
  if(hasChangedScreen) tft.pushImage(0, 0, priceScreenWidth, 170, priceScreen);
  printPoolData();
  hasChangedScreen = false;

  clock_data data = getClockData(mElapsed);

  if(!createBackgroundSprite(priceScreenWidth, 170)) return;
  background.pushImage(0, 0, priceScreenWidth, 170, priceScreen);

  // 小字算力
  render.setFontSize(25);
  render.setCursor(19, 122);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 94, 129, TFT_BLACK);

  // 区块高度
  render.setFontSize(18);
  render.rdrawString(data.blockHeight.c_str(), 254, 138, TFT_WHITE);

  // 时间
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.currentTime.c_str(), 222, 3, GFXFF);

  // 巨大的 BTC 价格
  background.setFreeFont(FF24);
  background.setTextDatum(TR_DATUM);
  background.setTextSize(1);
  background.setTextColor(0xDEDB, TFT_BLACK);
  background.drawString(data.btcPrice.c_str(), 300, 58, GFXFF);

  background.pushSprite(0, 0);
  background.deleteSprite();
}

// 启动加载画面
void tftEsp32S3_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 33, initWidth, initHeight, initScreen);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(CURRENT_VERSION, 24, 147, FONT2);
}

// 配网设置画面
void tftEsp32S3_SetupScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 33, setupModeWidth, setupModeHeight, setupModeScreen);
}

// 切屏动画预留 (目前为空)
void tftEsp32S3_AnimateCurrentScreen(unsigned long frame)
{
}

// LED 状态控制逻辑
// 根据矿机状态 (mMonitor.NerdStatus) 改变 WS2812 灯珠颜色
void tftEsp32S3_DoLedStuff(unsigned long frame)
{
  // 检测屏幕索引是否变化，以触发刷新标志
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

// 定义屏幕循环列表
CyclicScreenFunction tftEsp32S3CyclicScreens[] = {tftEsp32S3_MinerScreen, tftEsp32S3_ClockScreen, tftEsp32S3_GlobalHashScreen, tftEsp32S3_BTCprice};

// 驱动结构体定义
DisplayDriver tftEsp32S3DisplayDriver = {
    tftEsp32S3_Init,
    tftEsp32S3_AlternateScreenState,
    tftEsp32S3_AlternateRotation,
    tftEsp32S3_LoadingScreen,
    tftEsp32S3_SetupScreen,
    tftEsp32S3CyclicScreens,
    tftEsp32S3_AnimateCurrentScreen,
    tftEsp32S3_DoLedStuff,
    SCREENS_ARRAY_SIZE(tftEsp32S3CyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif