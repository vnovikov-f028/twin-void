#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "game_types.h"

// ============================================================
// ПЛАТА. Раскомментируй РОВНО один блок.
// ============================================================

//#define BOARD_T_DISPLAY_114
 #define BOARD_T_DISPLAY_S3

#if defined(BOARD_T_DISPLAY_114) && defined(BOARD_T_DISPLAY_S3)
  #error "Оставь включённым только один блок платы"
#elif defined(BOARD_T_DISPLAY_114)
  const int BUTTON_1 = 35;
  const int BUTTON_2 = 0;
  const int SCREEN_WIDTH  = 240;
  const int SCREEN_HEIGHT = 135;
  static void boardBacklightOn() {
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
  }
#elif defined(BOARD_T_DISPLAY_S3)
  const int BUTTON_1 = 14;
  const int BUTTON_2 = 0;
  const int SCREEN_WIDTH  = 320;
  const int SCREEN_HEIGHT = 170;
  static void boardBacklightOn() {
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);
  }
#else
  #error "Раскомментируй BOARD_T_DISPLAY_114 или BOARD_T_DISPLAY_S3"
#endif

const int MENU_TEXT = 2;
const int MENU_STEP = (SCREEN_HEIGHT - 36) / 4;
const int HUD_H = 14;
const int ESPNOW_CHANNEL = 1;

// About / донат (заполни своим)
const char *AUTHOR_NAME = "Volodymyr Novikov";
const char *DONATION_ADDR = "TLuB6sNT98YGjFdmvPkrfGBVkvpvdnQ5U1"; // USDT-TRC20
int aboutPage = 0;   // 0 = инфо, 1 = QR

const int WORLD = 1000;
const int HOST_WX = 60;
const int CLIENT_WX = 145;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

enum GameState {
  STATE_MENU, STATE_STORE, STATE_GARAGE,
  STATE_DEMO, STATE_COOP_LOBBY, STATE_COOP_GAME, STATE_GAMEOVER, STATE_LEVEL_DONE,
  STATE_ABOUT
};
GameState currentState = STATE_MENU;
int menuSelection = 0;
bool needShipMsg = false;

Preferences prefs;
int credits = 1000;
uint8_t ownedMask = 0;
int selectedShip = -1;
int garageIndex = 0;
int upgradeIndex = 0;
const int UPGRADE_COUNT = 4;
int armorLvl = 0;       // +1 HP за уровень, макс 5
int missilePodLvl = 0;  // слотов под ракеты, макс 8
int gunRateLvl = 0;     // +1 FIR за уровень, макс 3
int gunPowerLvl = 0;    // +1 урон пули за уровень, макс 3
int revivers = 0;
int rocketsHoming = 0;
int rocketsCluster = 0;
int rocketsHydra = 0;
int activeRocket = 0;   // 0 homing, 1 cluster, 2 hydra
int storeIndex = 0;
int matchCredits = 0;
int matchPayout = 0;
int matchKills = 0;

const int LEVEL_COUNT = 12;
const int BOSS_WX = 850;        // позиция босса по X (мир)
const int BOSS_HIT_W = 170;     // полуширина хитбокса босса (мир.ед., под спрайт)
const int BOSS_HIT_H = 220;     // полувысота хитбокса босса (мир.ед., под спрайт)
const int ACID_SPEED = 25;
int currentLevel = 0;
int lobbyLevel = 0;
int levelProgress = 0;   // сколько уровней пройдено локально
int peerProgress = -1;   // прогресс напарника (-1 = неизвестен)

// Босс: 0 = Медуза (кислота), 1 = Стручок (лазер)
bool bossActive = false;
bool bossDefeated = false;
int bossType = 0;
int bossCannon = 0;   // какая пушка Дредноута стреляет следующей
int bossHp = 0;
int bossMaxHp = 25;
int bossY = 0;
unsigned long bossLastShot = 0;
unsigned long bossInterval = 3500;
int pendingAcidShot = 0;

#define MAX_ACID 10
struct Acid { int x, y; int vx, vy; bool active; uint8_t kind; uint8_t dmg; };
Acid acids[MAX_ACID];

int remoteMaxHp = 3;
int remoteShip = -1;
bool remoteHpInit = false;
bool pendingHostShot = false;

const ShipDef SHIPS[3] = {
  {"BLADE",  "balance", 40, 350, 3},
  {"TITAN",  "heavy",   22, 520, 6},
  {"NEEDLE", "light",   62, 220, 2},
};
const int SHIP_PRICE = 999;
const int REVIVER_PRICE = 70;
const int ROCKET_HOMING_PRICE = 25;
const int ROCKET_CLUSTER_PRICE = 45;
const int ROCKET_HYDRA_PRICE = 30;
const int STORE_ITEMS = 4;

bool hasShip() { return selectedShip >= 0 && (ownedMask & (1 << selectedShip)); }

// Производные характеристики корабля с учётом прокачки
int playerMaxHp() { return hasShip() ? SHIPS[selectedShip].hull + armorLvl : 3; }
int playerFirBase() { return hasShip() ? (int)lround(1000.0f / SHIPS[selectedShip].fireMs) : 3; }
int playerFireMs() {
  if (!hasShip()) return 350;
  int fir = playerFirBase() + gunRateLvl;
  if (fir < 1) fir = 1;
  return 1000 / fir;
}
int playerBulletDmg() { return hasShip() ? 1 + gunPowerLvl : 1; }
int rocketCapacity() { return hasShip() ? missilePodLvl : 0; }

void loadEconomy() {
  prefs.begin("kolo", false);
  if (!prefs.isKey("credits")) {
    credits = 1000;
    ownedMask = 0;
    selectedShip = -1;
    revivers = 0;
    rocketsHoming = 0;
    rocketsCluster = 0;
    rocketsHydra = 0;
    activeRocket = 0;
    levelProgress = 0;
    armorLvl = 0;
    missilePodLvl = 0;
    gunRateLvl = 0;
    gunPowerLvl = 0;
    prefs.putInt("credits", credits);
    prefs.putUChar("owned", ownedMask);
    prefs.putChar("ship", -1);
    prefs.putInt("revivers", revivers);
    prefs.putInt("rkHoming", rocketsHoming);
    prefs.putInt("rkCluster", rocketsCluster);
    prefs.putInt("rkHydra", rocketsHydra);
    prefs.putInt("activeRk", activeRocket);
    prefs.putInt("armor", armorLvl);
    prefs.putInt("pod", missilePodLvl);
    prefs.putInt("gunRate", gunRateLvl);
    prefs.putInt("gunPow", gunPowerLvl);
  } else {
    credits = prefs.getInt("credits", 1000);
    ownedMask = prefs.getUChar("owned", 0);
    selectedShip = prefs.getChar("ship", -1);
    revivers = prefs.getInt("revivers", 0);
    rocketsHoming = prefs.getInt("rkHoming", 0);
    rocketsCluster = prefs.getInt("rkCluster", 0);
    rocketsHydra = prefs.getInt("rkHydra", 0);
    activeRocket = prefs.getInt("activeRk", 0);
    levelProgress = prefs.getInt("progress", 0);
    armorLvl = prefs.getInt("armor", 0);
    missilePodLvl = prefs.getInt("pod", 0);
    gunRateLvl = prefs.getInt("gunRate", 0);
    gunPowerLvl = prefs.getInt("gunPow", 0);
    if (selectedShip < -1 || selectedShip > 2) selectedShip = -1;
    if (selectedShip >= 0 && !(ownedMask & (1 << selectedShip))) selectedShip = -1;
  }
}

void saveEconomy() {
  prefs.putInt("credits", credits);
  prefs.putUChar("owned", ownedMask);
  prefs.putChar("ship", (int8_t)selectedShip);
  prefs.putInt("revivers", revivers);
  prefs.putInt("rkHoming", rocketsHoming);
  prefs.putInt("rkCluster", rocketsCluster);
  prefs.putInt("rkHydra", rocketsHydra);
  prefs.putInt("activeRk", activeRocket);
  prefs.putInt("progress", levelProgress);
  prefs.putInt("armor", armorLvl);
  prefs.putInt("pod", missilePodLvl);
  prefs.putInt("gunRate", gunRateLvl);
  prefs.putInt("gunPow", gunPowerLvl);
}

float blueprintAngle = 0;
float targetBlueprintAngle = 0;
bool subscreenReadyToExit = false;

bool isPeerConnected = false;
bool espNowReady = false;
unsigned long lastPingTime = 0;
unsigned long lastStateSend = 0;
uint8_t peerMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool isHost = false;
uint8_t txSeq = 0;

#define COLOR_BG       TFT_BLACK
#define COLOR_HUD_MAIN tft.color565(255, 150, 0)
#define COLOR_HUD_GLOW tft.color565(255, 220, 50)
#define COLOR_HUD_DIM  tft.color565(100, 50, 0)
#define COLOR_HUD_GRID tft.color565(50, 25, 0)
#define COLOR_SKY_TOP  tft.color565(44, 21, 84)
#define COLOR_SKY_MID  tft.color565(158, 42, 107)
#define COLOR_SKY_BOT  tft.color565(214, 76, 42)
#define COLOR_SUN      tft.color565(255, 218, 120)
#define COLOR_MOUNTAIN TFT_BLACK
#define COLOR_ICE_TOP   tft.color565(18, 40, 84)
#define COLOR_ICE_MID   tft.color565(40, 92, 150)
#define COLOR_ICE_BOT   tft.color565(78, 142, 196)
#define COLOR_TOX_TOP   tft.color565(16, 58, 40)
#define COLOR_TOX_MID   tft.color565(40, 116, 68)
#define COLOR_TOX_BOT   tft.color565(118, 182, 108)
#define COLOR_CRES_TOP  tft.color565(10, 12, 34)
#define COLOR_CRES_MID  tft.color565(24, 28, 58)
#define COLOR_CRES_BOT  tft.color565(44, 48, 82)
#define COLOR_LAVA_TOP  tft.color565(58, 12, 10)
#define COLOR_LAVA_MID  tft.color565(122, 32, 14)
#define COLOR_LAVA_BOT  tft.color565(172, 62, 22)
#define COLOR_LAVA_FIRE tft.color565(255, 120, 20)
#define COLOR_NORTH_TOP tft.color565(8, 34, 40)
#define COLOR_NORTH_MID tft.color565(16, 70, 78)
#define COLOR_NORTH_BOT tft.color565(30, 110, 96)
#define COLOR_DUNE_TOP  tft.color565(120, 40, 70)
#define COLOR_DUNE_MID  tft.color565(200, 70, 95)
#define COLOR_DUNE_BOT  tft.color565(240, 150, 130)
#define COLOR_AST_TOP   tft.color565(30, 20, 60)
#define COLOR_AST_MID   tft.color565(50, 40, 100)
#define COLOR_AST_BOT   tft.color565(30, 50, 110)
#define COLOR_ST_TOP    tft.color565(10, 20, 45)
#define COLOR_ST_MID    tft.color565(20, 40, 75)
#define COLOR_ST_BOT    tft.color565(30, 60, 105)
#define COLOR_DAWN_TOP  tft.color565(70, 30, 90)
#define COLOR_DAWN_MID  tft.color565(160, 70, 110)
#define COLOR_DAWN_BOT  tft.color565(235, 130, 130)
#define COLOR_GAS_TOP   tft.color565(40, 30, 80)
#define COLOR_GAS_MID   tft.color565(70, 50, 120)
#define COLOR_GAS_BOT   tft.color565(110, 80, 150)
#define COLOR_NIGHT_TOP tft.color565(30, 16, 64)
#define COLOR_NIGHT_MID tft.color565(58, 26, 96)
#define COLOR_NIGHT_BOT tft.color565(92, 42, 128)
#define COLOR_MOON      tft.color565(232, 224, 255)
#define COLOR_HOST     tft.color565(50, 220, 100)
#define COLOR_CLIENT   tft.color565(70, 200, 240)
#define COLOR_SAUCER   tft.color565(180, 50, 150)
#define COLOR_DRONE    tft.color565(70, 200, 240)

Player player;
Player remote;
bool remoteSeen = false;
unsigned long lastRemoteMs = 0;

#define MAX_BULLETS 8
Bullet bullets[MAX_BULLETS];

#define MAX_ROCKETS 14
struct Rocket { int x, y; bool active; bool fromRemote; uint8_t kind; uint8_t target; int splitAt; int vx, vy; };
Rocket rockets[MAX_ROCKETS];

#define MAX_EXPLOSIONS 4
struct Explosion { int x, y; uint8_t life; bool big; };
Explosion explosions[MAX_EXPLOSIONS];

const int ROCKET_SPEED = 32;
const int WORLD_MID = WORLD / 2;   // середина поля по X
int pendingRocketKind = -1;
uint8_t pendingEnemyFire = 0;      // битовая маска: кто из врагов выстрелил

#define MAX_SHARDS 12
struct Shard { int x, y; int vx, vy; bool active; bool fromRemote; };
Shard shards[MAX_SHARDS];

#define MAX_ENEMIES 4
Enemy enemies[MAX_ENEMIES];

#define NUM_STARS 20
struct Star { float x, y, speed; };
Star stars[NUM_STARS];

float timeCnt = 0;
unsigned long lastFrameTime = 0;
const int frameDelay = 33;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len);
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
#endif

// ============================================================
// RX-очередь: колбэк ESP-NOW выполняется в задаче WiFi и не должен
// мутировать игровое состояние напрямую. Он только копирует пакет,
// а разбор выполняется в loop() (см. processIncoming).
// ============================================================
#define RX_QUEUE_SIZE 16
struct RxItem {
  GamePacket pkt;
  uint8_t mac[6];
};
RxItem rxQueue[RX_QUEUE_SIZE];
volatile uint8_t rxHead = 0;
volatile uint8_t rxTail = 0;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

void enqueueRx(const GamePacket &pkt, const uint8_t *mac) {
  portENTER_CRITICAL(&rxMux);
  uint8_t next = (uint8_t)((rxHead + 1) % RX_QUEUE_SIZE);
  if (next != rxTail) {
    rxQueue[rxHead].pkt = pkt;
    memcpy(rxQueue[rxHead].mac, mac, 6);
    rxHead = next;
  }
  portEXIT_CRITICAL(&rxMux);
}

bool dequeueRx(GamePacket &pkt, uint8_t *mac) {
  bool ok = false;
  portENTER_CRITICAL(&rxMux);
  if (rxTail != rxHead) {
    pkt = rxQueue[rxTail].pkt;
    memcpy(mac, rxQueue[rxTail].mac, 6);
    rxTail = (uint8_t)((rxTail + 1) % RX_QUEUE_SIZE);
    ok = true;
  }
  portEXIT_CRITICAL(&rxMux);
  return ok;
}

void processIncoming();

// Явные прототипы (не полагаемся на автогенерацию Arduino)
int levelGoal(int level);
bool levelHasBoss(int level);
int levelIntensityPct(int level);
int spawnIntervalMs();
const char *levelName(int level);
void handleAboutInput();
void drawAbout();

void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void sendToPeer(const GamePacket &pkt) {
  if (!espNowReady) return;
  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t *dst = isPeerConnected ? peerMac : bcast;
  if (!isPeerConnected) addPeer(bcast);
  esp_now_send(dst, (const uint8_t *)&pkt, sizeof(pkt));
}

int sx(int wx) { return (int)((int32_t)wx * SCREEN_WIDTH / WORLD); }
int sy(int wy) {
  int area = SCREEN_HEIGHT - HUD_H;
  if (wy < 0) wy = 0;
  if (wy > WORLD) wy = WORLD;
  int v = HUD_H + (int)((int32_t)wy * area / WORLD);
  if (v > SCREEN_HEIGHT - 1) v = SCREEN_HEIGHT - 1; // не выходим за нижнюю границу спрайта
  if (v < HUD_H) v = HUD_H;
  return v;
}
uint8_t yToNet(int wy) {
  if (wy < 0) wy = 0;
  if (wy > WORLD) wy = WORLD;
  return (uint8_t)(wy * 255 / WORLD);
}
int yFromNet(uint8_t v) { return (int)v * WORLD / 255; }

void layoutShips() {
  int s = SCREEN_HEIGHT >= 160 ? 20 : 16;
  int maxHp = playerMaxHp();
  player = {HOST_WX, WORLD / 2, s, s * 10 / 16, COLOR_HOST, 0, maxHp, false, WORLD / 2, false};
  remote = {CLIENT_WX, WORLD / 2, s, s * 10 / 16, COLOR_CLIENT, 0, maxHp, false, WORLD / 2, false};
}

void finishMatch(bool coop) {
  // В кооперативе общий котёл делится 50/50 между игроками;
  // в одиночной игре игрок забирает весь котёл.
  matchPayout = coop ? matchCredits / 2 : matchCredits;
  credits += matchPayout;
  saveEconomy();
}

void resetMatch() {
  player.score = 0;
  remote.score = 0;
  matchCredits = 0;
  matchPayout = 0;
  matchKills = 0;
  remoteHpInit = false;
  remoteMaxHp = playerMaxHp();
  pendingHostShot = false;
  int maxHp = playerMaxHp();
  player.y = WORLD / 2;
  player.hp = maxHp;
  player.falling = false;
  player.dead = false;
  remote.y = WORLD / 2;
  remote.hp = maxHp;
  remote.falling = false;
  remote.dead = false;
  remoteSeen = false;
  remoteShip = -1;
  for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
  for (int i = 0; i < MAX_ROCKETS; i++) rockets[i].active = false;
  for (int i = 0; i < MAX_SHARDS; i++) shards[i].active = false;
  for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].life = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  for (int i = 0; i < MAX_ACID; i++) acids[i].active = false;
  bossActive = false;
  bossDefeated = false;
  bossHp = 0;
  bossLastShot = 0;
  bossCannon = 0;
  pendingAcidShot = 0;
  pendingRocketKind = -1;
  pendingEnemyFire = 0;
}

void spawnBulletAt(int x, int y, bool fromRemote, uint8_t dmg) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i] = {x, y, true, fromRemote, dmg};
      break;
    }
  }
}

void spawnRocketAt(int x, int y, bool fromRemote, uint8_t kind) {
  for (int i = 0; i < MAX_ROCKETS; i++) {
    if (!rockets[i].active) { rockets[i] = {x, y, true, fromRemote, kind, 255, 0, 0, 0}; break; }
  }
}

