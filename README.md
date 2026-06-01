<div align="center">

# 🤖 DeskBot v7

### *Your emotionally intelligent desk companion*

An ESP32-powered OLED robot with a living personality — it gets bored, angry, sleepy, and even dies dramatically if you ignore it long enough. Built with ❤️ and 6,600+ lines of hand-crafted C++.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg?style=for-the-badge&logo=espressif)](https://www.espressif.com/)
[![Display](https://img.shields.io/badge/Display-SH1106_OLED-white.svg?style=for-the-badge)](https://www.solomon-systech.com/)
[![Phone](https://img.shields.io/badge/Phone-Gadgetbridge-green.svg?style=for-the-badge)](https://gadgetbridge.org/)

---

```
    ╭─────────────╮
    │  ◉       ◉  │
    │     ───     │
    │    \___/    │
    ╰─────────────╯
      DeskBot v7
```

</div>

---

## ✨ What Is This?

DeskBot is a tiny robot that sits on your desk and *feels things*. It's not a smart assistant — it's a **companion**. It watches your phone notifications, reacts to your music, reminds you to drink water, and develops genuine emotional attachment based on how you treat it.

Leave it alone too long? It gets bored, then angry, then furious. Beat it (tap it too many times)? It gets knocked out with spinning cartoon stars. Pet it gently? It purrs with hearts floating up. Ignore it for days? It **dies dramatically** and reboots with amnesia.

> **This is not a utility. It's a relationship.**

---

## 🧠 The Emotional Engine

DeskBot runs a deeply layered emotional simulation — not random mood cycling, but a **reactive personality** with memory, escalation, and consequences.

### 🎭 Base Moods

| Mood | Visual | Trigger |
|------|--------|---------|
| 😊 **Alive** | Blinking, breathing, looking around | Default happy state |
| 💕 **Love** | Heart eyes, floating hearts | High affection + petting |
| 😡 **Angry** | Sharp brows, fire animation | Being beaten or ignored |
| 😢 **Sad** | Drooping eyes, falling tears | Low affection, rejection |
| 😵 **Dizzy** | Spiral eyes, wobbling | After being hit repeatedly |
| 🤩 **Excited** | Big eyes, bouncing body | New notifications, music |
| 😨 **Scared** | Shaking, sweat drops | Alarms, sudden events |
| 😏 **Smug** | Half-lidded, slight smirk | High affection flex |
| 🕐 **Clock** | Full-screen time display | Auto or knock pattern |
| 🌤️ **Weather** | Animated weather scene | Auto or knock pattern |

### 🎭 Overlay Expressions (10 total)

These layer *on top* of base moods for complex emotional blends:

```
Nervous ─── shaking body + sweat drops
Sleepy ──── drooping eyelids
Shocked ─── maxed wide eyes + jaw drop
Disgusted ─ squinted eye + curled lip
Embarrassed eyes looking away + blush circles
Thinking ── one brow up + side glance
Hyper ───── pupils dart randomly
Pain ────── X-eyes + zigzag mouth
Melting ──── face droops downward
Cry-Laugh ─ streaming tears + laughing mouth
```

### 📊 Emotional Memory System

```
Affection Score: -100 ◄────────────────────► +100
                 HATES                      ADORES

    ┌─────────────────────────────────────────────┐
    │  • Petting (long press)          → +3       │
    │  • Consoling when sad            → +5       │
    │  • Playing music                 → +1       │
    │  • Being beaten (rapid taps)     → -5       │
    │  • Being ignored (5min decay)    → -1       │
    │  • Dramatic death at -100        → RESET    │
    └─────────────────────────────────────────────┘
```

The bot **remembers** how you treat it with a 5-minute sliding memory window and persistent affection score. Its mood choices are weighted by your history together.

---

## 🎵 Music & Dance System

When your phone plays music, DeskBot comes alive with **genre-aware dancing**:

### 4 Dance Styles

| Style | Visual | Genre Match |
|-------|--------|-------------|
| 🕺 **Groove** | Smooth sway + relaxed bounce | Jazz, Soul, Hip-hop |
| 🪩 **Disco** | Heart eyes + sparkle ball | EDM, Funk, Pop |
| 🌊 **Wave** | Flowing wave motion | Ballad, Lofi, Classical |
| 🔥 **Hype** | X-eyes + aggressive bounce | Trap, Rock, Metal |

### Song Personality Engine

Every song gets a **unique dance fingerprint** derived from a hash of the track name + artist:

```
Track: "Bohemian Rhapsody" + Artist: "Queen"
         │
         ▼
    DJB2 Hash → Per-song parameters:
    ├── Beat interval:    430ms
    ├── Bounce amplitude: 3.2px
    ├── Arm swing:        35°
    ├── Head tilt:        5px
    ├── Note speed:       0.12
    ├── Mouth sensitivity: 0.9
    └── Genre tag:        "rock"
```

**9 genre detectors** with 80+ keyword matchers — from Beethoven to Playboi Carti, your bot dances differently to every song.

---

## 🔊 Sound Design

A custom **non-blocking tone sequencer** using ESP32 LEDC PWM drives a passive buzzer with warm, music-box-style melodies:

```
Boot     ─── C E G C' E' G'    (music box awakening)
Laugh    ─── high-low-high      (hahaha)
Cry      ─── E D C G...         (winding down music box)
Sneeze   ─── pip pip [silence] ACHOO!
Yawn     ─── rising ahhh → trailing zzz
Purr     ─── soft warm hum      (gentle vibration)
```

### 🛸 Rocky Vocabulary *(Project Hail Mary)*

Inspired by Rocky from Andy Weir's *Project Hail Mary* — the bot communicates through **pure melodic chords** instead of text:

| "Word" | Musical Phrase | Meaning |
|--------|---------------|---------|
| `AMAZE` | Triple ascending burst ×3 | "amaze amaze amaze!" |
| `GOOD` | Happy repeating note + lift | Approval |
| `QUESTION` | Rising phrase at end | Curiosity |
| `WORRY` | Wobbling between notes | Uncertainty |
| `NO NO NO` | Harsh low repeated drops | Disapproval |
| `SLEEPY` | Slow fading hum | Drifting off |
| `THANK` | Warm grateful two-note | Gratitude |

---

## 📱 Phone Integration (Gadgetbridge BLE)

DeskBot pairs with your Android phone via [Gadgetbridge](https://gadgetbridge.org/) over BLE UART, receiving:

| Data | Bot Reaction |
|------|-------------|
| **Notifications** | 🔔 Chime + fullscreen popup + jealousy system |
| **Incoming Calls** | 📞 Ring ripples + caller name + scared → happy/sad |
| **Music Info** | 🎵 Dance mode with genre-aware animations |
| **Weather** | 🌤️ Animated weather scenes (sun, rain, snow, fog, thunder) |
| **Time Sync** | 🕐 Full-screen clock with blinking colon |
| **Phone Battery** | 🔋 Battery level on clock face |
| **Alarms** | ⏰ Scared + shocked reaction |

### BLE Stability Features
- Zombie connection detection (90s stale timeout)
- Auto-restart with exponential backoff
- Connection parameter optimization
- Keepalive heartbeat every 10s
- Graceful disconnect cleanup

---

## 💪 Behavioral Systems

### 😒 Boredom Escalation
```
0 min ──────── Happy, doing quirks
    │
3 min ──────── Attention seeking: side-eye + sigh
5 min ──────── Bouncy expectation
8 min ──────── Dramatic slump
12 min ──────── Fake sleep (peeks one eye open)
    │
45 min ──────── BORED (dismissive eye-rolls)
55 min ──────── ANGRY (escalated frustration)
65 min ──────── FURIOUS (fire animation)
```

### 🥊 Getting Beaten (Rapid Tap Detection)

```
5 rapid taps ──── Stage 1: "Ouch!" (flinch)
8 rapid taps ──── Stage 2: Pain (X-eyes + zigzag mouth)
12 rapid taps ─── Stage 3: Crying hard
16 rapid taps ─── KNOCKOUT ⭐ (spinning stars, KO'd)
                      │
                      ▼
              Forgiveness Arc:
              Huffy → Side-eye → Tentative → Forgiven
              (requires gentle long-press to recover)
```

### 😻 Purring & Bliss

Hold the touch sensor continuously:
```
0s ──── Petting recognized
2s ──── Purring starts (warm hum + body sway)
3s+ ─── Hearts float up
        │
        └── Release → "Why did you stop?!" surprised face
```

### 💀 Dramatic Death

At affection score -100:
```
Phase 1: Shock ──── Wide eyes, frozen
Phase 2: Spiral ─── Eyes spiral inward
Phase 3: Flicker ── Screen flickers like old TV
Phase 4: Flatline ─ Heartbeat monitor → flatline
Phase 5: Reboot ─── Amnesia mode (confused, doesn't recognize you)
```

### 🧃 Health Reminders

| Reminder | Interval | Behavior |
|----------|----------|----------|
| 💧 Hydration | Every 45 min | Gets "thirsty" → urgent → desperate |
| 🪑 Posture | Every 30 min | Reminds you to sit up straight |

---

## 🔩 Hardware

### Components

| Part | Specification | Pin |
|------|--------------|-----|
| **MCU** | ESP32 DevKit | — |
| **Display** | SH1106 1.3" OLED (128×64, I2C) | SDA=21, SCL=22 |
| **Touch** | TTP223 Capacitive Touch Sensor | GPIO 4 |
| **Buzzer** | 3-pin Passive Buzzer Module | GPIO 25 |

### Wiring Diagram

```
ESP32                  SH1106 OLED
┌──────────┐          ┌──────────┐
│      3V3 ├──────────┤ VCC      │
│      GND ├──────────┤ GND      │
│   GPIO21 ├──────────┤ SDA      │
│   GPIO22 ├──────────┤ SCL      │
└──────────┘          └──────────┘

ESP32                  TTP223 Touch
┌──────────┐          ┌──────────┐
│      3V3 ├──────────┤ VCC      │
│      GND ├──────────┤ GND      │
│    GPIO4 ├──────────┤ SIG      │
└──────────┘          └──────────┘

ESP32                  Buzzer Module
┌──────────┐          ┌──────────┐
│       5V ├──────────┤ VCC (mid)│
│      GND ├──────────┤ GND (-)  │
│   GPIO25 ├──────────┤ SIG (S)  │
└──────────┘          └──────────┘
```

---

## ⚡ Quick Start

### 1. Prerequisites

- [Arduino IDE](https://www.arduino.cc/en/software) 2.x or [PlatformIO](https://platformio.org/)
- ESP32 board support package
- [Gadgetbridge](https://f-droid.org/packages/nodomain.freeyourgadget.gadgetbridge/) on your Android phone

### 2. Libraries

Install via Arduino Library Manager:
```
Adafruit GFX Library
Adafruit SH110X
```

### 3. Flash

```bash
# Clone the repo
git clone https://github.com/Dcodder33/desk_bot_v7.git

# Open in Arduino IDE
# Select Board: ESP32 Dev Module
# Select Port: /dev/ttyUSB0 (or your port)
# Upload!
```

### 4. Pair with Gadgetbridge

1. Install Gadgetbridge from F-Droid
2. Scan for **"DeskBot"** in Bluetooth devices
3. Pair as a **BangleJS** device type
4. Enable notifications, music, and weather forwarding

### 5. Touch Controls

| Action | Effect |
|--------|--------|
| **Single Tap** | Interact / acknowledge |
| **Double Tap** | Cycle through moods |
| **Long Press** | Pet / console |
| **Rapid Taps** | Beat the bot (escalating) |
| **Knock Patterns** | Switch to Clock / Weather |

---

## 🎨 Screen Transitions

Mode changes use a smooth **circle-iris wipe** effect (200ms per half) — the screen closes to a point, switches content, then opens back up. Combined with a brief screen flash for polish.

---

## 🏗️ Architecture

```
desk_bot_v7.ino (6,619 lines)
├── Buzzer System ─────── Non-blocking tone sequencer (LEDC PWM)
│   ├── 25+ sound patterns (warm musical notes, 1000-2600Hz)
│   └── Rocky vocabulary (10 melodic "words")
│
├── Input System ─────── TTP223 touch with auto-polarity detection
│   ├── Single/Double/Long press
│   ├── Rapid-tap beat detection
│   └── Knock pattern recognition
│
├── Emotional Engine ─── Reactive personality simulation
│   ├── 10 base moods + 10 overlay expressions
│   ├── Affection score (-100 to +100)
│   ├── Boredom escalation (4 stages)
│   ├── Attention seeking (4 phases)
│   ├── Forgiveness arc (4 phases)
│   ├── Startle chain (3 phases)
│   ├── Wake-up drama (4 phases)
│   └── Dramatic death (5 phases)
│
├── Dance Engine ─────── Genre-aware music visualization
│   ├── 4 dance styles (Groove/Disco/Wave/Hype)
│   ├── Song personality from track hash
│   ├── 9 genre detectors (80+ keywords)
│   ├── Floating music note particles
│   └── Disco ball + waveform bars
│
├── BLE Stack ────────── Gadgetbridge UART integration
│   ├── GB() JSON packet parser
│   ├── Notification/Call/Music/Weather handlers
│   ├── Zombie connection watchdog
│   └── Auto-restart with backoff
│
├── Display Faces ────── Animated OLED scenes
│   ├── Expressive face with 20+ emotional states
│   ├── Full-screen clock (blinking colon, date, temp)
│   ├── Weather scenes (7 types with animations)
│   ├── Call screen (ring ripples, caller info)
│   ├── Music popup (scrolling track, visualizer bars)
│   └── Notification overlay (app + message)
│
└── Health Systems ───── User wellness reminders
    ├── Hydration (every 45 min, 3 urgency levels)
    └── Posture check (every 30 min)
```

---

## 🤹 Personality Quirks

Every 15–45 seconds, the bot does a random micro-animation:

- 😮‍💨 **Sigh** — gentle exhale
- 👀 **Side-eye** — suspicious glance
- 🫨 **Wiggle** — playful body wobble
- 😤 **Nose scrunch** — cute twitch
- 🤨 **Suspicious squint** — narrowed eyes
- 🦘 **Mini bounce** — happy little hop
- 🤧 **Sneeze** — build → burst → surprised face

---

## 📐 Technical Details

| Spec | Value |
|------|-------|
| **Codebase** | 6,619 lines, single-file C++ |
| **Frame Rate** | ~30 FPS (non-blocking, millis-based) |
| **I2C Clock** | 400 kHz (fast mode) |
| **BLE Protocol** | UART Service (Nordic-compatible) |
| **Buzzer PWM** | 10-bit LEDC, 40% duty cycle |
| **Animation** | Float-based smooth interpolation |
| **Timezone** | IST (UTC+5:30), configurable |
| **Weather API** | Via Gadgetbridge (phone → BLE) |

---

## 📝 Version History

| Version | Highlights |
|---------|-----------|
| **v7** | Incoming call screen, 4 dance styles, song personality engine, disco ball |
| **v6** | Weather icon redesign, music popup, BLE stability |
| **v5** | Warm musical sound design, Rocky vocabulary |
| **v4** | Emotional memory, boredom system, dramatic death |
| **v3** | Overlay expressions, beat detection, forgiveness arc |
| **v2** | Gadgetbridge BLE, touch sensor, base moods |
| **v1** | Basic OLED face with blinking |

---

## 📜 License

MIT License — do whatever you want with it.

---

<div align="center">

**Built by [Dhruv Gorai](https://github.com/Dcodder33)**

*"It's not a product. It's a pet."* 🤖💕

---

*If you build one, it will judge you for not petting it enough.*

</div>
