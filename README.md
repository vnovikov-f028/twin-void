# TWIN VOID — co-op space shooter for LilyGo T-Display

TWIN VOID is a local two-player co-op space shooter for **LilyGo T-Display** (ESP32) and **T-Display S3** boards. The two boards connect directly over **ESP-NOW** (no router needed): one board is the host, the other is the client. Together you fight waves of enemies, clear levels, earn credits, upgrade your ship, and take down bosses.

- Platform: ESP32 / ESP32-S3 (Arduino)
- Display: 240×135 (T-Display 1.14") or 320×170 (T-Display S3)
- Link: ESP-NOW (channel 1)
- Created by **Volodymyr Novikov**
- License: MIT

> TWIN VOID is co-op only — two boards are required.

---

## Requirements

- Two LilyGo T-Display boards (both can be the same model, or a 1.14" and an S3 — they are compatible with each other).
- Arduino IDE + ESP32 core.
- The **TFT_eSPI** library with a correct `User_Setup` for your board.
- The same firmware build on both boards.
- Power and the two built-in buttons on each board.

### Build and flash

1. Open the sketch `TwinVoid/TwinVoid.ino` in Arduino IDE.
2. At the top of the file enable exactly **one** board block:
   ```cpp
   // #define BOARD_T_DISPLAY_114
   #define BOARD_T_DISPLAY_S3
   ```
   - `BOARD_T_DISPLAY_114` — 240×135, buttons 35/0.
   - `BOARD_T_DISPLAY_S3` — 320×170, buttons 14/0.
3. Select the ESP32 / ESP32-S3 board and flash.
4. Flash **both** boards with the same firmware.
5. Open Serial (115200). After boot you should see `ESP-NOW ready, channel 1`.

---

## Controls

### Main menu
| Action | Button |
|---|---|
| Next item | BTN2 |
| Select | BTN1 |

### Garage
| Action | Button |
|---|---|
| Select ship (before purchase) / upgrade | BTN2 |
| Buy ship / buy next upgrade level | BTN1 |
| Exit | hold BTN1 |

A ship can be bought **only once** and is locked afterwards. Once a ship is owned, upgrades become available.

### Store
| Action | Button |
|---|---|
| Next item | BTN2 |
| Buy | BTN1 |
| Exit | hold BTN1 |

To choose the active rocket for battle: highlight the desired rocket and exit the Store (hold BTN1). The active row is marked with `*`.

### Lobby
| Action | Button |
|---|---|
| Select level | BTN2 |
| Start game | BTN1 |
| Exit | hold BTN1 |

### In-game
| Action | Button |
|---|---|
| Move up / down | BTN1 / BTN2 |
| Fire a rocket | short press of BTN1+BTN2 together |
| Revive your partner | hold BTN1+BTN2 near the wreck |

Revive: fly to your partner's wreckage, hold both buttons and keep holding — after 2 seconds the partner is revived (if you have a reviver in your inventory).

---

## Progression and levels

Progress is stored locally on each board. Co-op uses the "weakest link" rule:

> available levels = **min(player 1 progress, player 2 progress) + 1**

For example, if one player has cleared 5 levels and the other has cleared 1, you can only play level 2 together. Levels can be replayed.

| # | Level | Goal | Enemies | Boss | Spawn intensity |
|---|---|---|---|---|---|
| 1 | Sunset | 40 | Saucer, Drone | — | 1.0 |
| 2 | Purple Night | 50 | Saucer, Drone, Beetle | — | 1.2 |
| 3 | Ice World | 50 | Saucer, Drone, Beetle | Medusa | 1.4 |
| 4 | Toxic Forest | 60 | Pod, Interceptor, Mine | — | 1.6 |
| 5 | Crescent | 60 | Mine, Drone, Cruiser | Dreadnought | 1.8 |
| 6 | Lava | 70 | Ray, Saucer, Pod | — | 2.0 |
| 7 | Northern | 70 | Swarm, Comet, Ray | Swarm Queen | 2.0 |
| 8 | Dunes | 80 | Drill, Ghost, Satellite | — | 2.0 |
| 9 | Asteroid | 80 | Bastion, Sickle | Reaper | 2.2 |
| 10 | Station | 90 | Bastion, Drill, Satellite | — | 2.2 |
| 11 | Dawn | 100 | Satellite, Sickle, Swarm | Core | 2.2 |
| 12 | Gas Giant | 120 | Guardian, Comet, Satellite | Colossus | 2.2 |

A level is completed when its kill goal is reached. On boss levels the boss appears after the required kills are reached, and the level is completed once the boss is destroyed.

---

## Player ships

Ships are chosen once at the start. A ship costs **999** credits; the starting balance is 1000.

| Ship | Role | HP | Speed | Fire interval | FIR (shots/s) |
|---|---|---|---|---|---|
| Blade | balanced | 3 | 40 | 350 ms | 3 |
| Titan | heavy | 6 | 22 | 520 ms | 2 |
| Needle | fast | 2 | 62 | 220 ms | 5 |

### Upgrades
| Upgrade | Effect | Max level | Cost per level |
|---|---|---|---|
| Armor | +1 HP | 5 | 100 / 200 / 300 / 400 / 500 |
| Launcher | +1 rocket slot | 8 | 50 / 100 / 150 / 200 / 250 / 300 / 350 / 400 |
| Gun Rate | +1 FIR | 3 | 100 / 200 / 300 |
| Gun Power | +1 bullet damage | 3 | 300 / 600 / 900 |

- Final HP = ship HP + Armor.
- FIR = base FIR + Gun Rate; fire interval = 1000 / FIR.
- Bullet damage = 1 + Gun Power.
- Rocket capacity = Launcher level (without a Launcher you cannot use rockets).

---

## Store

| Item | Price | Purpose |
|---|---|---|
| Reviver | 70 | Revives your partner (one per revive) |
| Homing rocket | 25 | Homing missile |
| Cluster rocket | 45 | Cluster missile |
| Hydra | 30 | Swarm missile |

Rockets are bought one at a time and the total cannot exceed your Launcher capacity.

---

## Rockets

| Rocket | Speed | Damage | Details |
|---|---|---|---|
| Homing | 32 | 8 | Homes on the nearest enemy; 55×40 blast |
| Cluster | 32 | 5 (blast) + 5 shards ×2 | 110×70 blast; detonates on impact or at mid-field; shards scatter in a fan |
| Hydra | 32 | parts ×1 | A carrier missile flies ~110 units, splits into 6 homing parts that fan out and home on **different** targets |

- Fire with a short press of both buttons.
- Rocket damage is resolved by the host; the client renders the visuals.

---

## Enemies

| Enemy | HP | Credits | Speed | Shooting | Contact damage | Notes |
|---|---|---|---|---|---|---|
| Saucer | 1 | 1 | 12 | — | 1 | bobs vertically |
| Drone | 1 | 1 | 12 | — | 1 | — |
| Beetle | 2 | 2 | 12 | — | 1 | — |
| Pod | 2 | 2 | 12 | — | 1 | — |
| Interceptor | 1 | 1 | 18 | — | 1 | fast |
| Mine | 1 | 1 | 10 | — | 2 (blast) | explodes when destroyed or on contact (90×60); chain detonation |
| Cruiser | 3 | 3 | 8 | — | 1 | heavy |
| Ray | 1 | 2 | 16 | bullet 25 / 1 | 1 | FIR 1 |
| Swarm | 2 | 2 | 14 | — | 2 | homes on a player after mid-field (ram) |
| Comet | 2 | 2 | 8 | — | 3 | trails |
| Drill | 3 | 3 | 8 | — | 2 | — |
| Ghost | 1 | 1 | 16 | — | 2 | — |
| Satellite | 4 | 4 | 8 | bullet 25 / 1 | 1 | FIR 1 |
| Bastion | 3 | 3 | 12 | — | 3 | — |
| Sickle | 3 | 4 | 14 | bullet 28 / 2 | 2 | FIR 1 |
| Guardian | 4 | 4 | 8 | bullet 28 / 2 | 2 | FIR 1 |

---

## Bosses

| Boss | Level | HP | Projectile | Speed | FIR | Behaviour |
|---|---|---|---|---|---|---|
| Medusa | 3 | 60 | acid | 25 | ~0.29/s | levitates |
| Dreadnought | 5 | 100 | laser | 25 | 1/s | levitates; fires alternately from two cannons |
| Swarm Queen | 7 | 120 | bolt | 25 | 1/s | levitates |
| Reaper | 9 | 150 | heavy bolt | 28 | 1.3/s | levitates |
| Core | 11 | 150 | heavy bolt | 28 | 1.3/s | levitates; fires from its center |
| Colossus | 12 | 200 | homing bolt | 35 | 1.3/s | levitates; projectiles steer toward a player |

The boss appears after the kill goal is reached. Killing a boss adds +25 credits to the shared pot and grants +100 score.

---

## Credits and economy

- Match credits are split **50/50** between the two players.
- Kills award credits based on the enemy type.
- Credits, upgrades, progress and inventory are stored in NVS (non-volatile flash).
- To reset progress, erase the flash / NVS.

---

## Technical notes

- Link: ESP-NOW, channel 1; broadcast discovery followed by direct peer-to-peer exchange.
- The **host** is authoritative for damage, kills and HP. The client receives the world state in WORLD packets.
- Incoming ESP-NOW packets are queued in the receive callback and processed in `loop()`.
- `GamePacket` is a fixed binary layout; both boards must run the same firmware version.

---

## Support the project

If you enjoy TWIN VOID, you can support development with a crypto donation.

**USDT (TRC20):** `TLuB6sNT98YGjFdmvPkrfGBVkvpvdnQ5U1`

![USDT-TRC20 donation QR](docs/donate-qr.png)

> Only send **USDT on the TRC20 (Tron)** network to this address.

---

## License

Released under the **MIT** license. See [LICENSE](LICENSE).