// Гидра: вылетает ОДНА большая ракета-носитель (kind 2) и через ~110 мир.ед. делится (kind 3)
void spawnHydra(int x, int y, bool fromRemote) {
  for (int i = 0; i < MAX_ROCKETS; i++) {
    // носитель летит совсем чуть-чуть, затем делится
    if (!rockets[i].active) { rockets[i] = {x, y, true, fromRemote, 2, 255, x + 30, 0, 0}; break; }
  }
}

void splitHydra(int x, int y, bool fromRemote) {
  int slots[MAX_ENEMIES], n = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].active) slots[n++] = i;
  // веер скоростей: -60, -36, -12, +12, +36, +60 градусов (скорость 32)
  int fvx[6] = {16, 26, 31, 31, 26, 16};
  int fvy[6] = {-28, -19, -7, 7, 19, 28};
  for (int p = 0; p < 6; p++) {
    int slot = -1;
    for (int i = 0; i < MAX_ROCKETS; i++) if (!rockets[i].active) { slot = i; break; }
    if (slot < 0) break;
    uint8_t tgt = 255;
    if (n > 0) tgt = (uint8_t)slots[p % n];
    // splitAt = точка, после которой часть прекращает разлёт и начинает наведение
    rockets[slot] = {x, y, true, fromRemote, 3, tgt, x + 55, fvx[p], fvy[p]};
  }
}

void spawnRocketEvent(int x, int y, bool fromRemote, uint8_t kind) {
  if (kind == 2) spawnHydra(x, y, fromRemote);
  else spawnRocketAt(x, y, fromRemote, kind);
}

bool bossHitBy(int x, int y, int padX, int padY) {
  return bossActive && abs(x - BOSS_WX) <= BOSS_HIT_W + padX &&
         abs(y - bossY) <= BOSS_HIT_H + padY;
}

void spawnExplosion(int x, int y, bool big) {
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (explosions[i].life == 0) { explosions[i] = {x, y, 5, big}; break; }
  }
}

void updateExplosions() {
  for (int i = 0; i < MAX_EXPLOSIONS; i++)
    if (explosions[i].life > 0) explosions[i].life--;
}

int nearestEnemyIndex(int x, int y) {
  int best = -1, bestd = 100000;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    int d = abs(enemies[i].x - x) + abs(enemies[i].y - y) / 2;
    if (d < bestd) { bestd = d; best = i; }
  }
  return best;
}

void awardKill(EnemyType t, bool fromRemote) {
  matchKills++;
  matchCredits += enemyCreditsFor(t);
  if (fromRemote) remote.score += 10; else player.score += 10;
}

void spawnShard(int x, int y, int vx, int vy, bool fromRemote) {
  for (int i = 0; i < MAX_SHARDS; i++) {
    if (!shards[i].active) { shards[i] = {x, y, vx, vy, true, fromRemote}; break; }
  }
}

// Взрыв ракеты. Кластерная дополнительно разлетается на 5 мелких частей.
void explodeRocket(int x, int y, uint8_t kind, bool fromRemote, bool applyDamage) {
  bool cluster = (kind == 1);
  spawnExplosion(x, y, cluster);
  if (cluster) {
    spawnShard(x, y, 32, 0, fromRemote);    // 1 прямо
    spawnShard(x, y, 28, -16, fromRemote);  // 2 под 30°
    spawnShard(x, y, 28, 16, fromRemote);
    spawnShard(x, y, 23, -23, fromRemote);  // 2 под 45°
    spawnShard(x, y, 23, 23, fromRemote);
  }
  if (!applyDamage) return;
  int rx, ry, dmg;
  if (cluster) { rx = 110; ry = 70; dmg = 5; }          // кластер: взрыв 5
  else if (kind == 3) { rx = 40; ry = 30; dmg = 1; }    // часть Гидры: 1
  else if (kind == 0) { rx = 55; ry = 40; dmg = 8; }    // самонаводящаяся: 8
  else { rx = 0; ry = 0; dmg = 0; }                     // носитель Гидры не бьёт
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    if (abs(enemies[i].x - x) < rx && abs(enemies[i].y - y) < ry) {
      if (enemies[i].type == ENEMY_MINE) {
        int mx = enemies[i].x, my = enemies[i].y;
        enemies[i].active = false;
        explodeMine(mx, my, fromRemote, 0);
      } else {
        enemies[i].hp -= dmg;
        if (enemies[i].hp <= 0) { enemies[i].active = false; awardKill(enemies[i].type, fromRemote); }
      }
    }
  }
  if (bossHitBy(x, y, rx, ry)) damageBoss(dmg, fromRemote);
}

void updateRockets(bool applyDamage) {
  for (int i = 0; i < MAX_ROCKETS; i++) {
    if (!rockets[i].active) continue;
    uint8_t k = rockets[i].kind;

    // Гидра-носитель: короткий полёт и деление (без урона)
    if (k == 2) {
      rockets[i].x += ROCKET_SPEED;
      bool hit = false;
      for (int e = 0; e < MAX_ENEMIES && !hit; e++) {
        if (!enemies[e].active) continue;
        if (rockets[i].x >= enemies[e].x - 20 && rockets[i].x <= enemies[e].x + 50 &&
            rockets[i].y >= enemies[e].y - 35 && rockets[i].y <= enemies[e].y + 35) hit = true;
      }
      if (bossHitBy(rockets[i].x, rockets[i].y, 0, 0)) hit = true;
      if (rockets[i].x >= rockets[i].splitAt || hit) {
        splitHydra(rockets[i].x, rockets[i].y, rockets[i].fromRemote);
        rockets[i].active = false;
      } else if (rockets[i].x > WORLD + 80) {
        rockets[i].active = false;
      }
      continue;
    }

    if (k == 3 && rockets[i].x < rockets[i].splitAt) {
      // Фаза разлёта веером: летим по своей траектории, не наводясь
      rockets[i].x += rockets[i].vx;
      rockets[i].y += rockets[i].vy;
      if (rockets[i].y < 20) rockets[i].y = 20;
      if (rockets[i].y > WORLD - 20) rockets[i].y = WORLD - 20;
    } else {
      rockets[i].x += ROCKET_SPEED;
      // Наведение: 0 — самонаводящаяся, 3 — часть Гидры после разлёта
      if (k == 0 || k == 3) {
        int t = -1;
        if (k == 3 && rockets[i].target < MAX_ENEMIES && enemies[rockets[i].target].active)
          t = rockets[i].target;                     // часть Гидры держит свою цель
        else
          t = nearestEnemyIndex(rockets[i].x, rockets[i].y);
        if (t >= 0) {
          if (rockets[i].y < enemies[t].y) rockets[i].y += 14;
          else if (rockets[i].y > enemies[t].y) rockets[i].y -= 14;
        }
      }
    }
    bool hit = false;
    for (int e = 0; e < MAX_ENEMIES && !hit; e++) {
      if (!enemies[e].active) continue;
      if (rockets[i].x >= enemies[e].x - 20 && rockets[i].x <= enemies[e].x + 50 &&
          rockets[i].y >= enemies[e].y - 35 && rockets[i].y <= enemies[e].y + 35) {
        hit = true;
      }
    }
    if (bossHitBy(rockets[i].x, rockets[i].y, 0, 0)) hit = true;
    // Кластерная детонирует и на середине поля, если никого не задела
    bool midField = (k == 1 && rockets[i].x >= WORLD_MID);
    if (hit || midField) {
      explodeRocket(rockets[i].x, rockets[i].y, k, rockets[i].fromRemote, applyDamage);
      rockets[i].active = false;
    } else if (rockets[i].x > WORLD + 80) {
      rockets[i].active = false;
    }
  }
}

void updateShards(bool applyDamage) {
  for (int i = 0; i < MAX_SHARDS; i++) {
    if (!shards[i].active) continue;
    shards[i].x += shards[i].vx;
    shards[i].y += shards[i].vy;
    if (shards[i].x > WORLD + 80 || shards[i].y < 0 || shards[i].y > WORLD) {
      shards[i].active = false;
      continue;
    }
    bool hit = false;
    for (int e = 0; e < MAX_ENEMIES && !hit; e++) {
      if (!enemies[e].active) continue;
      if (shards[i].x >= enemies[e].x - 20 && shards[i].x <= enemies[e].x + 40 &&
          shards[i].y >= enemies[e].y - 25 && shards[i].y <= enemies[e].y + 25) {
        hit = true;
        if (applyDamage) {
          if (enemies[e].type == ENEMY_MINE) {
            int mx = enemies[e].x, my = enemies[e].y;
            enemies[e].active = false;
            explodeMine(mx, my, shards[i].fromRemote, 0);
          } else {
            enemies[e].hp -= 2;
            if (enemies[e].hp <= 0) { enemies[e].active = false; awardKill(enemies[e].type, shards[i].fromRemote); }
          }
        }
      }
    }
    if (!hit && bossHitBy(shards[i].x, shards[i].y, 0, 0)) {
      if (applyDamage) damageBoss(2, shards[i].fromRemote);
      hit = true;
    }
    if (hit) shards[i].active = false;
  }
}

bool playerLaunchRocket() {
  if (rocketCapacity() <= 0) return false;               // нужен Launcher
  int have[3] = { rocketsHoming, rocketsCluster, rocketsHydra };
  int kind = -1;
  if (activeRocket >= 0 && activeRocket < 3 && have[activeRocket] > 0) kind = activeRocket;
  else for (int k = 0; k < 3; k++) if (have[k] > 0) { kind = k; break; }
  if (kind < 0) return false;
  if (kind == 0) rocketsHoming--;
  else if (kind == 1) rocketsCluster--;
  else rocketsHydra--;
  spawnRocketEvent(player.x + 40, player.y, false, (uint8_t)kind);
  pendingRocketKind = kind;
  saveEconomy();
  return true;
}

// ===== Босс «Медуза» =====
void spawnBoss() {
  bossActive = true;
  bossDefeated = false;
  if (currentLevel == 4) bossType = 1;
  else if (currentLevel == 6) bossType = 2;
  else if (currentLevel == 8) bossType = 3;
  else if (currentLevel == 10) bossType = 4;
  else if (currentLevel == 11) bossType = 5;
  else bossType = 0;
  if (bossType == 1) bossMaxHp = 100;
  else if (bossType == 2) bossMaxHp = 120;
  else if (bossType == 3) bossMaxHp = 150;
  else if (bossType == 4) bossMaxHp = 150;
  else if (bossType == 5) bossMaxHp = 200;
  else bossMaxHp = 60;
  bossInterval = (bossType == 0) ? 3500 : ((bossType >= 3) ? 769 : 1000); // FIR 1.3
  bossHp = bossMaxHp;
  bossY = WORLD / 2;
  bossLastShot = millis();
  bossCannon = 0;
}

void spawnAcid(int x, int y, int vx, int vy, uint8_t kind, uint8_t dmg) {
  for (int i = 0; i < MAX_ACID; i++)
    if (!acids[i].active) { acids[i] = {x, y, vx, vy, true, kind, dmg}; break; }
}

// Выстрел обычного врага (свои скорость/урон по типу)
void spawnEnemyShot(EnemyType t, int x, int y) {
  int vx = -ACID_SPEED, dmg = 1;
  uint8_t kind = 2;
  if (t == ENEMY_SICKLE || t == ENEMY_GUARDIAN) { vx = -28; dmg = 2; kind = 3; }
  spawnAcid(x, y, vx, 0, kind, dmg);
}

// Выстрел босса (Жнец/Ядро — быстрее; Колосс — наводящийся)
void spawnBossShot(int y) {
  int vx = -ACID_SPEED, dmg = 1;
  uint8_t kind = (uint8_t)bossType;
  if (bossType == 3 || bossType == 4) { vx = -28; kind = 3; }
  else if (bossType == 5) { vx = -35; kind = 4; }   // Колосс: наводящийся снаряд
  spawnAcid(BOSS_WX - 30, y, vx, 0, kind, dmg);
}

void damageBoss(int dmg, bool fromRemote) {
  if (!bossActive) return;
  bossHp -= dmg;
  if (bossHp <= 0) {
    bossHp = 0;
    bossActive = false;
    bossDefeated = true;
    matchCredits += 25;
    if (fromRemote) remote.score += 100; else player.score += 100;
  }
}

// Левитация босса (обе платы считают одинаково) + стрельба кислотой (авторитетная плата).
void updateBoss(bool authoritative) {
  if (!bossActive) return;
  if (!authoritative) return;   // клиент берёт bossY из WORLD, не считает локально
  bossY = WORLD / 2 + (int)(sin(timeCnt * 1.5f) * 230);
  if (bossY < 80) bossY = 80;
  if (bossY > WORLD - 80) bossY = WORLD - 80;
  if (millis() - bossLastShot > bossInterval) {
    bossLastShot = millis();
    int shotY = bossY;
    if (bossType == 1) { shotY += bossCannon ? 60 : -60; bossCannon = 1 - bossCannon; } // 2 пушки
    spawnBossShot(shotY);
    pendingAcidShot = 1;
  }
}

void updateAcids(bool applyDamage) {
  for (int i = 0; i < MAX_ACID; i++) {
    if (!acids[i].active) continue;
    if (acids[i].kind == 4) {
      // наводящийся снаряд Колосса
      int targetY = player.y;
      if (isHost && remoteSeen && !remote.dead &&
          abs(remote.y - acids[i].y) < abs(player.y - acids[i].y)) targetY = remote.y;
      if (acids[i].y < targetY) acids[i].vy += 3;
      else if (acids[i].y > targetY) acids[i].vy -= 3;
      if (acids[i].vy > 14) acids[i].vy = 14;
      if (acids[i].vy < -14) acids[i].vy = -14;
    }
    acids[i].x += acids[i].vx;
    acids[i].y += acids[i].vy;
    if (acids[i].x < -40 || acids[i].x > WORLD + 40 ||
        acids[i].y < -40 || acids[i].y > WORLD + 40) { acids[i].active = false; continue; }
    if (!applyDamage) continue;
    if (!player.falling && !player.dead &&
        abs(acids[i].x - player.x) < 40 && abs(acids[i].y - player.y) < 30) {
      acids[i].active = false;
      player.hp -= acids[i].dmg;
      if (player.hp <= 0) { player.falling = true; player.fallY = player.y; }
    } else if (!remote.falling && !remote.dead &&
        abs(acids[i].x - remote.x) < 40 && abs(acids[i].y - remote.y) < 30) {
      acids[i].active = false;
      remote.hp -= acids[i].dmg;
      if (remote.hp <= 0) { remote.falling = true; remote.fallY = remote.y; }
    }
  }
}

