#pragma once
#include <stdint.h>

struct __attribute__((packed)) EnemySnap {
  int16_t x;
  int16_t y;
  uint8_t kind;   // 0 saucer, 1 drone, 255 inactive
  uint8_t hp;
};

struct __attribute__((packed)) GamePacket {
  uint8_t type;        // 1 PING, 3 INPUT, 4 START, 5 WORLD, 6 REVIVE
  uint8_t y;           // Y отправителя (net-кодирование)
  uint8_t flags;       // bit0 = выстрел, bit1 = падение, bit2 = смерть отправителя
  uint8_t seq;         // счётчик tx-пакетов отправителя (монотонно растёт)
  uint8_t hostScore;   // очки хоста (заполняется хостом в WORLD)
  uint8_t clientScore; // очки клиента (заполняется хостом в WORLD)
  uint8_t matchCredits;// общий котёл кредитов (WORLD)
  uint8_t remoteHp;    // HP клиента (WORLD, авторитетно от хоста)
  uint8_t remoteFlags; // Статус falling/dead для клиента от хоста
  uint8_t senderHull;  // макс. HP корабля отправителя
  uint8_t senderHp;    // текущий HP отправителя (для отображения партнёра)
  uint8_t rocketKind;  // тип ракеты при установленном bit3 flags (0 homing, 1 cluster)
  uint8_t level;       // номер уровня (START / WORLD)
  uint8_t kills;       // убийств на уровне (WORLD / LEVEL_DONE)
  uint8_t progress;    // число пройденных уровней (PING / START)
  uint8_t senderShip;  // корабль отправителя (0 = нет, иначе индекс+1)
  uint8_t senderGun;   // уровень Gun Power отправителя (урон пуль)
  uint8_t boss;        // 1 = босс активен (WORLD)
  uint8_t bossHp;      // текущее HP босса (WORLD)
  uint8_t acidShot;    // 1 = босс выстрелил в этом кадре (WORLD)
  uint8_t bossType;    // 0 = Медуза, 1 = Дредноут, 2 = Королева роя (WORLD)
  uint8_t bossY;       // Y босса (net-кодирование), авторитетно от хоста
  uint8_t enemyFire;   // битовая маска: враг i выстрелил в этом кадре (WORLD)
  EnemySnap enemies[4];
};

struct ShipDef {
  const char *name;
  const char *role;
  int move;
  int fireMs;
  int hull;
};

struct Player {
  int x, y, width, height;
  uint16_t color;
  int score;
  int hp;
  bool falling;
  int fallY;
  bool dead;
};

struct Bullet { 
  int x, y; 
  bool active; 
  bool fromRemote; 
  uint8_t dmg;
};

enum EnemyType {
  ENEMY_SAUCER, ENEMY_DRONE, ENEMY_BEETLE, ENEMY_POD, ENEMY_INTERCEPTOR,
  ENEMY_MINE, ENEMY_CRUISER, ENEMY_RAY, ENEMY_SWARM, ENEMY_COMET,
  ENEMY_DRILL, ENEMY_GHOST, ENEMY_SATELLITE, ENEMY_BASTION, ENEMY_SICKLE,
  ENEMY_GUARDIAN
};

struct Enemy { 
  int x, y; 
  EnemyType type; 
  bool active; 
  int hp; 
  unsigned long nextFire;
};