// Мина: взрыв радиусом, урон 2 игрокам, врагам и боссу в зоне.
// fromRemote — чей это «килл»; depth ограничивает цепную детонацию мин.
void explodeMine(int x, int y, bool fromRemote, int depth) {
  if (depth > 3) return;
  spawnExplosion(x, y, true);
  int rx = 90, ry = 60;
  if (!player.falling && !player.dead && abs(player.x - x) < rx && abs(player.y - y) < ry) {
    player.hp -= 2;
    if (player.hp <= 0) { player.falling = true; player.fallY = player.y; }
  }
  if (remoteSeen && !remote.falling && !remote.dead &&
      abs(remote.x - x) < rx && abs(remote.y - y) < ry) {
    remote.hp -= 2;
    if (remote.hp <= 0) { remote.falling = true; remote.fallY = remote.y; }
  }
  if (bossHitBy(x, y, rx, ry)) damageBoss(2, fromRemote);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    if (abs(enemies[i].x - x) < rx && abs(enemies[i].y - y) < ry) {
      if (enemies[i].type == ENEMY_MINE) {
        int mx = enemies[i].x, my = enemies[i].y;
        enemies[i].active = false;
        explodeMine(mx, my, fromRemote, depth + 1);   // цепная детонация
      } else {
        enemies[i].hp -= 2;
        if (enemies[i].hp <= 0) { enemies[i].active = false; awardKill(enemies[i].type, fromRemote); }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  boardBacklightOn();
  tft.init();
  tft.setRotation(1);
  spr.setColorDepth(16);
  if (spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT) == nullptr) {
    Serial.printf("FATAL: sprite %dx%d alloc failed (PSRAM=%d, freeHeap=%u)\n",
                  SCREEN_WIDTH, SCREEN_HEIGHT, (int)psramFound(), (unsigned)ESP.getFreeHeap());
    while (true) delay(1000);
  }
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  loadEconomy();
  layoutShips();
  for (int i = 0; i < NUM_STARS; i++) {
    stars[i] = {(float)random(0, SCREEN_WIDTH), (float)random(HUD_H, SCREEN_HEIGHT - 20), (float)random(8, 25) / 10.0f};
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    espNowReady = true;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
#else
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
#endif
    Serial.printf("ESP-NOW ready, channel %d\n", ESPNOW_CHANNEL);
  } else {
    Serial.println("ERROR: esp_now_init failed - coop disabled");
  }
}

void loop() {
  processIncoming();
  if (millis() - lastFrameTime < frameDelay) return;
  lastFrameTime = millis();
  timeCnt += 0.05f;
  blueprintAngle += (targetBlueprintAngle - blueprintAngle) * 0.15f;

  if (currentState == STATE_MENU) { handleMenuInput(); drawSciFiMenu(); }
  else if (currentState == STATE_DEMO) { handleGameInput(false); updateSolo(); drawGame(false); }
  else if (currentState == STATE_STORE) { handleStoreInput(); drawStore(); }
  else if (currentState == STATE_GARAGE) { handleGarageInput(); drawGarage(); }
  else if (currentState == STATE_COOP_LOBBY) { handleCoopLobbyInput(); updateCoopLobby(); drawCoopLobby(); }
  else if (currentState == STATE_COOP_GAME) { handleGameInput(true); updateCoopGame(); drawGame(true); }
  else if (currentState == STATE_GAMEOVER) { handleGameOverInput(); drawGameOver(); }
  else if (currentState == STATE_LEVEL_DONE) { handleLevelDoneInput(); drawLevelDone(); }
  else if (currentState == STATE_ABOUT) { handleAboutInput(); drawAbout(); }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void applyPeerPose(const GamePacket &p) {
  remote.x = isHost ? CLIENT_WX : HOST_WX;
  player.x = isHost ? HOST_WX : CLIENT_WX;

  // Хост — авторитет по падению/смерти клиента (считает remote.hp).
  // INPUT клиента НЕ должен перезаписывать remote.fallY и сбрасывать dead:
  // иначе хост каждый кадр откатывает падение назад и клиент падает бесконечно.
  if (!remote.falling && !remote.dead) {
    remote.y = yFromNet(p.y);
  }

  remoteSeen = true;
  lastRemoteMs = millis();

  if (p.flags & 0x01) {
    spawnBulletAt(remote.x + 40, remote.y, true, (uint8_t)(1 + p.senderGun));
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *mac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(GamePacket)) return;
  GamePacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  enqueueRx(pkt, mac);
}

void processIncomingPacket(const GamePacket &pkt, const uint8_t *mac) {
  if (currentState == STATE_COOP_LOBBY) {
    if (pkt.type == 1 || pkt.type == 4) peerProgress = pkt.progress;
    if (!isPeerConnected && (pkt.type == 1 || pkt.type == 4)) {
      memcpy(peerMac, mac, 6);
      addPeer(peerMac);
      isPeerConnected = true;
      uint8_t myMac[6];
      WiFi.macAddress(myMac);
      isHost = memcmp(myMac, peerMac, 6) < 0;
      player.x = isHost ? HOST_WX : CLIENT_WX;
      remote.x = isHost ? CLIENT_WX : HOST_WX;
      player.color = isHost ? COLOR_HOST : COLOR_CLIENT;
      remote.color = isHost ? COLOR_CLIENT : COLOR_HOST;
    }
    if (pkt.type == 4) { currentLevel = pkt.level; resetMatch(); currentState = STATE_COOP_GAME; }
    return;
  }
  if (currentState != STATE_COOP_GAME) return;

    if (pkt.type == 6) {
    // Реанимация: напарник поднял нас — восстанавливаем своё состояние
    player.dead = false;
    player.falling = false;
    player.hp = playerMaxHp();
  }
  else if (pkt.type == 3) {
    // Получено хостом от клиента
    applyPeerPose(pkt);
    remoteShip = pkt.senderShip > 0 ? (int)pkt.senderShip - 1 : -1;
    if (pkt.flags & 0x08) spawnRocketEvent(remote.x + 40, remote.y, true, pkt.rocketKind);
    if (pkt.senderHull > 0) {
      remoteMaxHp = pkt.senderHull;
      if (!remoteHpInit) { remote.hp = remoteMaxHp; remoteHpInit = true; }
    }
  }
  else if (pkt.type == 7 && !isHost) {
    // Хост сообщил о завершении уровня
    currentLevel = pkt.level;
    matchKills = pkt.kills;
    matchCredits = pkt.matchCredits;
    player.score = pkt.clientScore;
    remote.score = pkt.hostScore;
    levelProgress = max(levelProgress, currentLevel + 1);
    saveEconomy();
    finishMatch(true);
    currentState = STATE_LEVEL_DONE;
  }
  else if (pkt.type == 5 && !isHost) {
    // Получено клиентом от хоста (World Sync)
    remote.x = HOST_WX;
    player.x = CLIENT_WX;

    remote.falling = (pkt.flags & 0x02) != 0;
    if (remote.falling) remote.fallY = yFromNet(pkt.y);
    else remote.y = yFromNet(pkt.y);
    remote.dead = (pkt.flags & 0x04) != 0;
    remote.hp = pkt.senderHp;
    remoteShip = pkt.senderShip > 0 ? (int)pkt.senderShip - 1 : -1;
    if (pkt.flags & 0x01) spawnBulletAt(remote.x + 40, remote.y, true, (uint8_t)(1 + pkt.senderGun));
    if (pkt.flags & 0x08) spawnRocketEvent(remote.x + 40, remote.y, true, pkt.rocketKind);

        // Синхронизация здоровья и состояния клиента, посчитанных хостом
    player.hp = pkt.remoteHp;
    bool clientFalling = (pkt.remoteFlags & 0x02) != 0;
    bool clientDead = (pkt.remoteFlags & 0x04) != 0;
    if (player.dead) {
      // Уже мёртв: игнорируем falling (не откатываем падение и не «оживляемся»).
      // Встаём только если хост реально оживил — у него HP снова > 0.
      if (player.hp > 0) { player.dead = false; player.falling = false; }
    } else if (clientDead) {
      player.dead = true;
      player.falling = false;
    } else if (clientFalling) {
      if (!player.falling) { player.falling = true; player.fallY = player.y; }
    } else {
      player.dead = false;
      player.falling = false;
    }

    player.score = pkt.clientScore;
    remote.score = pkt.hostScore;
    matchCredits = pkt.matchCredits;
    matchKills = pkt.kills;
    currentLevel = pkt.level;
    bossActive = pkt.boss != 0;
    bossHp = pkt.bossHp;
    bossType = pkt.bossType;
    if (bossType == 1) bossMaxHp = 100;
    else if (bossType == 2) bossMaxHp = 120;
    else if (bossType == 3) bossMaxHp = 150;
    else if (bossType == 4) bossMaxHp = 150;
    else if (bossType == 5) bossMaxHp = 200;
    else bossMaxHp = 60;
    bossY = yFromNet(pkt.bossY);   // авторитетная позиция от хоста
    if (pkt.acidShot) {
      int shotY = bossY;
      if (bossType == 1) { shotY += bossCannon ? 60 : -60; bossCannon = 1 - bossCannon; }
      spawnBossShot(shotY);
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
      bool wasMine = enemies[i].active && enemies[i].type == ENEMY_MINE;
      int px = enemies[i].x, py = enemies[i].y;
      if (pkt.enemies[i].kind == 255 || pkt.enemies[i].hp == 0) {
        enemies[i].active = false;
        // мина исчезла — проигрываем взрыв локально
        if (wasMine && px > 0) spawnExplosion(px, py, true);
      } else {
        enemies[i].active = true;
        enemies[i].x = pkt.enemies[i].x;
        enemies[i].y = pkt.enemies[i].y;
        enemies[i].type = (EnemyType)pkt.enemies[i].kind;
        enemies[i].hp = pkt.enemies[i].hp;
      }
    }
    // Выстрелы врагов (Скат) — визуал на клиенте
    for (int i = 0; i < MAX_ENEMIES; i++)
      if (pkt.enemyFire & (1 << i)) spawnEnemyShot(enemies[i].type, enemies[i].x - 12, enemies[i].y);
    remoteSeen = true;
    lastRemoteMs = millis();
  }
}

void processIncoming() {
  GamePacket pkt;
  uint8_t mac[6];
  while (dequeueRx(pkt, mac)) processIncomingPacket(pkt, mac);
}

void drawDetailedBlueprint(float angleDeg) {
  int radius = SCREEN_HEIGHT * 58 / 100;
  int centerX = SCREEN_WIDTH + radius / 5;
  int centerY = -radius / 12;
  float rad = angleDeg * 3.14159265f / 180.0f;
  float cosA = cos(rad), sinA = sin(rad);
  spr.drawCircle(centerX, centerY, radius, COLOR_HUD_MAIN);
  spr.drawCircle(centerX, centerY, radius * 2 / 3, COLOR_HUD_DIM);
  spr.drawCircle(centerX, centerY, radius / 3, COLOR_HUD_DIM);
  spr.drawCircle(centerX, centerY, radius + radius / 10, COLOR_HUD_GRID);
  int arm = radius + radius / 7;
  spr.drawLine(centerX + (int)(arm * cosA), centerY + (int)(arm * sinA),
               centerX - (int)(arm * cosA), centerY - (int)(arm * sinA), COLOR_HUD_GLOW);
  spr.drawLine(centerX + (int)(arm * -sinA), centerY + (int)(arm * cosA),
               centerX - (int)(arm * -sinA), centerY - (int)(arm * cosA), COLOR_HUD_DIM);
  int tri = radius * 6 / 10;
  int p1x = centerX + (int)(tri * cosA), p1y = centerY + (int)(tri * sinA);
  float r2 = rad + 2.0944f, r3 = rad + 4.18879f;
  int p2x = centerX + (int)(tri * cos(r2)), p2y = centerY + (int)(tri * sin(r2));
  int p3x = centerX + (int)(tri * cos(r3)), p3y = centerY + (int)(tri * sin(r3));
  spr.drawLine(p1x, p1y, p2x, p2y, COLOR_HUD_MAIN);
  spr.drawLine(p2x, p2y, p3x, p3y, COLOR_HUD_MAIN);
  spr.drawLine(p3x, p3y, p1x, p1y, COLOR_HUD_MAIN);
  spr.fillCircle(p1x, p1y, 2, COLOR_HUD_GLOW);
  spr.fillCircle(p2x, p2y, 2, COLOR_HUD_GLOW);
  spr.fillCircle(p3x, p3y, 2, COLOR_HUD_GLOW);
}

void drawStaticHexagon() {
  int hx = SCREEN_WIDTH * 58 / 100;
  int hy = SCREEN_HEIGHT * 26 / 100;
  int hr = SCREEN_HEIGHT * 15 / 100;
  for (int i = 0; i < 6; i++) {
    float a1 = i * 1.04719755f;
    float a2 = (i + 1) * 1.04719755f;
    int x1 = hx + (int)(hr * cos(a1)), y1 = hy + (int)(hr * sin(a1));
    int x2 = hx + (int)(hr * cos(a2)), y2 = hy + (int)(hr * sin(a2));
    spr.drawLine(x1, y1, x2, y2, COLOR_HUD_DIM);
    spr.fillCircle(x1, y1, 1, COLOR_HUD_MAIN);
    spr.drawLine(hx, hy, x1, y1, COLOR_HUD_GRID);
  }
}

void handleMenuInput() {
  static bool b2 = true, b1 = true;
  if (digitalRead(BUTTON_2) == LOW) {
    if (b2) { menuSelection = (menuSelection + 1) % 4; targetBlueprintAngle += 45.0f; b2 = false; }
  } else b2 = true;

  if (digitalRead(BUTTON_1) == LOW) {
    if (b1) {
      if (menuSelection == 0) { currentState = STATE_STORE; subscreenReadyToExit = false; }
      else if (menuSelection == 1) { currentState = STATE_GARAGE; subscreenReadyToExit = false; garageIndex = hasShip() ? selectedShip : 0; upgradeIndex = 0; }
      else if (menuSelection == 2) {
        if (!hasShip()) needShipMsg = true;
        else { currentState = STATE_COOP_LOBBY; isPeerConnected = false; isHost = false; peerProgress = -1; subscreenReadyToExit = false; }
      }
      else { currentState = STATE_ABOUT; subscreenReadyToExit = false; aboutPage = 0; }
      b1 = false;
    }
  } else b1 = true;
}

void drawSciFiMenu() {
  spr.fillSprite(COLOR_BG);
  drawDetailedBlueprint(blueprintAngle);
  drawStaticHexagon();
  const char *items[4] = {"Store", "Garage", "Coop", "About"};
  int xPos = SCREEN_WIDTH / 13;
  for (int i = 0; i < 4; i++) {
    int yPos = 16 + i * MENU_STEP;
    spr.setTextSize(MENU_TEXT);
    if (i == menuSelection) {
      spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
      spr.drawString(items[i], xPos, yPos);
      spr.drawFastHLine(xPos - 10, yPos - 3, 7, COLOR_HUD_GLOW);
      spr.drawFastVLine(xPos - 10, yPos - 3, MENU_TEXT * 8 + 2, COLOR_HUD_GLOW);
    } else {
      spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
      spr.drawString(items[i], xPos, yPos);
    }
  }
  if (needShipMsg) {
    spr.setTextSize(1);
    spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
    spr.setCursor(xPos, SCREEN_HEIGHT - 12);
    spr.print("GARAGE: BUY A SHIP");
  }
  spr.pushSprite(0, 0);
}

void handleStoreInput() {
  static bool b2 = true;
  static unsigned long t0 = 0;
  static bool hold = false;
  if (!subscreenReadyToExit) {
    if (digitalRead(BUTTON_1) == HIGH && digitalRead(BUTTON_2) == HIGH) subscreenReadyToExit = true;
    return;
  }
  if (digitalRead(BUTTON_2) == LOW) {
    if (b2) { storeIndex = (storeIndex + 1) % STORE_ITEMS; b2 = false; }
  } else b2 = true;

  if (digitalRead(BUTTON_1) == LOW) {
    if (!hold) { hold = true; t0 = millis(); }
    else if (millis() - t0 > 700) {
      // выбранная строка ракеты становится активной для пуска
      if (storeIndex >= 1 && storeIndex <= 3) { activeRocket = storeIndex - 1; saveEconomy(); }
      currentState = STATE_MENU;
      delay(200);
    }
  } else {
    if (hold && millis() - t0 < 700) {
      int rkTotal = rocketsHoming + rocketsCluster + rocketsHydra;
      if (storeIndex == 0 && credits >= REVIVER_PRICE) {
        credits -= REVIVER_PRICE;
        revivers++;
        saveEconomy();
      } else if (storeIndex == 1 && credits >= ROCKET_HOMING_PRICE && rkTotal < rocketCapacity()) {
        credits -= ROCKET_HOMING_PRICE;
        rocketsHoming++;
        saveEconomy();
      } else if (storeIndex == 2 && credits >= ROCKET_CLUSTER_PRICE && rkTotal < rocketCapacity()) {
        credits -= ROCKET_CLUSTER_PRICE;
        rocketsCluster++;
        saveEconomy();
      } else if (storeIndex == 3 && credits >= ROCKET_HYDRA_PRICE && rkTotal < rocketCapacity()) {
        credits -= ROCKET_HYDRA_PRICE;
        rocketsHydra++;
        saveEconomy();
      }
    }
    hold = false;
  }
}

void drawReviverIcon(int cx, int cy) {
  int r = 22;
  for (int i = 0; i < 6; i++) {
    float a1 = i * 60.0f * 0.0174533f;
    float a2 = (i + 1) * 60.0f * 0.0174533f;
    int x1 = cx + (int)(r * cos(a1)), y1 = cy + (int)(r * sin(a1));
    int x2 = cx + (int)(r * cos(a2)), y2 = cy + (int)(r * sin(a2));
    spr.drawLine(x1, y1, x2, y2, COLOR_HUD_DIM);
  }
  spr.fillRect(cx - 3, cy - 11, 6, 22, COLOR_HOST);
  spr.fillRect(cx - 11, cy - 3, 22, 6, COLOR_HOST);
  spr.fillCircle(cx, cy, 3, COLOR_HUD_GLOW);
}

void drawHomingRocketIcon(int cx, int cy) {
  int bx = cx - 18, by = cy + 8, w = 20, h = 8;
  spr.fillRect(bx, by - h / 2, w, h, TFT_RED);
  spr.fillTriangle(bx + w, by - h / 2, bx + w + 8, by, bx + w, by + h / 2, TFT_WHITE);
  spr.fillTriangle(bx, by - h / 2, bx - 7, by - h, bx - 2, by - h / 2, COLOR_HUD_MAIN);
  spr.fillTriangle(bx, by + h / 2, bx - 7, by + h, bx - 2, by + h / 2, COLOR_HUD_MAIN);
  int tx = cx + 16, ty = cy - 10;
  spr.drawCircle(tx, ty, 7, COLOR_HUD_GLOW);
  spr.drawFastHLine(tx - 11, ty, 5, COLOR_HUD_GLOW);
  spr.drawFastHLine(tx + 7, ty, 5, COLOR_HUD_GLOW);
  spr.drawFastVLine(tx, ty - 11, 5, COLOR_HUD_GLOW);
  spr.drawFastVLine(tx, ty + 7, 5, COLOR_HUD_GLOW);
  for (int t = 0; t < 5; t++) spr.drawPixel(bx - 6 - t * 4, by + 4 + t * 3, COLOR_HUD_DIM);
}

void drawClusterRocketIcon(int cx, int cy) {
  int bx = cx - 18, by = cy + 6, w = 20, h = 8;
  spr.fillRect(bx, by - h / 2, w, h, COLOR_SAUCER);
  spr.fillTriangle(bx + w, by - h / 2, bx + w + 8, by, bx + w, by + h / 2, TFT_WHITE);
  spr.fillTriangle(bx, by - h / 2, bx - 7, by - h, bx - 2, by - h / 2, COLOR_HUD_MAIN);
  spr.fillTriangle(bx, by + h / 2, bx - 7, by + h, bx - 2, by + h / 2, COLOR_HUD_MAIN);
  int ox = bx + w + 10, oy = by;
  for (int k = 0; k < 6; k++) {
    float a = k * 60.0f * 0.0174533f;
    int lx = ox + (int)(14 * cos(a)), ly = oy + (int)(14 * sin(a));
    spr.drawLine(ox, oy, lx, ly, TFT_ORANGE);
    spr.fillCircle(lx, ly, 2, COLOR_HUD_GLOW);
  }
}

void drawHydraRocketIcon(int cx, int cy) {
  for (int k = 0; k < 6; k++) {
    float a = (-60 + k * 24) * 0.0174533f;
    int x0 = cx + (int)(8 * cos(a)), y0 = cy + (int)(8 * sin(a));
    int x1 = cx + (int)(26 * cos(a)), y1 = cy + (int)(26 * sin(a));
    spr.drawLine(x0, y0, x1, y1, COLOR_HUD_MAIN);
    spr.fillCircle(x1, y1, 2, COLOR_HUD_GLOW);
  }
  spr.fillCircle(cx, cy, 3, TFT_CYAN);
}

void drawStoreIconPanel(int idx) {
  int cx = SCREEN_WIDTH * 3 / 4;
  int cy = SCREEN_HEIGHT / 2 + 6;
  int hw = 40, hh = 34;
  spr.drawRect(cx - hw, cy - hh, hw * 2, hh * 2, COLOR_HUD_DIM);
  spr.drawFastHLine(cx - hw, cy - hh, 12, COLOR_HUD_MAIN);
  spr.drawFastVLine(cx - hw, cy - hh, 12, COLOR_HUD_MAIN);
  spr.drawFastHLine(cx + hw - 12, cy + hh, 12, COLOR_HUD_MAIN);
  spr.drawFastVLine(cx + hw, cy + hh - 12, 12, COLOR_HUD_MAIN);
  if (idx == 0) drawReviverIcon(cx, cy);
  else if (idx == 1) drawHomingRocketIcon(cx, cy);
  else if (idx == 2) drawClusterRocketIcon(cx, cy);
  else drawHydraRocketIcon(cx, cy);
}

void drawStore() {
  spr.fillSprite(COLOR_BG);
  int x = SCREEN_WIDTH / 13;
  spr.setTextSize(MENU_TEXT);
  spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
  spr.setCursor(x, 8);
  spr.print("STORE");
  spr.setTextSize(1);
  spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
  spr.setCursor(x, 30);
  spr.print("CR:");
  spr.print(credits);
  spr.print("  POD:");
  spr.print(missilePodLvl);

  const char *names[STORE_ITEMS] = {"REVIVER", "HOMING RK", "CLUSTER RK", "HYDRA"};
  const int prices[STORE_ITEMS] = {REVIVER_PRICE, ROCKET_HOMING_PRICE, ROCKET_CLUSTER_PRICE, ROCKET_HYDRA_PRICE};
  const int counts[STORE_ITEMS] = {revivers, rocketsHoming, rocketsCluster, rocketsHydra};
  for (int i = 0; i < STORE_ITEMS; i++) {
    int y = 50 + i * 15;
    spr.setTextColor(i == storeIndex ? COLOR_HUD_GLOW : COLOR_HUD_MAIN, COLOR_BG);
    spr.setCursor(x, y);
    spr.print(i == storeIndex ? "> " : "  ");
    spr.print(names[i]);
    spr.setCursor(x + 92, y);
    spr.print("x");
    spr.print(counts[i]);
    if (i >= 1 && (i - 1) == activeRocket) spr.print("*"); // активная для пуска
  }

  drawStoreIconPanel(storeIndex);

  spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
  spr.setCursor(SCREEN_WIDTH * 3 / 4 - 24, SCREEN_HEIGHT / 2 + 44);
  spr.print(prices[storeIndex]);
  spr.print(" CR");

  spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT - 12);
  spr.print("BTN2 next  BTN1: click=buy hold=exit");
  spr.pushSprite(0, 0);
}

void drawCrossMark(int x, int y, uint16_t col) {
  spr.drawFastHLine(x - 3, y, 7, col);
  spr.drawFastVLine(x, y - 3, 7, col);
}

void drawEllipseOutline(int cx, int cy, int rx, int ry, uint16_t col) {
  int px = cx + rx, py = cy;
  for (int i = 1; i <= 24; i++) {
    float a = i * 6.2831853f / 24.0f;
    int nx = cx + (int)(rx * cos(a));
    int ny = cy + (int)(ry * sin(a));
    spr.drawLine(px, py, nx, ny, col);
    px = nx; py = ny;
  }
}

void drawDashedH(int x0, int x1, int y, uint16_t col, int dash, int gap) {
  for (int x = x0; x < x1; x += dash + gap) spr.drawFastHLine(x, y, min(dash, x1 - x), col);
}

// Чертёж-каркас для гаража в стиле референса c0ZSp.jpg:
// неоновый wireframe, двойной контур, осевые линии, ядро-свечение.
void drawGarageArt(int idx, int cx, int cy) {
  int hw = 44, hh = 40;
  // осевая линия и чертёжные метки
  drawDashedH(cx - hw - 6, cx + hw + 6, cy, COLOR_HUD_DIM, 6, 5);
  drawCrossMark(cx - hw + 2, cy - hh + 2, COLOR_HUD_DIM);
  drawCrossMark(cx - hw + 2, cy + hh - 2, COLOR_HUD_DIM);
  drawCrossMark(cx + hw - 2, cy - hh + 2, COLOR_HUD_DIM);
  drawCrossMark(cx + hw - 2, cy + hh - 2, COLOR_HUD_DIM);

  if (idx == 0) {
    // BLADE — стрела с вырезом сзади, двойной контур
    int nX = cx + hw, tX = cx - hw + 2, mX = cx - 4, bX = cx - hw + 2;
    spr.drawLine(nX, cy, tX, cy - hh, COLOR_HUD_MAIN);
    spr.drawLine(tX, cy - hh, mX, cy, COLOR_HUD_MAIN);
    spr.drawLine(mX, cy, bX, cy + hh, COLOR_HUD_MAIN);
    spr.drawLine(bX, cy + hh, nX, cy, COLOR_HUD_MAIN);
    spr.drawLine(nX, cy, tX + 16, cy - hh + 18, COLOR_HUD_GLOW);
    spr.drawLine(tX + 16, cy - hh + 18, mX + 2, cy, COLOR_HUD_GLOW);
    spr.drawLine(mX + 2, cy, tX + 16, cy + hh - 18, COLOR_HUD_GLOW);
    spr.drawLine(tX + 16, cy + hh - 18, nX, cy, COLOR_HUD_GLOW);
    spr.drawLine(tX, cy - hh, cx - hw - 2, cy, COLOR_HUD_DIM);
    spr.drawLine(tX, cy + hh, cx - hw - 2, cy, COLOR_HUD_DIM);
    spr.drawCircle(cx + 8, cy, 6, COLOR_HUD_GLOW);
    spr.fillCircle(cx + 8, cy, 2, COLOR_HUD_GLOW);
  } else if (idx == 1) {
    // TITAN — массивный крейсер: ступенчатый корпус, две гондолы-двигателя, кольца-ядро
    for (int gy = -1; gy <= 1; gy += 2) {
      int ey = cy + gy * 24;
      drawEllipseOutline(cx - 30, ey, 13, 7, COLOR_HUD_MAIN);      // двигатель
      for (int t = -10; t <= 10; t += 5) {                        // рёбра
        int yy = (int)(7 * sqrt(max(0.0f, 1.0f - (t * t) / 169.0f)));
        spr.drawFastVLine(cx - 30 + t, ey - yy, 2 * yy, COLOR_HUD_DIM);
      }
      spr.drawCircle(cx - 17, ey, 3, COLOR_HUD_GLOW);             // сопло
    }
    // ступенчатый корпус (верх/низ зеркально), нос вправо
    int px[8] = {44, 34, 28, 24, 2, -2, -24, -28};
    int po[8] = {0, 10, 10, 18, 18, 24, 24, 14};
    for (int i = 0; i < 7; i++) {
      spr.drawLine(cx + px[i], cy - po[i], cx + px[i + 1], cy - po[i + 1], COLOR_HUD_MAIN);
      spr.drawLine(cx + px[i], cy + po[i], cx + px[i + 1], cy + po[i + 1], COLOR_HUD_MAIN);
      int a = px[i] * 7 / 10, b = po[i] * 6 / 10;
      int a2 = px[i + 1] * 7 / 10, b2 = po[i + 1] * 6 / 10;
      spr.drawLine(cx + a, cy - b, cx + a2, cy - b2, COLOR_HUD_GLOW);   // внутренний контур
      spr.drawLine(cx + a, cy + b, cx + a2, cy + b2, COLOR_HUD_GLOW);
    }
    spr.drawFastVLine(cx + 44, cy - 3, 6, COLOR_HUD_MAIN);        // тупой нос
    // центральные кольца-ядро с лучами
    spr.drawCircle(cx, cy, 14, COLOR_HUD_MAIN);
    spr.drawCircle(cx, cy, 10, COLOR_HUD_MAIN);
    spr.drawCircle(cx, cy, 6, COLOR_HUD_GLOW);
    spr.fillCircle(cx, cy, 3, COLOR_HUD_GLOW);
    for (int k = 0; k < 8; k++) {
      float a = k * 0.7853982f;
      spr.drawLine(cx + (int)(6 * cos(a)), cy + (int)(6 * sin(a)),
                   cx + (int)(13 * cos(a)), cy + (int)(13 * sin(a)), COLOR_HUD_DIM);
    }
  } else {
    // NEEDLE — быстрый: вытянутый корпус, два слоя крыльев, киль, фонарь
    spr.drawLine(cx + hw, cy, cx + 8, cy - 7, COLOR_HUD_MAIN);
    spr.drawLine(cx + hw, cy, cx + 8, cy + 7, COLOR_HUD_MAIN);
    spr.drawLine(cx + 8, cy - 7, cx - 8, cy - 9, COLOR_HUD_MAIN);
    spr.drawLine(cx + 8, cy + 7, cx - 8, cy + 9, COLOR_HUD_MAIN);
    spr.drawLine(cx - 8, cy - 9, cx - hw - 6, cy, COLOR_HUD_MAIN);
    spr.drawLine(cx - 8, cy + 9, cx - hw - 6, cy, COLOR_HUD_MAIN);
    // крылья, слой 1
    spr.drawLine(cx + 4, cy - 7, cx - 20, cy - 18, COLOR_HUD_GLOW);
    spr.drawLine(cx - 20, cy - 18, cx - 30, cy - 2, COLOR_HUD_GLOW);
    spr.drawLine(cx + 4, cy + 7, cx - 20, cy + 18, COLOR_HUD_GLOW);
    spr.drawLine(cx - 20, cy + 18, cx - 30, cy + 2, COLOR_HUD_GLOW);
    // крылья, слой 2
    spr.drawLine(cx - 4, cy - 8, cx - 28, cy - 13, COLOR_HUD_DIM);
    spr.drawLine(cx - 4, cy + 8, cx - 28, cy + 13, COLOR_HUD_DIM);
    // киль/хвост
    spr.drawLine(cx - 30, cy - 4, cx - hw - 8, cy - 10, COLOR_HUD_MAIN);
    spr.drawLine(cx - 30, cy + 4, cx - hw - 8, cy + 10, COLOR_HUD_MAIN);
    // фонарь кабины
    spr.drawLine(cx + 10, cy - 3, cx + 17, cy, COLOR_HUD_GLOW);
    spr.drawLine(cx + 10, cy + 3, cx + 17, cy, COLOR_HUD_GLOW);
    spr.drawCircle(cx + 2, cy, 3, COLOR_HUD_GLOW);
    spr.fillCircle(cx + 2, cy, 1, COLOR_HUD_GLOW);
  }
}

void drawGarageShipBlueprint(int idx, int cx, int cy) {
  drawGarageArt(idx, cx, cy);
}

// Схематичная подсветка узла корабля под выбранный апгрейд.
void drawGarageHighlight(int upgrade, int cx, int cy) {
  if (upgrade == 0) {
    // ARMOR — сфера-щит вокруг корабля
    spr.drawCircle(cx, cy, 42, COLOR_HUD_DIM);
    spr.drawCircle(cx, cy, 39, COLOR_HOST);
    for (int k = 0; k < 6; k++) {
      float a = k * 1.0471976f;
      spr.drawLine(cx + (int)(35 * cos(a)), cy + (int)(35 * sin(a)),
                   cx + (int)(44 * cos(a)), cy + (int)(44 * sin(a)), COLOR_HOST);
    }
  } else if (upgrade == 1) {
    // LAUNCHER — пусковая установка на верхнем крыле
    int px = cx - 22, py = cy - 31;
    spr.drawLine(px + 14, py + 9, cx - 4, cy - 16, COLOR_HUD_DIM); // связь с корпусом
    spr.drawRect(px, py, 30, 9, COLOR_HUD_GLOW);
    for (int t = 0; t < 3; t++) {
      int rx = px + 4 + t * 9;
      spr.fillRect(rx, py + 3, 6, 4, COLOR_HUD_MAIN);
      spr.fillTriangle(rx + 6, py + 3, rx + 10, py + 5, rx + 6, py + 7, COLOR_HUD_GLOW);
    }
  } else {
    // GUN (rate / power) — орудие на нижнем крыле
    int gx = cx - 20, gy = cy + 28;
    spr.drawLine(gx + 6, gy, cx - 4, cy + 16, COLOR_HUD_DIM); // связь с корпусом
    if (upgrade == 2) {
      // G.RATE — спаренный ствол
      spr.drawRect(gx, gy, 26, 4, COLOR_HUD_GLOW);
      spr.drawRect(gx, gy + 6, 26, 4, COLOR_HUD_GLOW);
      spr.fillRect(gx + 26, gy, 6, 4, COLOR_HUD_MAIN);
      spr.fillRect(gx + 26, gy + 6, 6, 4, COLOR_HUD_MAIN);
    } else {
      // G.PWR — толстый ствол с крупным дульным срезом
      spr.drawRect(gx, gy + 1, 30, 8, COLOR_HUD_GLOW);
      spr.fillRect(gx + 30, gy, 8, 10, COLOR_HUD_MAIN);
      spr.fillCircle(gx + 41, gy + 5, 3, TFT_YELLOW);
    }
  }
}

int upgradeLevel(int idx) {
  if (idx == 0) return armorLvl;
  if (idx == 1) return missilePodLvl;
  if (idx == 2) return gunRateLvl;
  return gunPowerLvl;
}

int upgradeMax(int idx) {
  if (idx == 0) return 5;
  if (idx == 1) return 8;
  return 3; // Gun Rate и Gun Power
}

int upgradeCost(int idx) {
  int lvl = upgradeLevel(idx);
  if (idx == 0) return 100 + lvl * 100;  // 100..500
  if (idx == 1) return 50 + lvl * 50;    // 50..400
  if (idx == 2) return 100 + lvl * 100;  // 100,200,300
  return 300 + lvl * 300;                // 300,600,900
}

void buyUpgrade(int idx) {
  int lvl = upgradeLevel(idx);
  if (lvl >= upgradeMax(idx)) return;
  int cost = upgradeCost(idx);
  if (credits < cost) return;
  credits -= cost;
  if (idx == 0) armorLvl++;
  else if (idx == 1) missilePodLvl++;
  else if (idx == 2) gunRateLvl++;
  else gunPowerLvl++;
  saveEconomy();
}

void handleGarageInput() {
  static bool b2 = true;
  static unsigned long t0 = 0;
  static bool hold = false;
  static bool counted = false;

  if (!subscreenReadyToExit) {
    if (digitalRead(BUTTON_1) == HIGH && digitalRead(BUTTON_2) == HIGH) subscreenReadyToExit = true;
    return;
  }

  bool owned = hasShip();

  if (digitalRead(BUTTON_2) == LOW) {
    if (b2) {
      if (owned) upgradeIndex = (upgradeIndex + 1) % UPGRADE_COUNT;
      else garageIndex = (garageIndex + 1) % 3;
      b2 = false;
    }
  } else b2 = true;

  if (digitalRead(BUTTON_1) == LOW) {
    if (!hold) { hold = true; counted = false; t0 = millis(); }
    else if (!counted && millis() - t0 > 700) {
      counted = true;
      currentState = STATE_MENU;
      delay(200);
    }
  } else {
    if (hold && !counted) {
      if (owned) {
        buyUpgrade(upgradeIndex);
      } else if (credits >= SHIP_PRICE) {
        credits -= SHIP_PRICE;
        ownedMask |= (1 << garageIndex);
        selectedShip = garageIndex;   // корабль фиксируется навсегда
        needShipMsg = false;
        saveEconomy();
      }
    }
    hold = false;
  }
}

void drawGarage() {
  spr.fillSprite(COLOR_BG);
  int x = SCREEN_WIDTH / 16;
  bool owned = hasShip();

  if (!owned) {
    // Режим покупки: показаны все корабли
    const ShipDef &s = SHIPS[garageIndex];
    drawGarageShipBlueprint(garageIndex, SCREEN_WIDTH - 58, SCREEN_HEIGHT / 2);
    spr.setTextSize(1);
    spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
    spr.setCursor(x, 2);
    spr.print("GARAGE  CR:");
    spr.print(credits);
    spr.setTextSize(2);
    spr.setCursor(x, 14);
    spr.print(s.name);
    spr.setTextSize(1);
    spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
    spr.setCursor(x, 40);
    spr.print(s.role);
    spr.setCursor(x, 54);
    spr.print("HP:");
    spr.print(s.hull);
    spr.print(" SPD:");
    spr.print(s.move);
    spr.print(" FIR:");
    spr.print((int)lround(1000.0f / s.fireMs));
    spr.setCursor(x, 70);
    if (credits >= SHIP_PRICE) { spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG); spr.print("BUY 999"); }
    else { spr.setTextColor(TFT_RED, COLOR_BG); spr.print("NEED 999 CR"); }
    spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
    spr.setCursor(x, SCREEN_HEIGHT - 12);
    spr.print("BTN2 next  BTN1:buy hold=exit");
  } else {
    // Режим прокачки: только свой корабль + улучшения
    const ShipDef &s = SHIPS[selectedShip];
    drawGarageShipBlueprint(selectedShip, SCREEN_WIDTH - 58, SCREEN_HEIGHT / 2);
    drawGarageHighlight(upgradeIndex, SCREEN_WIDTH - 58, SCREEN_HEIGHT / 2);
    spr.setTextSize(1);
    spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
    spr.setCursor(x, 2);
    spr.print("GARAGE  CR:");
    spr.print(credits);
    spr.setTextSize(2);
    spr.setCursor(x, 14);
    spr.print(s.name);
    spr.setTextSize(1);
    spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
    spr.setCursor(x, 32);
    spr.print("HP:");
    spr.print(playerMaxHp());
    spr.print(" FIR:");
    spr.print(1000 / playerFireMs());
    spr.print(" DMG:");
    spr.print(playerBulletDmg());

    const char *un[UPGRADE_COUNT] = {"ARMOR", "LAUNCHER", "G.RATE", "G.PWR"};
    for (int i = 0; i < UPGRADE_COUNT; i++) {
      int y = 50 + i * 14;
      int lvl = upgradeLevel(i), mx = upgradeMax(i);
      spr.setTextColor(i == upgradeIndex ? COLOR_HUD_GLOW : COLOR_HUD_MAIN, COLOR_BG);
      spr.setCursor(x, y);
      spr.print(i == upgradeIndex ? "> " : "  ");
      spr.print(un[i]);
      spr.print(" ");
      spr.print(lvl);
      spr.print("/");
      spr.print(mx);
      if (lvl >= mx) spr.print(" MAX");
      else { spr.print(" "); spr.print(upgradeCost(i)); spr.print("CR"); }
    }
    spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
    spr.setCursor(x, SCREEN_HEIGHT - 12);
    spr.print("BTN2 next  BTN1:upg hold=exit");
  }
  spr.pushSprite(0, 0);
}

void handleCoopLobbyInput() {
  static unsigned long t0 = 0;
  static bool hold = false;
  static bool b2 = true;

  if (!subscreenReadyToExit) {
    if (digitalRead(BUTTON_1) == HIGH && digitalRead(BUTTON_2) == HIGH) subscreenReadyToExit = true;
    return;
  }

  int allowed = allowedLevels();
  if (lobbyLevel > allowed - 1) lobbyLevel = allowed - 1;
  if (lobbyLevel < 0) lobbyLevel = 0;

  if (digitalRead(BUTTON_2) == LOW) {
    if (b2) { lobbyLevel = (lobbyLevel + 1) % allowed; b2 = false; }
  } else b2 = true;

  if (digitalRead(BUTTON_1) == LOW) {
    if (!hold) { hold = true; t0 = millis(); }
    else if (millis() - t0 > 700) { currentState = STATE_MENU; delay(200); return; }
  } else {
    if (hold && millis() - t0 < 700) {
      if (isPeerConnected) {
        GamePacket pkt = {};
        pkt.type = 4;
        pkt.level = (uint8_t)lobbyLevel;
        pkt.progress = (uint8_t)levelProgress;
        sendToPeer(pkt);
        currentLevel = lobbyLevel;
        resetMatch();
        currentState = STATE_COOP_GAME;
      }
    }
    hold = false;
  }
}

void updateCoopLobby() {
  if (espNowReady && !isPeerConnected && millis() - lastPingTime > 500) {
    lastPingTime = millis();
    GamePacket pkt = {};
    pkt.type = 1;
    pkt.progress = (uint8_t)levelProgress;
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    addPeer(bcast);
    esp_now_send(bcast, (uint8_t *)&pkt, sizeof(pkt));
  }
}

void drawCoopLobby() {
  spr.fillSprite(COLOR_BG);
  drawStaticHexagon();
  int x = SCREEN_WIDTH / 13;
  spr.setTextSize(MENU_TEXT);
  spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT / 10);
  spr.print("COOP LOBBY");
  spr.setTextSize(1);
  spr.setCursor(x, SCREEN_HEIGHT * 30 / 100);
  if (!espNowReady) {
    spr.setTextColor(TFT_RED, COLOR_BG);
    spr.print("RADIO ERROR: ESP-NOW init failed");
  } else if (isPeerConnected) {
    spr.setTextColor(COLOR_HOST, COLOR_BG);
    spr.print("P2 CONNECTED! ROLE: ");
    spr.print(isHost ? "HOST" : "CLIENT");
  } else {
    spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
    spr.print("SEARCHING FOR P2...");
  }
  spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
  int ly = SCREEN_HEIGHT * 38 / 100;
  spr.setCursor(x, ly);
  spr.print("LEVEL ");
  spr.print(lobbyLevel + 1);
  spr.print("/");
  spr.print(allowedLevels());
  spr.print(" ");
  spr.print(levelName(lobbyLevel));
  spr.setCursor(x, ly + 12);
  spr.print("CLEARED YOU:");
  spr.print(levelProgress);
  spr.print(" P2:");
  if (peerProgress >= 0) spr.print(peerProgress); else spr.print("-");
  spr.print(" (BTN2)");

  int btnY = SCREEN_HEIGHT * 60 / 100;
  uint16_t c = isPeerConnected ? COLOR_HUD_GLOW : COLOR_HUD_DIM;
  spr.drawRect(x, btnY, 130, 22, c);
  spr.setTextColor(c, COLOR_BG);
  spr.setCursor(x + 8, btnY + 7);
  spr.print(isPeerConnected ? "[ BTN1 START ]" : "[ WAITING... ]");
  spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT - 16);
  spr.print("Hold BTN1 to exit");
  spr.pushSprite(0, 0);
}

bool consumeFireEdge() {
  static unsigned long lastShot = 0;
  int gap = playerFireMs();
  if (millis() - lastShot > (unsigned)gap) { lastShot = millis(); return true; }
  return false;
}

void handleGameInput(bool coop) {
  int step = hasShip() ? SHIPS[selectedShip].move : 40;
  int top = 40;
  int bot = WORLD - 40;

  if (!player.falling && !player.dead) {
    if (digitalRead(BUTTON_1) == LOW) {
      player.y -= step;
      if (player.y < top) player.y = top;
    }
    if (digitalRead(BUTTON_2) == LOW) { 
      player.y += step; 
      if (player.y > bot) player.y = bot; 
    }
  }

  if (coop) {
    if (player.dead && remote.dead) {
      finishMatch(true);
      currentState = STATE_GAMEOVER;
      return;
    }
  } else {
    if (player.dead) {
      finishMatch(false);
      currentState = STATE_GAMEOVER;
      return;
    }
  }

  bool canShoot = !player.falling && !player.dead;

  // Обе кнопки: короткий тап (<400мс) — пуск ракеты;
  // удержание — реанимация напарника (2с, только у обломков).
  static unsigned long bothDownAt = 0;
  static bool bothWasDown = false;
  static bool reviveDone = false;
  bool rkFired = false;
  bool bothDown = digitalRead(BUTTON_1) == LOW && digitalRead(BUTTON_2) == LOW;
  if (bothDown) {
    if (!bothWasDown) { bothWasDown = true; bothDownAt = millis(); reviveDone = false; }
    else if (canShoot && !reviveDone && coop && (remote.dead || remote.falling) &&
             revivers > 0 && millis() - bothDownAt > 2000) {
      // Реанимировать можно только у самих обломков: у подбитого напарника
      // remote.y остаётся прежним, а обломки лежат внизу экрана.
      int wreckY = remote.falling ? remote.fallY : (remote.dead ? WORLD - 20 : remote.y);
      if (abs(player.y - wreckY) < 150) {
        revivers--;
        remote.dead = false;
        remote.falling = false;
        remote.hp = remoteMaxHp;
        saveEconomy();
        GamePacket revPkt = {};
        revPkt.type = 6;
        sendToPeer(revPkt);
        reviveDone = true;
      }
    }
  } else {
    if (bothWasDown && !reviveDone && canShoot && millis() - bothDownAt < 400) {
      if (playerLaunchRocket()) rkFired = true;
    }
    bothWasDown = false;
  }

  bool fired = false;
  if (canShoot && consumeFireEdge()) {
    spawnBulletAt(player.x + 40, player.y, false, (uint8_t)playerBulletDmg());
    fired = true;
  }

  if (!coop) return;

  if (isHost) {
    if (fired) pendingHostShot = true;
    return;
  }

  // Клиент шлёт ввод/состояние хосту и будучи подбитым: иначе хост
  // перестаёт получать пакеты и гасит remoteSeen — напарник исчезает.
  if (millis() - lastStateSend > 33 || fired || rkFired) {
    GamePacket pkt = {};
    pkt.type = 3;
    pkt.y = player.falling ? yToNet(player.fallY) : yToNet(player.y);
    pkt.flags = 0;
    if (fired) pkt.flags |= 0x01;
    if (player.falling) pkt.flags |= 0x02;
    if (player.dead) pkt.flags |= 0x04;
    if (pendingRocketKind >= 0) {
      pkt.flags |= 0x08;
      pkt.rocketKind = (uint8_t)pendingRocketKind;
      pendingRocketKind = -1;
    }
    pkt.senderHull = (uint8_t)playerMaxHp();
    pkt.senderHp = (uint8_t)max(0, player.hp);
    pkt.senderShip = (uint8_t)(hasShip() ? selectedShip + 1 : 0);
    pkt.senderGun = (uint8_t)gunPowerLvl;
    pkt.seq = txSeq++;
    sendToPeer(pkt);
    lastStateSend = millis();
  }
}

int enemyHpForLevel(EnemyType t) {
  if (t == ENEMY_SATELLITE) return 4;
  if (t == ENEMY_GUARDIAN) return 4;
  if (t == ENEMY_CRUISER) return 3;
  if (t == ENEMY_DRILL) return 3;
  if (t == ENEMY_BASTION) return 3;
  if (t == ENEMY_SICKLE) return 3;
  if (t == ENEMY_BEETLE) return 2;
  if (t == ENEMY_POD) return 2;
  if (t == ENEMY_SWARM) return 2;
  if (t == ENEMY_COMET) return 2;
  return 1;
}

int enemyCreditsFor(EnemyType t) {
  if (t == ENEMY_SATELLITE) return 4;
  if (t == ENEMY_GUARDIAN) return 4;
  if (t == ENEMY_SICKLE) return 4;
  if (t == ENEMY_CRUISER) return 3;
  if (t == ENEMY_DRILL) return 3;
  if (t == ENEMY_BASTION) return 3;
  if (t == ENEMY_BEETLE) return 2;
  if (t == ENEMY_POD) return 2;
  if (t == ENEMY_RAY) return 2;
  if (t == ENEMY_SWARM) return 2;
  if (t == ENEMY_COMET) return 2;
  return 1;
}

// Урон игроку при столкновении с врагом
int enemyContactDamage(EnemyType t) {
  if (t == ENEMY_COMET) return 3;
  if (t == ENEMY_BASTION) return 3;
  if (t == ENEMY_SWARM) return 2;
  if (t == ENEMY_DRILL) return 2;
  if (t == ENEMY_GHOST) return 2;
  if (t == ENEMY_SICKLE) return 2;
  if (t == ENEMY_GUARDIAN) return 2;
  return 1;
}

// Уровень 0 (Sunset): тарелка + дрон.
// Уровень 1 (Purple Night): тарелка + дрон + жук.
// Уровень 2 (Ice World): тарелка + дрон + жук (+ босс).
// Уровень 3 (Toxic Forest): стручок + перехватчик + мина.
EnemyType randomEnemyForLevel(int level) {
  int r = random(0, 100);
  if (level <= 0) return r < 55 ? ENEMY_SAUCER : ENEMY_DRONE;
  if (level == 1) {
    if (r < 40) return ENEMY_SAUCER;
    if (r < 75) return ENEMY_DRONE;
    return ENEMY_BEETLE;
  }
  if (level == 2) {
    if (r < 40) return ENEMY_SAUCER;
    if (r < 75) return ENEMY_DRONE;
    return ENEMY_BEETLE;
  }
  if (level == 3) {
    // Toxic Forest
    if (r < 45) return ENEMY_POD;
    if (r < 80) return ENEMY_INTERCEPTOR;
    return ENEMY_MINE;
  }
  if (level == 4) {
    // Crescent of a Planet
    if (r < 40) return ENEMY_MINE;
    if (r < 75) return ENEMY_DRONE;
    return ENEMY_CRUISER;
  }
  if (level == 5) {
    // Lava Planet
    if (r < 40) return ENEMY_RAY;
    if (r < 75) return ENEMY_SAUCER;
    return ENEMY_POD;
  }
  if (level == 6) {
    // Northern Lights
    if (r < 35) return ENEMY_SWARM;
    if (r < 70) return ENEMY_COMET;
    return ENEMY_RAY;
  }
  if (level == 7) {
    // Dunes and two moons
    if (r < 40) return ENEMY_DRILL;
    if (r < 75) return ENEMY_GHOST;
    return ENEMY_SATELLITE;
  }
  if (level == 8) {
    // Asteroid field
    if (r < 50) return ENEMY_BASTION;
    return ENEMY_SICKLE;
  }
  if (level == 9) {
    // Station
    if (r < 40) return ENEMY_BASTION;
    if (r < 75) return ENEMY_DRILL;
    return ENEMY_SATELLITE;
  }
  if (level == 10) {
    // Dawn over the ocean
    if (r < 40) return ENEMY_SATELLITE;
    if (r < 70) return ENEMY_SICKLE;
    return ENEMY_SWARM;
  }
  // Gas giant
  if (r < 40) return ENEMY_GUARDIAN;
  if (r < 70) return ENEMY_COMET;
  return ENEMY_SATELLITE;
}

const char *levelName(int level) {
  if (level == 1) return "PURPLE NIGHT";
  if (level == 2) return "ICE WORLD";
  if (level == 3) return "TOXIC FOREST";
  if (level == 4) return "CRESCENT";
  if (level == 5) return "LAVA";
  if (level == 6) return "NORTHERN";
  if (level == 7) return "DUNES";
  if (level == 8) return "ASTEROID";
  if (level == 9) return "STATION";
  if (level == 10) return "DAWN";
  if (level == 11) return "GAS GIANT";
  return "SUNSET";
}

int levelGoal(int level) {
  if (level == 0) return 40;
  if (level == 1) return 50;
  if (level == 2) return 50;   // + босс
  if (level == 3) return 60;
  if (level == 4) return 60;   // + босс
  if (level == 5) return 70;
  if (level == 6) return 70;   // + босс
  if (level == 7) return 80;
  if (level == 8) return 80;   // + босс
  if (level == 9) return 90;
  if (level == 10) return 100;  // + босс
  if (level == 11) return 120;  // + босс
  return 40;
}

// Интенсивность: 1.0/1.2/1.4/1.6/1.8/2.0/2.0/2.0/2.2/2.2 (в процентах)
int levelIntensityPct(int level) {
  if (level == 1) return 120;
  if (level == 2) return 140;
  if (level == 3) return 160;
  if (level == 4) return 180;
  if (level == 5) return 200;
  if (level == 6) return 200;
  if (level == 7) return 200;
  if (level == 8) return 220;
  if (level == 9) return 220;
  if (level == 10) return 220;
  if (level == 11) return 220;
  return 100;
}

int spawnIntervalMs() { return 1800 * 100 / levelIntensityPct(currentLevel); }

bool levelHasBoss(int level) { return level == 2 || level == 4 || level == 6 || level == 8 || level == 10 || level == 11; }

// Правило слабого звена: доступно min(прогресс1, прогресс2) + 1 уровней.
int allowedLevels() {
  int p = levelProgress;
  if (peerProgress >= 0 && peerProgress < p) p = peerProgress;
  int allowed = p + 1;
  if (allowed > LEVEL_COUNT) allowed = LEVEL_COUNT;
  if (allowed < 1) allowed = 1;
  return allowed;
}

void spawnEnemy() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) continue;
    enemies[i].active = true;
    enemies[i].x = WORLD + 80;
    enemies[i].y = random(60, WORLD - 60);
    enemies[i].type = randomEnemyForLevel(currentLevel);
    enemies[i].hp = enemyHpForLevel(enemies[i].type);
    enemies[i].nextFire = millis() + 1000;
    break;
  }
}

void moveBulletsAndStars() {
  int skyTop = HUD_H + 2;
  int skyBot = SCREEN_HEIGHT - 18;
  for (int i = 0; i < NUM_STARS; i++) {
    stars[i].x -= stars[i].speed;
    if (stars[i].x < 0) { stars[i].x = SCREEN_WIDTH; stars[i].y = random(skyTop, skyBot); }
  }
  int speed = 48;
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    bullets[i].x += speed;
    if (bullets[i].x > WORLD + 80) bullets[i].active = false;
  }
}

void stepEnemies() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    int spd = 12;
    if (enemies[i].type == ENEMY_INTERCEPTOR) spd = 18;
    else if (enemies[i].type == ENEMY_RAY) spd = 16;
    else if (enemies[i].type == ENEMY_MINE) spd = 10;
    else if (enemies[i].type == ENEMY_CRUISER) spd = 8;  // тяжёлый
    else if (enemies[i].type == ENEMY_SWARM) spd = 14;
    else if (enemies[i].type == ENEMY_COMET) spd = 8;
    else if (enemies[i].type == ENEMY_DRILL) spd = 8;
    else if (enemies[i].type == ENEMY_GHOST) spd = 16;
    else if (enemies[i].type == ENEMY_SATELLITE) spd = 8;
    else if (enemies[i].type == ENEMY_BASTION) spd = 12;
    else if (enemies[i].type == ENEMY_SICKLE) spd = 14;
    else if (enemies[i].type == ENEMY_GUARDIAN) spd = 8;
    enemies[i].x -= spd;
    if (enemies[i].type == ENEMY_SAUCER) enemies[i].y += sin(timeCnt * 3 + i) * 4.0f;
    // Рой: до середины поля летит прямо, затем наводится на ближайшего игрока (таран)
    if (enemies[i].type == ENEMY_SWARM && enemies[i].x <= WORLD_MID) {
      int targetY = player.y;
      if (isHost && remoteSeen && !remote.dead &&
          abs(remote.y - enemies[i].y) < abs(player.y - enemies[i].y)) targetY = remote.y;
      if (enemies[i].y < targetY) enemies[i].y += 10;
      else if (enemies[i].y > targetY) enemies[i].y -= 10;
      if (enemies[i].y < 40) enemies[i].y = 40;
      if (enemies[i].y > WORLD - 40) enemies[i].y = WORLD - 40;
    }
    // Стреляющие враги (Скат / Спутник / Серп): FIR 1 (1 выстрел/сек)
    if ((enemies[i].type == ENEMY_RAY || enemies[i].type == ENEMY_SATELLITE ||
         enemies[i].type == ENEMY_SICKLE || enemies[i].type == ENEMY_GUARDIAN) &&
        millis() >= enemies[i].nextFire) {
      spawnEnemyShot(enemies[i].type, enemies[i].x - 12, enemies[i].y);
      enemies[i].nextFire = millis() + 1000;
      pendingEnemyFire |= (1 << i);
    }
    if (enemies[i].x < -80) enemies[i].active = false;
  }
}

void checkPlayerEnemyCollisions(Player &p) {
  if (p.falling || p.dead) return;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    if (abs(p.x - enemies[i].x) < 65 && abs(p.y - enemies[i].y) < 65) {
      if (enemies[i].type == ENEMY_MINE) {
        int mx = enemies[i].x, my = enemies[i].y;
        enemies[i].active = false;
        explodeMine(mx, my, false, 0);
      } else {
        enemies[i].active = false;
        p.hp -= enemyContactDamage(enemies[i].type);
        if (p.hp <= 0) {
          p.falling = true;
          p.fallY = p.y;
        }
      }
    }
  }
}

void resolveHits(bool applyDamage) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    for (int b = 0; b < MAX_BULLETS; b++) {
      if (!bullets[b].active) continue;
      if (bullets[b].x >= enemies[i].x - 30 && bullets[b].x <= enemies[i].x + 55 &&
          bullets[b].y >= enemies[i].y - 50 && bullets[b].y <= enemies[i].y + 50) {
        bullets[b].active = false; // пуля гасится о врага (и на клиенте — визуально)
        if (applyDamage) {
          if (enemies[i].type == ENEMY_MINE) {
            int mx = enemies[i].x, my = enemies[i].y;
            enemies[i].active = false;
            explodeMine(mx, my, bullets[b].fromRemote, 0); // мина детонирует от попадания
          } else {
            enemies[i].hp -= bullets[b].dmg;
            if (enemies[i].hp <= 0) {
              enemies[i].active = false;
              awardKill(enemies[i].type, bullets[b].fromRemote);
              break; // враг уничтожен — не проверяем остальные пули в этом кадре
            }
          }
        }
      }
    }
  }
  if (bossActive) {
    for (int b = 0; b < MAX_BULLETS; b++) {
      if (!bullets[b].active) continue;
      if (bossHitBy(bullets[b].x, bullets[b].y, 0, 0)) {
        bullets[b].active = false;
        if (applyDamage) damageBoss(bullets[b].dmg, bullets[b].fromRemote);
      }
    }
  }
}

void updateSolo() {
  moveBulletsAndStars();
  static unsigned long lastSpawn = 0;
  if (millis() - lastSpawn > (unsigned)spawnIntervalMs()) { spawnEnemy(); lastSpawn = millis(); }
  stepEnemies();
  resolveHits(true);
  updateRockets(true);
  updateShards(true);
  updateExplosions();

  if (levelHasBoss(currentLevel) && !bossActive && !bossDefeated && matchKills >= levelGoal(currentLevel))
    spawnBoss();
  updateBoss(true);
  updateAcids(true);

  bool done = levelHasBoss(currentLevel) ? bossDefeated : (matchKills >= levelGoal(currentLevel));
  if (done) {
    levelProgress = max(levelProgress, currentLevel + 1);
    saveEconomy();
    finishMatch(false);
    currentState = STATE_LEVEL_DONE;
    return;
  }

  checkPlayerEnemyCollisions(player);

  if (player.falling) {
    player.fallY += 30;
    if (player.fallY >= WORLD - 20) {
      player.falling = false;
      player.dead = true;
    }
  }
}

void sendWorld() {
  GamePacket pkt = {};
  pkt.type = 5;
  pkt.y = player.falling ? yToNet(player.fallY) : yToNet(player.y);
  pkt.flags = 0;
  if (player.falling) pkt.flags |= 0x02;
  if (player.dead) pkt.flags |= 0x04;
  if (pendingHostShot) { pkt.flags |= 0x01; pendingHostShot = false; }
  if (pendingRocketKind >= 0) {
    pkt.flags |= 0x08;
    pkt.rocketKind = (uint8_t)pendingRocketKind;
    pendingRocketKind = -1;
  }

  // Хост передает клиенту его актуальное здоровье и статус падения/смерти
  pkt.remoteHp = (uint8_t)max(0, remote.hp);
  pkt.remoteFlags = 0;
  if (remote.falling) pkt.remoteFlags |= 0x02;
  if (remote.dead) pkt.remoteFlags |= 0x04;

  // HP и корпус хоста — для отображения у клиента
  pkt.senderHull = (uint8_t)playerMaxHp();
  pkt.senderHp = (uint8_t)max(0, player.hp);
  pkt.senderShip = (uint8_t)(hasShip() ? selectedShip + 1 : 0);
  pkt.senderGun = (uint8_t)gunPowerLvl;

  pkt.matchCredits = (uint8_t)min(matchCredits, 255);
  pkt.hostScore = (uint8_t)min(player.score, 255);
  pkt.clientScore = (uint8_t)min(remote.score, 255);
  pkt.level = (uint8_t)currentLevel;
  pkt.kills = (uint8_t)min(matchKills, 255);
  pkt.boss = bossActive ? 1 : 0;
  pkt.bossHp = (uint8_t)max(0, bossHp);
  pkt.bossType = (uint8_t)bossType;
  pkt.bossY = yToNet(bossY);
  pkt.acidShot = (uint8_t)pendingAcidShot;
  pendingAcidShot = 0;
  pkt.enemyFire = pendingEnemyFire;
  pendingEnemyFire = 0;
  pkt.seq = txSeq++;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) { pkt.enemies[i].kind = 255; continue; }
    pkt.enemies[i].kind = (uint8_t)enemies[i].type; // 0 saucer, 1 drone, 2 beetle
    pkt.enemies[i].hp = (uint8_t)enemies[i].hp;
    pkt.enemies[i].x = (int16_t)enemies[i].x;
    pkt.enemies[i].y = (int16_t)enemies[i].y;
  }
  sendToPeer(pkt);
}

void updateCoopGame() {
  moveBulletsAndStars();

  if (isHost) {
    static unsigned long lastSpawn = 0;
    if (millis() - lastSpawn > (unsigned)spawnIntervalMs()) { spawnEnemy(); lastSpawn = millis(); }
    stepEnemies();
    resolveHits(true);
    updateRockets(true);
    updateShards(true);
    updateExplosions();

    if (levelHasBoss(currentLevel) && !bossActive && !bossDefeated && matchKills >= levelGoal(currentLevel))
      spawnBoss();
    updateBoss(true);
    updateAcids(true);

    bool levelDone = levelHasBoss(currentLevel) ? bossDefeated : (matchKills >= levelGoal(currentLevel));
    if (levelDone) {
      GamePacket dp = {};
      dp.type = 7;
      dp.level = (uint8_t)currentLevel;
      dp.kills = (uint8_t)min(matchKills, 255);
      dp.matchCredits = (uint8_t)min(matchCredits, 255);
      dp.hostScore = (uint8_t)min(player.score, 255);
      dp.clientScore = (uint8_t)min(remote.score, 255);
      sendToPeer(dp);
      levelProgress = max(levelProgress, currentLevel + 1);
      saveEconomy();
      finishMatch(true);
      currentState = STATE_LEVEL_DONE;
      return;
    }

    // 1. Проверяем столкновения хоста с врагами
    checkPlayerEnemyCollisions(player);

    // 2. Хост проверяет столкновения клиента (remote) с врагами
    if (!remote.falling && !remote.dead) {
      for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        if (abs(remote.x - enemies[i].x) < 65 && abs(remote.y - enemies[i].y) < 65) {
          if (enemies[i].type == ENEMY_MINE) {
            int mx = enemies[i].x, my = enemies[i].y;
            enemies[i].active = false;
            explodeMine(mx, my, true, 0);
          } else {
            enemies[i].active = false;
            remote.hp -= enemyContactDamage(enemies[i].type);
            if (remote.hp <= 0) {
              remote.falling = true;
              remote.fallY = remote.y;
            }
          }
        }
      }
    }

    if (player.falling) {
      player.fallY += 30;
      if (player.fallY >= WORLD - 20) {
        player.falling = false;
        player.dead = true;
      }
    }

    if (remote.falling) {
      remote.fallY += 30;
      if (remote.fallY >= WORLD - 20) {
        remote.falling = false;
        remote.dead = true;
      }
    }

    sendWorld();
  } else {
    // Клиент проигрывает визуал (пули/ракеты/взрывы), урон считает хост
    resolveHits(false);
    updateRockets(false);
    updateShards(false);
    updateBoss(false);
    updateAcids(false);
    updateExplosions();
    // Анимация падения для клиента локально при получении флага от хоста
    if (player.falling) {
      player.fallY += 30;
      if (player.fallY >= WORLD - 20) {
        player.falling = false;
        player.dead = true;
      }
    }
  }

  if (remoteSeen && millis() - lastRemoteMs > 1500) remoteSeen = false;
}

void handleGameOverInput() {
  static bool b1 = true;
  if (digitalRead(BUTTON_1) == LOW) {
    if (b1) {
      currentState = STATE_MENU;
      b1 = false;
    }
  } else {
    b1 = true;
  }
}

void drawGameOver() {
  spr.fillSprite(COLOR_BG);
  drawStaticHexagon();
  int x = SCREEN_WIDTH / 10;
  
  spr.setTextSize(2);
  spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT / 5);
  spr.print("GAME OVER");

  spr.setTextSize(1);
  spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT * 45 / 100);
  spr.print("EARNED CR: ");
  spr.print(matchPayout);

  spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT * 75 / 100);
  spr.print("[ PRESS BTN1 TO MENU ]");
  spr.pushSprite(0, 0);
}

void handleLevelDoneInput() {
  static bool b1 = true;
  if (digitalRead(BUTTON_1) == LOW) {
    if (b1) { currentState = STATE_MENU; b1 = false; }
  } else {
    b1 = true;
  }
}

void drawLevelDone() {
  spr.fillSprite(COLOR_BG);
  drawStaticHexagon();
  int x = SCREEN_WIDTH / 10;
  spr.setTextSize(2);
  spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT / 7);
  spr.print("LEVEL");
  spr.setCursor(x, SCREEN_HEIGHT / 7 + 20);
  spr.print("COMPLETE");

  spr.setTextSize(1);
  spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT * 55 / 100);
  spr.print("KILLS: ");
  spr.print(matchKills);
  spr.setCursor(x, SCREEN_HEIGHT * 66 / 100);
  spr.print("CREDITS: ");
  spr.print(matchPayout);

  spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
  spr.setCursor(x, SCREEN_HEIGHT * 82 / 100);
  spr.print("[ PRESS BTN1 TO MENU ]");
  spr.pushSprite(0, 0);
}

// Статический QR-код для DONATION_ADDR (USDT-TRC20), 29x29, ЕС M.
// Сгенерирован офлайн; при смене адреса QR нужно перегенерировать.
#define QR_SIZE 29
const uint64_t QR_ROWS[QR_SIZE] = {
  0x000000001fcf9a7fULL, 0x0000000010577d41ULL, 0x00000000175cb55dULL,
  0x000000001751115dULL, 0x000000001751905dULL, 0x0000000010429641ULL,
  0x000000001fd5557fULL, 0x00000000001dd700ULL, 0x000000000e620141ULL,
  0x00000000158f38acULL, 0x0000000004e7ef7bULL, 0x0000000009c83738ULL,
  0x0000000011c8a1efULL, 0x000000001b988e33ULL, 0x0000000005938171ULL,
  0x00000000161c179bULL, 0x00000000178b8579ULL, 0x0000000001d9612bULL,
  0x0000000011de8657ULL, 0x0000000002053b39ULL, 0x0000000001f3cf69ULL,
  0x000000001b19cf00ULL, 0x000000000553847fULL, 0x00000000131c5441ULL,
  0x000000001df8e45dULL, 0x0000000012b5485dULL, 0x000000000968865dULL,
  0x0000000015accc41ULL, 0x00000000060a1f7fULL
};

void handleAboutInput() {
  static bool b1 = true, b2 = true;
  if (!subscreenReadyToExit) {
    if (digitalRead(BUTTON_1) == HIGH && digitalRead(BUTTON_2) == HIGH) subscreenReadyToExit = true;
    return;
  }
  if (digitalRead(BUTTON_2) == LOW) {
    if (b2) { aboutPage = 1 - aboutPage; b2 = false; }
  } else b2 = true;
  if (digitalRead(BUTTON_1) == LOW) {
    if (b1) { currentState = STATE_MENU; b1 = false; }
  } else b1 = true;
}

void drawAbout() {
  spr.fillSprite(COLOR_BG);
  drawStaticHexagon();
  int x = SCREEN_WIDTH / 13;
  spr.setTextSize(1);

  if (aboutPage == 0) {
    spr.setTextSize(MENU_TEXT);
    spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
    spr.setCursor(x, 8);
    spr.print("ABOUT");
    spr.setTextSize(1);
    spr.setTextColor(COLOR_HUD_MAIN, COLOR_BG);
    int y = 32;
    spr.setCursor(x, y); spr.print("TWIN VOID - co-op shooter"); y += 12;
    spr.setCursor(x, y); spr.print("LilyGo T-Display (S3)"); y += 12;
    spr.setCursor(x, y); spr.print("Author: "); spr.print(AUTHOR_NAME); y += 12;
    spr.setCursor(x, y); spr.print("Open source: MIT"); y += 12;
    spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
    spr.setCursor(x, y); spr.print("DONATE USDT-TRC20:"); y += 11;
    spr.setCursor(x, y); spr.print(DONATION_ADDR);
    spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
    spr.setCursor(x, SCREEN_HEIGHT - 12);
    spr.print("[BTN2] QR   [BTN1] MENU");
  } else {
    int total = QR_SIZE + 8;      // + quiet zone (4 модуля с каждой стороны)
    int qs = 3;                   // масштаб модуля
    int qx = (SCREEN_WIDTH - total * qs) / 2;
    int qy = (SCREEN_HEIGHT - total * qs) / 2;
    spr.setTextColor(COLOR_HUD_GLOW, COLOR_BG);
    spr.setCursor(x, 2);
    spr.print("DONATE USDT-TRC20");
    spr.fillRect(qx, qy, total * qs, total * qs, TFT_WHITE);
    for (int r = 0; r < QR_SIZE; r++)
      for (int c = 0; c < QR_SIZE; c++)
        if ((QR_ROWS[r] >> c) & 1ULL)
          spr.fillRect(qx + (c + 4) * qs, qy + (r + 4) * qs, qs, qs, TFT_BLACK);
    spr.setTextColor(COLOR_HUD_DIM, COLOR_BG);
    spr.setCursor(x, SCREEN_HEIGHT - 11);
    spr.print("[BTN2] INFO   [BTN1] MENU");
  }
  spr.pushSprite(0, 0);
}

void drawBackground() {
  int h = SCREEN_HEIGHT;
  spr.fillSprite(COLOR_BG); // гарантированная полная очистка (нет «залипших» пикселей)
  if (currentLevel == 1) {
    // Уровень 2: фиолетовая ночь и город
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_NIGHT_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_NIGHT_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_NIGHT_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    int mx = SCREEN_WIDTH * 76 / 100, my = h * 22 / 100, mr = h / 10;
    spr.fillCircle(mx, my, mr, COLOR_MOON);
    spr.fillCircle(mx - mr / 3, my - mr / 4, mr / 5, COLOR_NIGHT_MID);
    spr.fillCircle(mx + mr / 4, my + mr / 3, mr / 6, COLOR_NIGHT_MID);
    int seed = 0;
    for (int bx = 0; bx < SCREEN_WIDTH; ) {
      int bw = 12 + (seed * 7) % 20;
      int bh = 22 + (seed * 13) % 42;
      spr.fillRect(bx, h - bh, bw, bh, TFT_BLACK);
      for (int wy = h - bh + 5; wy < h - 4; wy += 6)
        for (int wx = bx + 3; wx < bx + bw - 2; wx += 6)
          if (((wx * 7 + wy * 3 + seed) % 5) == 0) spr.drawPixel(wx, wy, COLOR_HUD_GLOW);
      bx += bw + 3;
      seed++;
    }
  } else if (currentLevel == 4) {
    // Уровень 5: полумесяц планеты
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_CRES_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_CRES_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_CRES_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    // большая тёмная планета с оранжевой кромкой справа внизу
    int px = SCREEN_WIDTH * 92 / 100, py = SCREEN_HEIGHT * 82 / 100, pr = SCREEN_HEIGHT * 45 / 100;
    // видимая часть диска (клип по экрану, без записи за границы спрайта)
    for (int yy = max(0, py - pr); yy <= min(SCREEN_HEIGHT - 1, py + pr); yy++) {
      int dy = yy - py;
      int dx = (int)sqrt((float)(pr * pr - dy * dy));
      int x0 = max(0, px - dx), x1 = min(SCREEN_WIDTH - 1, px + dx);
      if (x1 >= x0) spr.drawFastHLine(x0, yy, x1 - x0 + 1, tft.color565(14, 16, 34));
    }
    // гладкая оранжевая кромка: радиальные сегменты вместо «бусин»
    for (int a = 110; a <= 250; a += 2) {
      float r = a * 3.14159265f / 180.0f;
      int x1 = px + (int)(pr * cos(r)), y1 = py + (int)(pr * sin(r));
      int x2 = px + (int)((pr - 3) * cos(r)), y2 = py + (int)((pr - 3) * sin(r));
      if (x1 >= 0 && x1 < SCREEN_WIDTH && y1 >= 0 && y1 < SCREEN_HEIGHT &&
          x2 >= 0 && x2 < SCREEN_WIDTH && y2 >= 0 && y2 < SCREEN_HEIGHT) {
        spr.drawLine(x1, y1, x2, y2, COLOR_HUD_GLOW);
      }
    }
  } else if (currentLevel == 6) {
    // Уровень 7: северное сияние
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_NORTH_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_NORTH_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_NORTH_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    // волнистые полосы сияния
    for (int w = 0; w < 3; w++) {
      uint16_t col = (w == 0) ? tft.color565(130, 255, 190)
                    : (w == 1 ? tft.color565(80, 210, 210) : tft.color565(60, 150, 170));
      int prevX = 0, prevY = 0;
      for (int xx = 0; xx <= SCREEN_WIDTH; xx += 4) {
        int yy = h * (18 + w * 9) / 100 + (int)(9 * sin((xx + w * 50) * 0.05f));
        if (xx > 0) spr.drawLine(prevX, prevY, xx, yy, col);
        prevX = xx; prevY = yy;
      }
    }
    // тёмный хвойный лес
    int seed = 0;
    for (int bx = -6; bx < SCREEN_WIDTH; bx += 10) {
      int bh = 16 + (seed * 13) % 22;
      spr.fillTriangle(bx, h, bx + 7, h - bh, bx + 14, h, TFT_BLACK);
      seed++;
    }
  } else if (currentLevel == 7) {
    // Уровень 8: дюны и две луны
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_DUNE_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_DUNE_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_DUNE_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    spr.fillCircle(SCREEN_WIDTH * 30 / 100, h * 22 / 100, h / 9, tft.color565(250, 230, 210));
    spr.fillCircle(SCREEN_WIDTH * 45 / 100, h * 14 / 100, h / 14, tft.color565(235, 215, 200));
    spr.fillTriangle(0, h, SCREEN_WIDTH * 25 / 100, h * 70 / 100, SCREEN_WIDTH * 55 / 100, h, TFT_BLACK);
    spr.fillTriangle(SCREEN_WIDTH * 40 / 100, h, SCREEN_WIDTH * 70 / 100, h * 74 / 100, SCREEN_WIDTH, h, TFT_BLACK);
  } else if (currentLevel == 8) {
    // Уровень 9: астероидное поле
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_AST_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_AST_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_AST_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    int axp[5] = {15, 40, 62, 80, 30};
    int ayp[5] = {35, 25, 45, 30, 55};
    int ar[5] = {7, 5, 9, 6, 4};
    for (int i = 0; i < 5; i++) spr.fillCircle(SCREEN_WIDTH * axp[i] / 100, h * ayp[i] / 100, ar[i], TFT_BLACK);
    spr.fillTriangle(SCREEN_WIDTH * 18 / 100, h * 62 / 100, SCREEN_WIDTH * 28 / 100, h * 52 / 100,
                     SCREEN_WIDTH * 34 / 100, h * 64 / 100, TFT_BLACK);
  } else if (currentLevel == 9) {
    // Уровень 10: станция
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_ST_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_ST_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_ST_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    spr.fillCircle(SCREEN_WIDTH * 82 / 100, h * 25 / 100, h / 10, tft.color565(60, 90, 160)); // планета
    spr.fillRect(SCREEN_WIDTH * 20 / 100, h * 58 / 100, SCREEN_WIDTH * 34 / 100, h * 8 / 100, TFT_BLACK);  // корпус
    spr.fillRect(SCREEN_WIDTH * 30 / 100, h * 48 / 100, SCREEN_WIDTH * 10 / 100, h * 12 / 100, TFT_BLACK); // надстройка
    spr.fillRect(SCREEN_WIDTH * 34 / 100, h * 42 / 100, SCREEN_WIDTH * 2 / 100, h * 6 / 100, TFT_BLACK);
    spr.fillCircle(SCREEN_WIDTH * 24 / 100, h * 62 / 100, 1, COLOR_HUD_GLOW);
    spr.fillCircle(SCREEN_WIDTH * 36 / 100, h * 62 / 100, 1, COLOR_HUD_GLOW);
  } else if (currentLevel == 10) {
    // Уровень 11: рассвет над океаном
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_DAWN_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_DAWN_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_DAWN_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    spr.fillCircle(SCREEN_WIDTH * 55 / 100, h * 60 / 100, h / 9, tft.color565(255, 220, 170));
    spr.fillRect(0, h * 68 / 100, SCREEN_WIDTH, h - h * 68 / 100, tft.color565(30, 40, 80)); // океан
    for (int k = 0; k < 4; k++)
      spr.drawFastHLine(SCREEN_WIDTH * 48 / 100 - k * 10, h * 72 / 100 + k * 5,
                        SCREEN_WIDTH * 14 / 100 + k * 20, tft.color565(255, 200, 160));
    spr.drawFastHLine(0, h * 60 / 100, SCREEN_WIDTH, TFT_BLACK); // горизонт
  } else if (currentLevel == 11) {
    // Уровень 12: газовый гигант
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_GAS_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_GAS_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_GAS_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    int gpx = SCREEN_WIDTH * 70 / 100, gpy = h * 45 / 100, gpr = h * 26 / 100;
    spr.fillCircle(gpx, gpy, gpr, tft.color565(130, 100, 170));
    for (int k = -2; k <= 2; k++)
      spr.drawFastHLine(gpx - gpr + 5, gpy + k * gpr / 5, 2 * gpr - 10, tft.color565(100, 80, 140));
    drawEllipseOutline(gpx, gpy, gpr + 16, gpr / 3, tft.color565(220, 160, 90)); // кольцо
  } else if (currentLevel == 5) {
    // Уровень 6: лавовая планета
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_LAVA_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_LAVA_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_LAVA_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    spr.fillTriangle(0, h, SCREEN_WIDTH * 22 / 100, h * 68 / 100, SCREEN_WIDTH * 44 / 100, h, TFT_BLACK);
    spr.fillTriangle(SCREEN_WIDTH * 34 / 100, h, SCREEN_WIDTH * 58 / 100, h * 62 / 100, SCREEN_WIDTH * 84 / 100, h, TFT_BLACK);
    spr.fillTriangle(SCREEN_WIDTH * 70 / 100, h, SCREEN_WIDTH * 88 / 100, h * 70 / 100, SCREEN_WIDTH, h, TFT_BLACK);
    // лавовые потоки на каждой горе
    spr.drawLine(SCREEN_WIDTH * 22 / 100, h * 68 / 100, SCREEN_WIDTH * 20 / 100, h * 80 / 100, COLOR_LAVA_FIRE);
    spr.drawLine(SCREEN_WIDTH * 20 / 100, h * 80 / 100, SCREEN_WIDTH * 25 / 100, h * 90 / 100, COLOR_LAVA_FIRE);
    spr.drawLine(SCREEN_WIDTH * 58 / 100, h * 62 / 100, SCREEN_WIDTH * 60 / 100, h * 74 / 100, COLOR_LAVA_FIRE);
    spr.drawLine(SCREEN_WIDTH * 55 / 100, h * 78 / 100, SCREEN_WIDTH * 62 / 100, h * 86 / 100, COLOR_LAVA_FIRE);
    spr.drawLine(SCREEN_WIDTH * 88 / 100, h * 70 / 100, SCREEN_WIDTH * 86 / 100, h * 82 / 100, COLOR_LAVA_FIRE);
    spr.drawLine(SCREEN_WIDTH * 86 / 100, h * 82 / 100, SCREEN_WIDTH * 91 / 100, h * 92 / 100, COLOR_LAVA_FIRE);
  } else if (currentLevel == 3) {
    // Уровень 4: токсичный лес
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_TOX_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_TOX_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_TOX_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    int seed = 0;
    for (int bx = -6; bx < SCREEN_WIDTH; bx += 12) {
      int bh = 18 + (seed * 11) % 24;
      spr.fillTriangle(bx, h, bx + 8, h - bh, bx + 16, h, TFT_BLACK);
      seed++;
    }
  } else if (currentLevel == 2) {
    // Уровень 3: ледяной мир
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_ICE_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_ICE_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_ICE_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    int px = SCREEN_WIDTH * 70 / 100, py = h * 38 / 100, pr = h / 9;
    spr.fillCircle(px, py, pr, tft.color565(40, 70, 110));
    drawEllipseOutline(px, py, pr + 9, pr / 3, tft.color565(150, 200, 230));
    spr.fillTriangle(0, h, SCREEN_WIDTH * 20 / 100, h * 72 / 100, SCREEN_WIDTH * 42 / 100, h, TFT_BLACK);
    spr.fillTriangle(SCREEN_WIDTH * 32 / 100, h, SCREEN_WIDTH * 56 / 100, h * 66 / 100, SCREEN_WIDTH * 82 / 100, h, TFT_BLACK);
    spr.fillTriangle(SCREEN_WIDTH * 68 / 100, h, SCREEN_WIDTH * 86 / 100, h * 70 / 100, SCREEN_WIDTH, h, TFT_BLACK);
  } else {
    // Уровень 1: классический закат
    spr.fillRect(0, 0, SCREEN_WIDTH, h * 30 / 100, COLOR_SKY_TOP);
    spr.fillRect(0, h * 30 / 100, SCREEN_WIDTH, h * 60 / 100 - h * 30 / 100, COLOR_SKY_MID);
    spr.fillRect(0, h * 60 / 100, SCREEN_WIDTH, h - h * 60 / 100, COLOR_SKY_BOT);
    for (int i = 0; i < NUM_STARS; i++) spr.drawPixel((int)stars[i].x, (int)stars[i].y, TFT_WHITE);
    spr.fillCircle(SCREEN_WIDTH * 80 / 100, h * 55 / 100, h / 7, COLOR_SUN);
    spr.fillTriangle(0, h, SCREEN_WIDTH * 16 / 100, h * 74 / 100, SCREEN_WIDTH * 35 / 100, h, COLOR_MOUNTAIN);
    spr.fillTriangle(SCREEN_WIDTH * 25 / 100, h, SCREEN_WIDTH * 48 / 100, h * 68 / 100, SCREEN_WIDTH * 70 / 100, h, COLOR_MOUNTAIN);
    spr.fillTriangle(SCREEN_WIDTH * 60 / 100, h, SCREEN_WIDTH * 82 / 100, h * 65 / 100, SCREEN_WIDTH, h, COLOR_MOUNTAIN);
  }
}

// Боевые силуэты кораблей (тёмный корпус + акцент игрока).
// 0 = BLADE (баланс), 1 = TITAN (тяжёлый), 2 = NEEDLE (быстрый), иначе Blade.
void drawShipBody(int kind, int x, int y, int s, uint16_t accent) {
  int h = s / 2;
  if (kind == 1) {
    // TITAN: тяжёлый, блочный, с двумя боковыми плавниками
    spr.fillRect(x - s / 3, y - h / 2, s * 3 / 4, h, TFT_BLACK);
    spr.fillRect(x - s / 6, y - h, s / 2, h * 2 / 3, TFT_BLACK);
    spr.fillRect(x - s / 6, y + h / 3, s / 2, h * 2 / 3, TFT_BLACK);
    spr.fillRect(x - s / 6, y - h, 2, h * 2 / 3, accent);
    spr.fillRect(x - s / 6, y + h / 3, 2, h * 2 / 3, accent);
    spr.fillTriangle(x + s, y, x + s / 3, y - h / 2, x + s / 3, y + h / 2, TFT_BLACK);
    spr.fillCircle(x + s / 2, y, 2, accent);
  } else if (kind == 2) {
    // NEEDLE: быстрый, длинный тонкий дротик
    int nh = s / 3;
    spr.fillTriangle(x + s, y, x + s / 3, y - nh, x + s / 3, y + nh, TFT_BLACK);
    spr.fillTriangle(x + s, y, x + s * 2 / 3, y - nh / 2, x + s * 2 / 3, y + nh / 2, accent);
    spr.fillRect(x - s, y - 1, s - s / 3, 2, TFT_BLACK);
  } else {
    // BLADE: баланс, стреловидный с ромбом
    spr.fillTriangle(x + s, y, x + s / 4, y - h * 2 / 3, x + s / 4, y + h * 2 / 3, TFT_BLACK);
    spr.fillTriangle(x + s / 4, y, x - s / 4, y, x, y - h, accent);
    spr.fillTriangle(x + s / 4, y, x - s / 4, y, x, y + h, accent);
    spr.drawFastHLine(x - s, y, s, TFT_BLACK);
  }
}

void drawPlayerShip(int kind, int x, int y, int w, uint16_t accent, bool falling, bool dead) {
  int s = w;
  if (dead) {
    spr.fillCircle(x, SCREEN_HEIGHT - 6, 3, TFT_DARKGREY);
    spr.fillCircle(x + 4, SCREEN_HEIGHT - 8, 2, TFT_RED);
    return;
  }
  drawShipBody(kind, x, y, s, accent);
  if (falling) {
    spr.fillCircle(x - s / 2, y + ((int)(timeCnt * 20) % 4 - 2), 4, TFT_DARKGREY);
    spr.fillCircle(x - s / 3, y - 4, 3, TFT_ORANGE);
    spr.fillCircle(x, y + 2, 2, TFT_RED);
  } else {
    if ((int)(timeCnt * 12) % 2 == 0)
      spr.fillTriangle(x - s / 4, y - 2, x - s * 6 / 10, y, x - s / 4, y + 2, TFT_ORANGE);
  }
}

bool onScreen(int x, int y, int pad) {
  return x > -pad && x < SCREEN_WIDTH + pad && y > -pad && y < SCREEN_HEIGHT + pad;
}

void drawEnemySaucer(int x, int y) {
  if (!onScreen(x, y, 16)) return;
  spr.fillEllipse(x + 6, y + 1, 11, 4, TFT_BLACK);   // корпус-тень
  spr.fillCircle(x + 6, y - 3, 5, TFT_BLACK);        // купол
  spr.fillCircle(x + 6, y - 3, 2, COLOR_HUD_GLOW);   // иллюминатор
  spr.fillCircle(x, y + 1, 1, COLOR_SAUCER);         // огни обода
  spr.fillCircle(x + 6, y + 2, 1, COLOR_SAUCER);
  spr.fillCircle(x + 12, y + 1, 1, COLOR_SAUCER);
}

void drawEnemyDrone(int x, int y) {
  if (!onScreen(x, y, 16)) return;
  spr.fillRect(x - 2, y - 1, 16, 3, TFT_BLACK);      // несущая балка
  spr.fillCircle(x + 6, y, 4, TFT_BLACK);            // корпус
  spr.fillCircle(x + 6, y, 1, TFT_RED);              // глаз
  spr.fillCircle(x - 1, y, 3, TFT_BLACK);            // винтовые диски
  spr.fillCircle(x + 13, y, 3, TFT_BLACK);
  spr.fillCircle(x - 1, y, 1, COLOR_DRONE);
  spr.fillCircle(x + 13, y, 1, COLOR_DRONE);
}

void drawEnemyBeetle(int x, int y, int hp) {
  if (!onScreen(x, y, 18)) return;
  spr.fillEllipse(x + 4, y, 9, 6, TFT_BLACK);            // тело
  spr.fillCircle(x - 5, y, 4, TFT_BLACK);                // голова (вперёд, влево)
  spr.drawLine(x - 5, y - 4, x - 11, y - 9, TFT_BLACK);  // усики
  spr.drawLine(x - 5, y + 4, x - 11, y + 9, TFT_BLACK);
  for (int k = -1; k <= 1; k++)
    spr.drawLine(x + 4, y + k * 4, x + 10, y + k * 4 + 4, TFT_BLACK); // лапки
  spr.fillCircle(x - 5, y - 1, 1, TFT_RED);              // глаза
  spr.fillCircle(x - 5, y + 1, 1, TFT_RED);
  spr.drawLine(x + 4, y - 6, x + 4, y + 6, hp < 2 ? COLOR_HUD_DIM : COLOR_SAUCER); // полоса
}

void drawEnemyPod(int x, int y) {
  if (!onScreen(x, y, 16)) return;
  spr.fillEllipse(x + 5, y, 9, 7, TFT_BLACK);            // тело-стручок
  spr.fillTriangle(x + 14, y, x + 5, y - 4, x + 5, y + 4, TFT_BLACK); // шип вперёд
  spr.fillCircle(x + 4, y, 2, COLOR_HUD_GLOW);           // ядро
  spr.drawLine(x - 1, y - 7, x - 5, y - 11, TFT_BLACK);  // усики
  spr.drawLine(x - 1, y + 7, x - 5, y + 11, TFT_BLACK);
}

void drawEnemyInterceptor(int x, int y) {
  if (!onScreen(x, y, 16)) return;
  spr.fillTriangle(x + 14, y, x - 6, y - 6, x - 6, y + 6, TFT_BLACK);   // стрела
  spr.fillTriangle(x + 14, y, x + 4, y - 3, x + 4, y + 3, COLOR_DRONE); // акцент-нос
  spr.drawLine(x - 6, y - 6, x - 12, y, TFT_BLACK);
  spr.drawLine(x - 6, y + 6, x - 12, y, TFT_BLACK);
}

void drawEnemyMine(int x, int y) {
  if (!onScreen(x, y, 16)) return;
  bool blink = ((int)(timeCnt * 6) % 2) == 0;
  for (int k = 0; k < 8; k++) {                          // шипы-лучи
    float a = k * 0.7853982f;
    spr.drawLine(x + 6, y, x + 6 + (int)(9 * cos(a)), y + (int)(9 * sin(a)), TFT_BLACK);
  }
  spr.fillCircle(x + 6, y, 5, TFT_BLACK);                // корпус
  spr.fillCircle(x + 6, y, 2, blink ? TFT_RED : COLOR_HUD_DIM); // мигающий взрыватель
}

void drawEnemyCruiser(int x, int y) {
  if (!onScreen(x, y, 22)) return;
  spr.fillRect(x - 8, y - 3, 30, 6, TFT_BLACK);                        // корпус
  spr.fillRect(x - 2, y - 9, 14, 6, TFT_BLACK);                        // рубка
  spr.fillRect(x - 8, y + 3, 16, 4, TFT_BLACK);                        // нижняя надстройка
  spr.fillTriangle(x + 22, y - 3, x + 30, y, x + 22, y + 3, TFT_BLACK); // нос
  spr.fillRect(x + 1, y - 8, 7, 3, COLOR_DRONE);                       // окна рубки
  spr.fillCircle(x + 24, y, 1, TFT_RED);
}

void drawEnemyRay(int x, int y) {
  if (!onScreen(x, y, 18)) return;
  spr.fillTriangle(x - 14, y, x + 4, y - 9, x + 4, y + 9, TFT_BLACK);   // крыло-ромб
  spr.fillTriangle(x + 4, y - 9, x + 10, y, x + 4, y + 9, TFT_BLACK);
  spr.drawLine(x + 10, y, x + 20, y, TFT_BLACK);                        // тонкий хвост
  spr.fillCircle(x - 2, y, 2, COLOR_SAUCER);                            // акцент
}

void drawEnemySwarm(int x, int y) {
  if (!onScreen(x, y, 18)) return;
  const int dx[4] = {-6, 6, -6, 6};
  const int dy[4] = {-6, -6, 6, 6};
  for (int k = 0; k < 4; k++) {
    int cx = x + 6 + dx[k], cy = y + dy[k];
    spr.fillTriangle(cx, cy - 4, cx - 4, cy, cx + 4, cy, TFT_BLACK);
    spr.fillTriangle(cx, cy + 4, cx - 4, cy, cx + 4, cy, TFT_BLACK);
    spr.fillCircle(cx, cy, 1, COLOR_HUD_GLOW);
  }
}

void drawEnemyComet(int x, int y) {
  if (!onScreen(x, y, 20)) return;
  for (int k = 0; k < 3; k++) spr.drawFastHLine(x + 8, y - 4 + k * 4, 16 - k * 4, COLOR_HUD_DIM); // шлейф
  spr.fillCircle(x + 2, y, 7, TFT_BLACK);                                // ядро
  spr.fillRect(x - 4, y - 3, 8, 6, TFT_BLACK);
  spr.fillCircle(x - 1, y, 2, COLOR_HUD_GLOW);
}

void drawEnemyDrill(int x, int y) {
  if (!onScreen(x, y, 20)) return;
  spr.fillTriangle(x - 14, y, x + 2, y - 7, x + 2, y + 7, TFT_BLACK);       // нос-бур
  spr.fillRect(x + 2, y - 7, 14, 14, TFT_BLACK);
  spr.fillTriangle(x + 16, y - 7, x + 24, y - 12, x + 16, y - 1, TFT_BLACK); // плавники
  spr.fillTriangle(x + 16, y + 7, x + 24, y + 12, x + 16, y + 1, TFT_BLACK);
  spr.fillCircle(x + 1, y - 3, 1, COLOR_HUD_GLOW);
  spr.fillCircle(x + 1, y, 1, COLOR_HUD_GLOW);
  spr.fillCircle(x + 1, y + 3, 1, COLOR_HUD_GLOW);
}

void drawEnemyGhost(int x, int y) {
  if (!onScreen(x, y, 18)) return;
  spr.fillCircle(x + 6, y - 2, 9, TFT_BLACK);
  spr.fillRect(x - 3, y - 2, 18, 10, TFT_BLACK);
  for (int k = 0; k < 3; k++) {
    int gx = x - 3 + k * 6;
    spr.fillTriangle(gx, y + 8, gx + 3, y + 13, gx + 6, y + 8, TFT_BLACK);  // рваный низ
  }
  spr.fillCircle(x + 2, y - 2, 1, TFT_CYAN);
  spr.fillCircle(x + 10, y - 2, 1, TFT_CYAN);
}

void drawEnemySatellite(int x, int y) {
  if (!onScreen(x, y, 20)) return;
  spr.fillRect(x - 8, y - 10, 4, 20, TFT_BLACK);   // панели
  spr.fillRect(x + 10, y - 10, 4, 20, TFT_BLACK);
  spr.fillRect(x - 2, y - 6, 10, 12, TFT_BLACK);   // центральный блок
  spr.fillCircle(x + 3, y - 8, 2, COLOR_HUD_GLOW); // маяк
  spr.fillCircle(x + 3, y, 1, TFT_CYAN);
}

void drawEnemyBastion(int x, int y) {
  if (!onScreen(x, y, 20)) return;
  spr.fillTriangle(x - 12, y, x + 4, y - 10, x + 4, y + 10, TFT_BLACK);  // клин-нос
  spr.fillRect(x + 4, y - 10, 12, 20, TFT_BLACK);                        // корпус
  spr.fillRect(x + 16, y - 8, 8, 5, TFT_BLACK);                          // выступы
  spr.fillRect(x + 16, y + 3, 8, 5, TFT_BLACK);
  spr.fillRect(x + 6, y - 6, 6, 12, COLOR_HUD_DIM);                      // панель
  spr.fillCircle(x + 9, y, 2, COLOR_HUD_GLOW);
}

void drawEnemySickle(int x, int y) {
  if (!onScreen(x, y, 18)) return;
  // серп: сегмент кольца, открытый вправо
  for (int a = 130; a < 230; a += 10) {
    float r1 = a * 0.0174533f, r2 = (a + 10) * 0.0174533f;
    int x1 = x + 6 + (int)(13 * cos(r1)), y1 = y + (int)(13 * sin(r1));
    int x2 = x + 6 + (int)(13 * cos(r2)), y2 = y + (int)(13 * sin(r2));
    int x3 = x + 6 + (int)(7 * cos(r2)), y3 = y + (int)(7 * sin(r2));
    int x4 = x + 6 + (int)(7 * cos(r1)), y4 = y + (int)(7 * sin(r1));
    spr.fillTriangle(x1, y1, x2, y2, x4, y4, TFT_BLACK);
    spr.fillTriangle(x2, y2, x3, y3, x4, y4, TFT_BLACK);
  }
  spr.fillCircle(x + 6, y, 1, TFT_ORANGE);
}

void drawEnemyGuardian(int x, int y) {
  if (!onScreen(x, y, 22)) return;
  spr.fillCircle(x + 6, y, 10, TFT_BLACK);   // глаз
  spr.fillCircle(x + 6, y, 6, TFT_CYAN);     // радужка
  spr.fillCircle(x + 6, y, 3, TFT_BLACK);    // зрачок
  spr.fillTriangle(x - 8, y - 3, x - 16, y, x - 8, y + 3, TFT_BLACK);
  spr.fillTriangle(x + 20, y - 3, x + 28, y, x + 20, y + 3, TFT_BLACK);
}

void drawRockets() {
  for (int i = 0; i < MAX_ROCKETS; i++) {
    if (!rockets[i].active) continue;
    int rx = sx(rockets[i].x), ry = sy(rockets[i].y);
    if (!onScreen(rx, ry, 28)) continue;
    uint8_t k = rockets[i].kind;
    // Гидра-носитель (2) — крупная, части (3) — мелкие
    uint16_t body = (k == 1) ? COLOR_SAUCER : (k == 3 ? TFT_YELLOW : (k == 2 ? TFT_ORANGE : TFT_RED));
    uint16_t nose = (k == 3) ? TFT_RED : TFT_WHITE;
    int len = (k == 2) ? 22 : (k == 3 ? 8 : 18);
    int half = (k == 2) ? 3 : (k == 3 ? 1 : 2);
    if (k != 3) {
      spr.drawFastHLine(rx - 20, ry, 10, COLOR_HUD_DIM);   // шлейф
      spr.drawFastHLine(rx - 12, ry, 8, TFT_ORANGE);
    }
    spr.fillRect(rx, ry - half, len, half * 2, body);
    spr.fillTriangle(rx + len, ry, rx + len - len / 3, ry - half - 1,
                     rx + len - len / 3, ry + half + 1, nose);
    if (k != 3) spr.fillTriangle(rx, ry - half, rx - 6, ry, rx, ry + half, TFT_ORANGE);
  }
}

void drawExplosions() {
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (explosions[i].life == 0) continue;
    int ex = sx(explosions[i].x), ey = sy(explosions[i].y);
    int base = explosions[i].big ? 8 : 5;
    int r = base + (5 - explosions[i].life) * (explosions[i].big ? 4 : 2);
    uint16_t c;
    switch (explosions[i].life) {
      case 5: c = TFT_WHITE; break;
      case 4: c = TFT_YELLOW; break;
      case 3: c = COLOR_HUD_GLOW; break;
      case 2: c = COLOR_SKY_BOT; break;
      default: c = COLOR_HUD_DIM; break;
    }
    spr.drawCircle(ex, ey, r, c);
    if (explosions[i].life >= 4) spr.fillCircle(ex, ey, r / 2, c);
  }
}

void drawShards() {
  for (int i = 0; i < MAX_SHARDS; i++) {
    if (!shards[i].active) continue;
    int x = sx(shards[i].x), y = sy(shards[i].y);
    if (!onScreen(x, y, 6)) continue;
    spr.fillRect(x, y, 3, 3, shards[i].fromRemote ? TFT_CYAN : TFT_ORANGE);
  }
}

// Босс «Медуза»: тёмный купол со светящимися глазами и волнистыми щупальцами
void drawBoss() {
  if (!bossActive) return;
  int bx = sx(BOSS_WX), by = sy(bossY);
  if (bossType == 5) {
    // Колосс: огромный глаз в кольце
    spr.fillCircle(bx, by, 26, TFT_BLACK);
    drawEllipseOutline(bx, by, 42, 12, TFT_BLACK);
    spr.fillCircle(bx, by, 18, COLOR_HUD_GLOW);
    spr.fillCircle(bx, by, 12, TFT_CYAN);
    spr.fillCircle(bx, by, 6, TFT_BLACK);
    spr.fillCircle(bx, by, 2, TFT_WHITE);
  } else if (bossType == 4) {
    // Ядро: шипастая звезда с раскалённым центром
    for (int k = 0; k < 10; k++) {
      float a = k * 0.3141593f;
      int rx1 = bx + (int)(30 * cos(a)), ry1 = by + (int)(30 * sin(a));
      int rx2 = bx + (int)(14 * cos(a + 0.15f)), ry2 = by + (int)(14 * sin(a + 0.15f));
      spr.fillTriangle(bx, by, rx1, ry1, rx2, ry2, TFT_BLACK);
    }
    spr.fillCircle(bx, by, 12, COLOR_HUD_GLOW);
    spr.fillCircle(bx, by, 7, TFT_ORANGE);
    spr.fillCircle(bx, by, 3, TFT_RED);
  } else if (bossType == 3) {
    // Жнец: клинки-крылья, оранжевый «глаз»
    spr.fillTriangle(bx - 44, by, bx + 6, by - 12, bx + 6, by + 12, TFT_BLACK);   // корпус
    spr.fillTriangle(bx + 6, by - 12, bx + 30, by - 6, bx + 8, by, TFT_BLACK);
    spr.fillTriangle(bx + 6, by + 12, bx + 30, by + 6, bx + 8, by, TFT_BLACK);
    spr.fillTriangle(bx - 4, by - 8, bx - 30, by - 30, bx + 18, by - 14, TFT_BLACK); // верхний клинок
    spr.fillTriangle(bx - 4, by + 8, bx - 30, by + 30, bx + 18, by + 14, TFT_BLACK); // нижний клинок
    spr.drawLine(bx + 28, by, bx + 44, by - 6, TFT_BLACK);                           // хвост
    spr.drawLine(bx + 28, by, bx + 44, by + 6, TFT_BLACK);
    spr.fillCircle(bx - 24, by, 3, TFT_ORANGE);                                      // глаз
    spr.fillCircle(bx + 4, by - 6, 1, TFT_CYAN);
    spr.fillCircle(bx + 4, by + 6, 1, TFT_CYAN);
  } else if (bossType == 2) {
    // Королева роя: вытянутое тело, веерный хвост, усики, глаза
    spr.fillEllipse(bx, by, 32, 15, TFT_BLACK);
    spr.fillTriangle(bx - 32, by - 10, bx - 52, by, bx - 32, by + 10, TFT_BLACK);       // нос
    spr.fillTriangle(bx + 26, by - 12, bx + 50, by - 20, bx + 34, by, TFT_BLACK);       // хвост-веер
    spr.fillTriangle(bx + 26, by + 12, bx + 50, by + 20, bx + 34, by, TFT_BLACK);
    spr.fillTriangle(bx - 10, by - 15, bx + 6, by - 30, bx + 12, by - 12, TFT_BLACK);   // верхний плавник
    spr.fillTriangle(bx - 10, by + 15, bx + 6, by + 30, bx + 12, by + 12, TFT_BLACK);   // нижний плавник
    spr.drawLine(bx - 40, by - 6, bx - 54, by - 14, TFT_BLACK);                          // усики
    spr.drawLine(bx - 40, by + 6, bx - 54, by + 14, TFT_BLACK);
    spr.fillCircle(bx - 16, by - 4, 2, TFT_CYAN);
    spr.fillCircle(bx - 16, by + 4, 2, TFT_CYAN);
    spr.fillCircle(bx + 2, by - 2, 2, COLOR_SAUCER);
    spr.fillCircle(bx + 2, by + 2, 2, COLOR_SAUCER);
    spr.fillCircle(bx - 54, by - 14, 1, TFT_CYAN);
    spr.fillCircle(bx - 54, by + 14, 1, TFT_CYAN);
  } else if (bossType == 1) {
    // Дредноут: длинный корпус, надстройка, две пушки
    spr.fillRect(bx - 34, by - 7, 66, 14, TFT_BLACK);                            // корпус
    spr.fillTriangle(bx - 34, by - 7, bx - 50, by, bx - 34, by + 7, TFT_BLACK);  // нос
    spr.fillRect(bx - 8, by - 16, 26, 9, TFT_BLACK);                             // надстройка
    spr.fillRect(bx + 2, by - 22, 10, 6, TFT_BLACK);
    spr.fillRect(bx - 44, by - 9, 16, 4, TFT_BLACK);                             // пушка 1
    spr.fillRect(bx - 44, by + 5, 16, 4, TFT_BLACK);                             // пушка 2
    spr.fillCircle(bx - 20, by, 2, TFT_RED);
    spr.fillCircle(bx - 6, by, 2, COLOR_HUD_GLOW);
    spr.fillCircle(bx + 12, by, 2, COLOR_HUD_GLOW);
    spr.fillCircle(bx + 22, by, 2, COLOR_HUD_GLOW);
  } else {
    // Медуза: купол со щупальцами
    int R = 22;
    for (int k = -2; k <= 2; k++) {
      int lx = bx + k * 8;
      int len = 18 + (int)(8 * (1 + sin(timeCnt * 2 + k)));
      int sway = (int)(sin(timeCnt * 3 + k) * 3);
      spr.drawLine(lx, by + R * 3 / 4, lx + sway, by + R * 3 / 4 + len, TFT_BLACK);
      spr.fillCircle(lx + sway, by + R * 3 / 4 + len, 2, COLOR_SAUCER);
    }
    spr.fillEllipse(bx, by, R, R * 3 / 4, TFT_BLACK);
    spr.fillCircle(bx - 8, by - 2, 3, TFT_RED);
    spr.fillCircle(bx + 8, by - 2, 3, TFT_RED);
  }
  int barW = 60, barY = by - (bossType == 0 ? 42 : 48);
  spr.drawRect(bx - barW / 2, barY, barW, 5, COLOR_HUD_DIM);
  int fw = (int)((int32_t)barW * bossHp / bossMaxHp);
  spr.fillRect(bx - barW / 2, barY, fw, 5, TFT_RED);
}

void drawAcids() {
  uint16_t acidCol = tft.color565(130, 220, 60);
  for (int i = 0; i < MAX_ACID; i++) {
    if (!acids[i].active) continue;
    int x = sx(acids[i].x), y = sy(acids[i].y);
    if (!onScreen(x, y, 14)) continue;
    if (acids[i].kind == 1) {
      spr.fillRect(x, y - 1, 14, 3, TFT_RED);   // лазер
      spr.fillRect(x + 14, y, 4, 1, TFT_YELLOW);
    } else if (acids[i].kind == 2) {
      spr.fillCircle(x, y, 2, TFT_ORANGE);      // пуля Ската/Спутника
      spr.fillCircle(x, y, 1, TFT_YELLOW);
    } else if (acids[i].kind == 3) {
      spr.fillRect(x, y - 1, 10, 3, COLOR_SAUCER); // тяжёлый снаряд (Серп/Жнец/Страж)
      spr.fillCircle(x, y, 1, TFT_WHITE);
    } else if (acids[i].kind == 4) {
      spr.fillCircle(x, y, 3, TFT_MAGENTA);        // наводящийся снаряд Колосса
      spr.fillCircle(x, y, 1, TFT_WHITE);
    } else {
      spr.fillCircle(x, y, 3, acidCol);         // кислота
      spr.fillCircle(x, y, 1, TFT_WHITE);
    }
  }
}

void drawGame(bool coop) {
  drawBackground();
  if (!coop) player.x = HOST_WX;

  int py = player.falling ? sy(player.fallY) : sy(player.y);
    int ownKind = hasShip() ? selectedShip : -1;
    drawPlayerShip(ownKind, sx(player.x), py, player.width, player.color, player.falling, player.dead);

    if (coop && remoteSeen) {
      int ry = remote.falling ? sy(remote.fallY) : sy(remote.y);
      drawPlayerShip(remoteShip, sx(remote.x), ry, remote.width, remote.color, remote.falling, remote.dead);
    }

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    int bx = sx(bullets[i].x), by = sy(bullets[i].y);
    if (!onScreen(bx, by, 4)) continue;
    spr.fillRect(bx, by, 5, 2, bullets[i].fromRemote ? TFT_CYAN : TFT_YELLOW);
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    int ex = sx(enemies[i].x), ey = sy(enemies[i].y);
    if (enemies[i].type == ENEMY_SAUCER) drawEnemySaucer(ex, ey);
    else if (enemies[i].type == ENEMY_DRONE) drawEnemyDrone(ex, ey);
    else if (enemies[i].type == ENEMY_POD) drawEnemyPod(ex, ey);
    else if (enemies[i].type == ENEMY_INTERCEPTOR) drawEnemyInterceptor(ex, ey);
    else if (enemies[i].type == ENEMY_MINE) drawEnemyMine(ex, ey);
    else if (enemies[i].type == ENEMY_CRUISER) drawEnemyCruiser(ex, ey);
    else if (enemies[i].type == ENEMY_RAY) drawEnemyRay(ex, ey);
    else if (enemies[i].type == ENEMY_SWARM) drawEnemySwarm(ex, ey);
    else if (enemies[i].type == ENEMY_COMET) drawEnemyComet(ex, ey);
    else if (enemies[i].type == ENEMY_DRILL) drawEnemyDrill(ex, ey);
    else if (enemies[i].type == ENEMY_GHOST) drawEnemyGhost(ex, ey);
    else if (enemies[i].type == ENEMY_SATELLITE) drawEnemySatellite(ex, ey);
    else if (enemies[i].type == ENEMY_BASTION) drawEnemyBastion(ex, ey);
    else if (enemies[i].type == ENEMY_SICKLE) drawEnemySickle(ex, ey);
    else if (enemies[i].type == ENEMY_GUARDIAN) drawEnemyGuardian(ex, ey);
    else drawEnemyBeetle(ex, ey, enemies[i].hp);
  }

  drawBoss();
  drawAcids();
  drawRockets();
  drawShards();
  drawExplosions();

  spr.fillRect(0, 0, SCREEN_WIDTH, HUD_H, TFT_BLACK);
  spr.drawFastHLine(0, HUD_H, SCREEN_WIDTH, COLOR_HUD_MAIN);
  spr.setTextColor(COLOR_HUD_MAIN, TFT_BLACK);
  spr.setTextSize(1);
  spr.setCursor(4, 3);

  if (!coop) {
    spr.print("HP:");
    spr.print(max(0, player.hp));
    spr.setCursor(SCREEN_WIDTH - 88, 3);
    if (bossActive) {
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.print("BOSS:");
      spr.print(bossHp);
    } else {
      spr.print("K:");
      spr.print(matchKills);
      spr.print("/");
      spr.print(levelGoal(currentLevel));
    }
  } else {
    spr.print("HP:");
    spr.print(max(0, player.hp));
    spr.setCursor(SCREEN_WIDTH / 3, 3);
    if (remote.falling || remote.dead) {
      // на узком экране (240 px) длинная надпись наезжает на счётчик фрагов
      if (revivers > 0) spr.print(SCREEN_WIDTH >= 300 ? "HOLD BOTH:REVIVE" : "HOLD BOTH!");
      else spr.print("P2 DOWN");
    } else {
      spr.print("P2 HP:");
      spr.print(max(0, remote.hp));
    }
    spr.setCursor(SCREEN_WIDTH - 88, 3);
    if (bossActive) {
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.print("BOSS:");
      spr.print(bossHp);
    } else {
      spr.print("K:");
      spr.print(matchKills);
      spr.print("/");
      spr.print(levelGoal(currentLevel));
    }
  }

  // Индикатор ракет: R<заряжено>/<вместимость пода>
  spr.setCursor(SCREEN_WIDTH - 30, 3);
  int rkTotal = rocketsHoming + rocketsCluster + rocketsHydra;
  uint16_t rkCol = (activeRocket == 1) ? COLOR_SAUCER : (activeRocket == 2 ? TFT_CYAN : TFT_RED);
  if (rocketCapacity() > 0 && rkTotal > 0) {
    spr.setTextColor(((int)(timeCnt * 6) % 2 == 0) ? rkCol : COLOR_HUD_DIM, TFT_BLACK);
    spr.print("R");
    spr.setTextColor(COLOR_HUD_MAIN, TFT_BLACK);
    spr.print(rkTotal);
    spr.print("/");
    spr.print(rocketCapacity());
  } else {
    spr.setTextColor(COLOR_HUD_DIM, TFT_BLACK);
    spr.print("R-");
  }
  spr.pushSprite(0, 0);
}