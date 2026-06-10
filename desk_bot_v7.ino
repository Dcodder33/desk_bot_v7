/*
  ESP32 OLED Desk Bot 🤖✨
  ------------------------------------------------
  v7 — Incoming Call + Enhanced Dance Animations

  Base Moods (auto-cycle + knock patterns):
  0=Alive  1=Love  2=Angry  3=Sad
  4=Dizzy  5=Excited  6=Scared  7=Smug
  8=Clock  9=Weather

  Overlay expressions (triggered by touch):
  - Nervous shaking + sweat
  - Sleepy droopy eyes
  - Shocked wide eyes + jaw drop
  - Disgusted squint + curled lip
  - Embarrassed look-away + blush
  - Thinking raised brow + side glance
  - Hyper darting pupils
  - Pain X-eyes + zigzag mouth
  - Melting drooping face
  - Crying laughing tears+laugh

  NEW in v7:
  - Incoming Call screen — caller name, ring ripples, 30s timeout
  - Call reactions — scared ring → happy accept / sad missed
  - Alarm packet reaction (scared + shocked)
  - 4 Dance Styles: Groove / Disco / Wave / Hype (auto-cycle every 12s)
  - Disco ball with sparkles in Disco mode
  - Waveform bars at bottom during dance
  - Note explosion burst on strong beat
  - Heart eyes in Disco mode, X-eyes in Hype mode
  - Double border on peak beat
  ------------------------------------------------
  Created by: Dhruv gorai
  License: MIT
*/

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <time.h>

// ================= USER CONFIG =================
// WiFi credentials not needed — using Gadgetbridge
const char *PLACE_NAME = "Bhubaneshwar"; // fallback if no location from phone
#define LATITUDE 20.27241
#define LONGITUDE 85.83385
char weatherCity[32] = ""; // dynamic — filled from Gadgetbridge weather "loc"

// ================= PINS & SCREEN =================
#define TOUCH_PIN 4   // TTP223 touch sensor SIG pin (digital HIGH = touched)
#define BUZZER_PIN 25 // 3-pin passive buzzer SIG pin
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// SSD1306 → SH1106 compatibility aliases
#define WHITE SH110X_WHITE
#define BLACK SH110X_BLACK

// ================= SOUND MUTE TOGGLE =================
// Triple-tap on clock/weather screen toggles sound on/off
bool soundEnabled = true;
bool showMuteOverlay = false;     // brief "SOUND ON/OFF" confirmation
unsigned long muteOverlayEnd = 0;

// ================= BUZZER SYSTEM =================
// Non-blocking tone sequencer using ESP32 LEDC PWM
#define BUZZER_LEDC_CH 0
#define BUZZER_LEDC_RES 10 // 10-bit for better tone quality

// Buzzer volume: duty cycle percentage
// LOW duty = soft bell-like chime. HIGH duty = harsh buzzy square wave.
// 18% for 3-pin passive buzzer module (S + VCC + GND).
// The module has a built-in driver. Adjust 15-25 to taste.
#define BUZZER_VOLUME 40
#define BUZZER_DUTY ((1 << BUZZER_LEDC_RES) * BUZZER_VOLUME / 100)

// Tone pattern entry: {frequency_hz, duration_ms, pause_after_ms}
// freq=0 means silence/pause
struct BuzzerNote {
  uint16_t freq;
  uint16_t durMs;
  uint16_t pauseMs;
};

// Helper: start a tone at given freq
// 3-pin buzzer: S→GPIO 25, VCC(middle)→5V, (-)→GND
void buzzerToneOn(uint16_t freq) {
  if (freq > 0) {
    ledcChangeFrequency(BUZZER_PIN, freq, BUZZER_LEDC_RES);
    ledcWrite(BUZZER_PIN, BUZZER_DUTY);
  } else {
    ledcWrite(BUZZER_PIN, 0);
  }
}

void buzzerToneOff() { ledcWrite(BUZZER_PIN, 0); }

// ===== SOUND DESIGN v5 — WARM MUSICAL NOTES =====
// For 3-pin passive buzzer module (built-in amplifier)
// Uses warm mid-range (1000-2600Hz) = actual musical notes
// Pentatonic scale: C5=523 D5=587 E5=659 G5=784 A5=880
//   C6=1047 D6=1175 E6=1319 G6=1568 A6=1760 C7=2093
// Longer notes (25-60ms) + musical pauses = real melodies

// --- SYSTEM ---

// Click — gentle tap: single soft ping
const BuzzerNote SND_CLICK[] = {{1760, 12, 0}, {0, 0, 0}};

// Boot — music box awakening: C E G C' E' G' (ascending arpeggio)
const BuzzerNote SND_BOOT[] = {{1047, 40, 80}, {1319, 35, 70}, {1568, 35, 70},
                               {2093, 30, 60}, {1760, 35, 80}, {2093, 45, 0},
                               {0, 0, 0}};

// Notification — single sweet "ding!" ping
const BuzzerNote SND_NOTIF[] = {{2093, 30, 0}, {0, 0, 0}};

// Reminder — soft triple chime: ping ping ping
const BuzzerNote SND_REMIND[] = {
    {1568, 25, 150}, {1568, 25, 150}, {2093, 35, 0}, {0, 0, 0}};

// Mode transition — tiny sparkle: ti-ti
const BuzzerNote SND_TRANSITION[] = {{1568, 15, 50}, {2093, 18, 0}, {0, 0, 0}};

// --- PHONE ---

// Ring — melodic music-box phone chime (loops): C5 E5 G5 C6 ~pause~ G5 E5 C6
const BuzzerNote SND_RING[] = {{1047, 50, 80},  {1319, 45, 70},  {1568, 45, 70},
                               {2093, 55, 120}, {0, 180, 0},     {1568, 45, 80},
                               {1319, 45, 70},  {2093, 60, 350}, {0, 0, 0}};

// Call OK — happy ascending: do-mi-sol-do!
const BuzzerNote SND_CALL_OK[] = {
    {1047, 30, 60}, {1319, 30, 60}, {1568, 30, 60}, {2093, 45, 0}, {0, 0, 0}};

// Call end — gentle descending: sol-mi-do
const BuzzerNote SND_CALL_END[] = {
    {1568, 35, 100}, {1319, 35, 100}, {1047, 45, 0}, {0, 0, 0}};

// --- HAPPY ---

// Celebrate — music box cascade: C E G A C' (sparkling rise)
const BuzzerNote SND_CELEBRATE[] = {{1047, 25, 50}, {1319, 25, 50},
                                    {1568, 25, 50}, {1760, 25, 50},
                                    {2093, 35, 0},  {0, 0, 0}};

// Laugh — bouncy: high-low-high-low-high! (hahaha)
const BuzzerNote SND_LAUGH[] = {{1568, 20, 35}, {1319, 20, 35}, {1568, 20, 35},
                                {1319, 20, 35}, {1760, 25, 0},  {0, 0, 0}};

// Consoled — warm lullaby arc: C D E G E D C (music box)
const BuzzerNote SND_CONSOLED[] = {
    {1047, 45, 100}, {1175, 40, 100}, {1319, 40, 100}, {1568, 45, 100},
    {1319, 40, 100}, {1047, 50, 0},   {0, 0, 0}};

// Forgiven — sigh then hope: low G...then C D E G C'
const BuzzerNote SND_FORGIVEN[] = {
    {784, 60, 150}, {1047, 30, 70}, {1175, 30, 70}, {1319, 30, 70},
    {1568, 30, 70}, {2093, 45, 0},  {0, 0, 0}};

// --- SAD ---

// Cry — slow falling: E D C G... (like a music box winding down)
const BuzzerNote SND_CRY[] = {
    {1319, 50, 150}, {1175, 50, 150}, {1047, 55, 150}, {784, 65, 0}, {0, 0, 0}};

// Sob — broken stuttered: E..C..E..C..G (catch in breath)
const BuzzerNote SND_SOB[] = {{1319, 25, 80},  {1047, 25, 100}, {1319, 20, 80},
                              {1047, 20, 100}, {784, 30, 0},    {0, 0, 0}};

// --- SLEEP ---

// Yawn — smooth descending slide: rise...then long "aaahh" down
const BuzzerNote SND_YAWN[] = {{784, 35, 30}, // inhale hm
                               {1047, 45, 0}, // rising ah
                               {1175, 50, 0}, // peak — mouth open
                               {1109, 55, 0}, // start descending
                               {1047, 60, 0}, // ...aaah
                               {988, 65, 0},  // ...hhh
                               {880, 75, 0},  // ...mmm
                               {784, 90, 0},  // trailing off
                               {698, 110, 0}, // zzz...
                               {0, 0, 0}};

// Wake — bright morning chime: C E G C' (cheerful!)
const BuzzerNote SND_WAKE[] = {
    {1047, 25, 60}, {1319, 25, 60}, {1568, 25, 60}, {2093, 35, 0}, {0, 0, 0}};

// Purr — soft warm hum (very low, gentle vibration feel)
const BuzzerNote SND_PURR[] = {
    {262, 90, 50}, {294, 90, 50}, {262, 90, 50}, {0, 0, 0}};

// --- NEGATIVE ---

// Angry — low growl: short tense notes
const BuzzerNote SND_ANGRY[] = {
    {523, 30, 60}, {494, 30, 60}, {523, 35, 0}, {0, 0, 0}};

// Scared — fast rising: quick ascending steps (panic)
const BuzzerNote SND_SCARED[] = {{1047, 15, 25}, {1175, 15, 25}, {1319, 15, 25},
                                 {1397, 15, 25}, {1568, 20, 0},  {0, 0, 0}};

// Bored — lazy sigh: two slow descending notes
const BuzzerNote SND_BORED[] = {{1175, 50, 180}, {880, 60, 0}, {0, 0, 0}};

// --- ACTION ---

// Hit — short thump
const BuzzerNote SND_HIT[] = {{523, 15, 0}, {0, 0, 0}};

// KO — falling spiral: descending cartoon knockout
const BuzzerNote SND_KO[] = {{1568, 30, 60}, {1319, 30, 60}, {1047, 35, 60},
                             {784, 40, 60},  {523, 50, 0},   {0, 0, 0}};

// Death — slow fading: gentle descent into silence
const BuzzerNote SND_DEATH[] = {
    {1319, 40, 120}, {1175, 40, 120}, {1047, 45, 150}, {880, 50, 150},
    {784, 55, 180},  {659, 65, 0},    {0, 0, 0}};

// Sneeze — build then burst: pip pip [silence] ACHOO!
const BuzzerNote SND_SNEEZE[] = {
    {1175, 20, 50}, {1319, 20, 50}, {0, 130, 0}, {2637, 25, 0}, {0, 0, 0}};

// --- NEW: EXPRESSIVE SOUNDS ---

// Sigh — gentle exhale: high note descends slowly (hmmmm)
const BuzzerNote SND_SIGH[] = {
    {1319, 45, 0}, {1175, 55, 0}, {1047, 70, 0}, {0, 0, 0}};

// Hiccup — bouncy two-note pop: bip-BOP!
const BuzzerNote SND_HICCUP[] = {{1047, 15, 30}, {1760, 20, 0}, {0, 0, 0}};

// Wiggle — playful wobble: fast alternating (wubwubwub)
const BuzzerNote SND_WIGGLE[] = {
    {1319, 15, 20}, {1568, 15, 20}, {1319, 15, 20}, {1568, 18, 0}, {0, 0, 0}};

// Chirp — idle happy peep: random cheerful sound
const BuzzerNote SND_CHIRP[] = {{1568, 20, 80}, {2093, 15, 0}, {0, 0, 0}};

// Confused — wobbly uncertain notes: huh? what?
const BuzzerNote SND_CONFUSED[] = {
    {1047, 30, 60}, {1175, 25, 60}, {1047, 25, 60}, {880, 35, 0}, {0, 0, 0}};

// Jealous — possessive low grumble: grrr-hmph
const BuzzerNote SND_JEALOUS[] = {
    {523, 35, 40}, {587, 25, 40}, {523, 25, 40}, {494, 40, 0}, {0, 0, 0}};

// Sleep — soft descending lullaby: drifting off to sleep
const BuzzerNote SND_SLEEP[] = {
    {1319, 50, 100}, {1175, 55, 120}, {1047, 60, 140}, {880, 70, 0}, {0, 0, 0}};

// Attention — whiny plea melody: notice meeee!
const BuzzerNote SND_ATTENTION[] = {
    {1568, 30, 60}, {1760, 25, 50}, {1568, 30, 60}, {2093, 35, 0}, {0, 0, 0}};

// Giggle — fast ascending trill: ti-ti-ti-tee! (cuter than laugh)
const BuzzerNote SND_GIGGLE[] = {
    {1568, 12, 25}, {1760, 12, 25}, {2093, 12, 25}, {2349, 15, 0}, {0, 0, 0}};

// Hum — idle humming: two gentle notes back and forth
const BuzzerNote SND_HUM[] = {
    {784, 60, 40}, {880, 60, 40}, {784, 60, 0}, {0, 0, 0}};

// ===== ROCKY VOCABULARY (Project Hail Mary) =====
// Each "word" is a unique musical phrase — no text needed!
// The bot communicates through pure melodic chords, like Rocky.

// "Amaze!" — triple ascending burst (amaze amaze amaze!)
const BuzzerNote SND_ROCKY_AMAZE[] = {
    {1047, 25, 30}, {1319, 25, 30}, {1568, 30, 80}, {1047, 25, 30},
    {1319, 25, 30}, {1568, 30, 80}, {1047, 25, 30}, {1319, 25, 30},
    {2093, 40, 0},  {0, 0, 0}};

// "Good good good!" — happy repeating same note with lift
const BuzzerNote SND_ROCKY_GOOD[] = {
    {1568, 30, 50}, {1568, 30, 50}, {1568, 30, 50}, {2093, 35, 0}, {0, 0, 0}};

// "Happy!" — fast bouncy ascending
const BuzzerNote SND_ROCKY_HAPPY[] = {{1319, 20, 25}, {1568, 20, 25},
                                      {1760, 20, 25}, {2093, 20, 25},
                                      {2349, 25, 0},  {0, 0, 0}};

// "Question?" — phrase that rises at end (like asking)
const BuzzerNote SND_ROCKY_QUESTION[] = {
    {1047, 35, 50}, {1175, 30, 50}, {1568, 25, 40}, {2093, 35, 0}, {0, 0, 0}};

// "Worry!" — uncertain wobbling between two notes
const BuzzerNote SND_ROCKY_WORRY[] = {
    {880, 35, 30}, {784, 35, 30}, {880, 30, 30}, {784, 40, 0}, {0, 0, 0}};

// "Understand!" — confident firm descending then up
const BuzzerNote SND_ROCKY_UNDERSTAND[] = {
    {1568, 40, 40}, {1319, 35, 40}, {1568, 45, 0}, {0, 0, 0}};

// "No no no!" — harsh low repeated with drop
const BuzzerNote SND_ROCKY_NO[] = {
    {523, 30, 40}, {494, 30, 40}, {440, 35, 40}, {392, 40, 0}, {0, 0, 0}};

// "Excited!" — rapid high ascending chirps
const BuzzerNote SND_ROCKY_EXCITED[] = {
    {2093, 15, 20}, {2349, 15, 20}, {2637, 15, 20}, {2093, 15, 20},
    {2349, 15, 20}, {2637, 20, 0},  {0, 0, 0}};

// "Sleepy..." — very slow fading hum
const BuzzerNote SND_ROCKY_SLEEPY[] = {
    {784, 50, 0}, {698, 60, 0}, {659, 70, 0}, {587, 90, 0}, {0, 0, 0}};

// "Thank!" — warm grateful two-note
const BuzzerNote SND_ROCKY_THANK[] = {{1319, 40, 60}, {1568, 50, 0}, {0, 0, 0}};

// Buzzer state (non-blocking)
const BuzzerNote *buzzerPattern = nullptr; // current pattern playing
int buzzerNoteIdx = 0;                     // index in pattern
bool buzzerPlaying = false;
unsigned long buzzerNoteEnd = 0; // when current note/pause ends
bool buzzerInPause = false;      // currently in pause between notes
bool buzzerLooping = false;      // repeat pattern?
unsigned long buzzerLoopEnd = 0; // auto-stop looping at this time

// Start playing a pattern (non-blocking)
void buzzerPlay(const BuzzerNote *pattern, bool loop = false,
                unsigned long loopDurationMs = 0) {
  // Sound mute gate — skip all sounds when muted
  if (!soundEnabled) return;
  buzzerPattern = pattern;
  buzzerNoteIdx = 0;
  buzzerPlaying = true;
  buzzerInPause = false;
  buzzerLooping = loop;
  buzzerLoopEnd = (loopDurationMs > 0) ? millis() + loopDurationMs : 0;
  // Start first note immediately
  buzzerToneOn(pattern[0].freq);
  buzzerNoteEnd = millis() + pattern[0].durMs;
}

void buzzerStop() {
  buzzerToneOff();
  buzzerPlaying = false;
  buzzerPattern = nullptr;
  buzzerLooping = false;
}

// Call from loop() — advances the tone sequencer
void buzzerUpdate() {
  if (!buzzerPlaying || !buzzerPattern)
    return;
  unsigned long now = millis();

  // Check loop timeout
  if (buzzerLooping && buzzerLoopEnd > 0 && now >= buzzerLoopEnd) {
    buzzerStop();
    return;
  }

  if (now < buzzerNoteEnd)
    return; // still playing current note/pause

  if (!buzzerInPause) {
    // Note just finished — enter pause (if any)
    uint16_t pauseMs = buzzerPattern[buzzerNoteIdx].pauseMs;
    if (pauseMs > 0) {
      buzzerToneOff(); // silence during pause
      buzzerNoteEnd = now + pauseMs;
      buzzerInPause = true;
      return;
    }
    // No pause — fall through to next note
  }

  // Move to next note
  buzzerInPause = false;
  buzzerNoteIdx++;

  // Check for end-of-pattern marker (freq=0, dur=0, pause=0)
  if (buzzerPattern[buzzerNoteIdx].freq == 0 &&
      buzzerPattern[buzzerNoteIdx].durMs == 0) {
    if (buzzerLooping) {
      buzzerNoteIdx = 0; // restart pattern
    } else {
      buzzerStop();
      return;
    }
  }

  // Play next note
  buzzerToneOn(buzzerPattern[buzzerNoteIdx].freq);
  buzzerNoteEnd = now + buzzerPattern[buzzerNoteIdx].durMs;
}

// ================= INPUT =================
const int DOUBLE_TAP_DELAY = 350;
const int LONG_PRESS_TIME = 600;
unsigned long touchStartTime = 0;
unsigned long lastTapTime = 0;
bool isTouching = false;
bool isLongPressing = false;
int tapCount = 0;
unsigned long knockTimes[6];
int knockCount = 0;
unsigned long lastKnockTime = 0;
const int KNOCK_WINDOW = 2000;

// ---- TTP223 TOUCH SENSOR ----
// Auto-detects polarity: some modules go HIGH on touch, some go LOW.
// We read the resting state at boot and invert if needed.
bool touchCalibrated = true;
bool touchInverted = false; // set true if module is active-LOW
unsigned long lastTouchDebug = 0;

void calibrateTouch() {
  pinMode(TOUCH_PIN, INPUT);
  delay(500); // TTP223 self-calibration time

  // Read resting state (nobody touching it during boot)
  int resting = digitalRead(TOUCH_PIN);
  if (resting == HIGH) {
    // Pin is HIGH at rest → module outputs LOW on touch (active-LOW)
    touchInverted = true;
    Serial.println("TTP223: resting=HIGH → active-LOW mode (inverted)");
  } else {
    touchInverted = false;
    Serial.println("TTP223: resting=LOW → active-HIGH mode (normal)");
  }
  Serial.println("TTP223 ready on GPIO " + String(TOUCH_PIN));
}

bool isTouchActive() {
  int val = digitalRead(TOUCH_PIN);

  // Debug print every 500ms
  unsigned long now = millis();
  if (now - lastTouchDebug > 500) {
    lastTouchDebug = now;
    bool touched = touchInverted ? (val == LOW) : (val == HIGH);
    Serial.println("TOUCH pin:" + String(val) + " inv:" +
                   String(touchInverted) + (touched ? " <<TOUCH>>" : ""));
  }

  return touchInverted ? (val == LOW) : (val == HIGH);
}

// ================= BASE MOODS =================
int currentMode = 0;
int previousMode = 0;
unsigned long lastInteractionTime = 0;
bool isSleeping = false;
const unsigned long SLEEP_TIMEOUT = 60000;
unsigned long nextMoodChangeAt = 0;

// ================= BASE MOOD FLAGS =================
bool isPuppySquint = false;
unsigned long squintEndTime = 0;
bool isBeingPetted = false;
bool isRejected = false;
unsigned long rejectEndTime = 0;
bool isFurious = false;
bool isComforted = false;
bool isBeingLoved = false;
bool isWinking = false;
unsigned long winkEndTime = 0;
bool isSurprised = false;
unsigned long surpriseEndTime = 0;
bool isLaughing = false;
unsigned long laughEndTime = 0;
bool isSuspicious = false;
unsigned long suspiciousEndTime = 0;
bool isCryingHard = false;
bool isSmugWink = false;
unsigned long smugWinkEndTime = 0;

// ================= OVERLAY EXPRESSION FLAGS =================
// Each has a timer — they expire and face returns to base mood

// NERVOUS — shaking body + sweat drops
bool ovNervous = false;
unsigned long ovNervousEnd = 0;
float nervShake = 0;

// SLEEPY — drooping eyelids overlay
bool ovSleepy = false;
unsigned long ovSleepyEnd = 0;
float sleepDroop = 0; // 0-1 how much lids droop

// SHOCKED — maxed wide eyes + dropped jaw
bool ovShocked = false;
unsigned long ovShockedEnd = 0;

// DISGUSTED — one squinted eye + curled lip
bool ovDisgusted = false;
unsigned long ovDisgustedEnd = 0;

// EMBARRASSED — eyes looking away + blush circles
bool ovEmbarrassed = false;
unsigned long ovEmbarrassedEnd = 0;
float blushPulse = 0;

// THINKING — one brow up + eyes look up-left
bool ovThinking = false;
unsigned long ovThinkingEnd = 0;

// HYPER — pupils dart randomly very fast
bool ovHyper = false;
unsigned long ovHyperEnd = 0;
int hyperPupilX = 0;
int hyperPupilY = 0;
unsigned long nextHyperDart = 0;
int hyperTargetX = 0;
int hyperTargetY = 0;

// PAIN — X eyes + zigzag mouth
bool ovPain = false;
unsigned long ovPainEnd = 0;

// MELTING — face droops downward
bool ovMelting = false;
unsigned long ovMeltingEnd = 0;
float meltDroop = 0; // increases over time 0-14

// CRY LAUGH — tears streaming + laughing mouth
bool ovCryLaugh = false;
unsigned long ovCryLaughEnd = 0;
float cryLaughShake = 0;

// GETTING BEATEN — progressive stages from rapid taps
// Stage 0=none 1=ouch 2=pain 3=crying 4=KO
int beatStage = 0;
int beatHitCount = 0;              // rapid hits counter
unsigned long beatWindowStart = 0; // window to count rapid hits
unsigned long lastBeatHit = 0;
unsigned long beatRecoverAt = 0; // when to start recovering
float beatShake = 0;             // screen shake intensity
bool beatStarsActive = false;    // impact stars flying
unsigned long beatStarEnd = 0;
float beatStarAngle = 0; // rotating stars around head
bool isKO = false;       // KO = completely knocked out
unsigned long koEnd = 0;
float koSpinAngle = 0;              // spinning stars when KO
const int BEAT_RAPID_WINDOW = 1200; // ms window to count hits as beating
const int BEAT_HITS_STAGE1 = 5;  // hits to reach stage 1 (ouch) — raised from 3
const int BEAT_HITS_STAGE2 = 8;  // hits to reach stage 2 (pain)
const int BEAT_HITS_STAGE3 = 12; // hits to reach stage 3 (crying)
const int BEAT_HITS_KO = 16;     // hits to reach KO

// ---- CONSOLING SYSTEM ----
// When sad/crying, bot needs to be comforted (long press) to recover.
// Mood system won't auto-switch while needsConsoling is true.
bool needsConsoling = false;
bool isBeingConsoled = false;
unsigned long consoleStartAt = 0;
const unsigned long CONSOLE_HOLD_MS = 3000; // hold 3s to fully console

// ================= BOREDOM SYSTEM =================
// 15 min no activity → bored → angry → furious
// Touch while bored → irritated. Hold → angrier then flips to happy.
const unsigned long BORED_TIMEOUT = 2700000UL; // 45 min → boredom kicks in
const unsigned long BORED_ANGRY_AT =
    600000UL; // +10 min after bored → escalate to angry
const unsigned long BORED_FURIOUS_AT = 1200000UL; // +20 min → furious
bool isBored = false;
int boredStage = 0; // 0=not 1=bored/dismissive 2=angry 3=furious
unsigned long boredStartTime = 0;
unsigned long lastBoredEyeRoll = 0; // periodic eye-roll while bored

// Long-hold flip: hold 3s → irritated; hold 6s → melts into happy
const unsigned long HOLD_ANGRY_MS = 3000UL;
const unsigned long HOLD_HAPPY_MS = 6000UL;
bool holdHappyTriggered = false;
bool holdAngryTriggered = false;

// ================= EMOTIONAL MEMORY =================
// Affection: -100 (hates you) → +100 (adores you)
// Builds with gentle interactions, drops with beating/ignoring
int affectionScore = 50; // starts neutral-positive
unsigned long lastAffectionDecay = 0;
const unsigned long AFFECTION_DECAY_MS = 300000UL; // -1 every 5 min idle

// Mood cooldown — bot "lingers" in moods, won't instant-switch
unsigned long moodStartedAt = 0;
const unsigned long MIN_MOOD_DURATION = 30000UL; // 30s minimum per mood

// Recent interaction memory (sliding 5-min window)
int recentPetCount = 0;
int recentTapCount = 0;
int recentBeatCount = 0;
unsigned long lastMemoryReset = 0;
const unsigned long MEMORY_WINDOW = 300000UL;

// ================= PERSONALITY QUIRKS =================
// Random cute micro-animations every 15–45s
unsigned long nextQuirkAt = 0;
bool isDoingQuirk = false;
int quirkType = 0; // 0=sigh 1=side-eye 2=wiggle 3=nose-scrunch
                   // 4=suspicious-squint 5=mini-bounce 6=sneeze
unsigned long quirkEndAt = 0;
float quirkParam = 0; // animation progress float

// ================= ATTENTION SEEKING =================
// Graduated "hey look at me!" stages BEFORE boredom kicks in
// Stage 0=none 1=side-eye+sigh(3min) 2=bouncy-expectant(5min)
//        3=dramatic-slump(8min) 4=fake-sleep-peeking(12min)
int attentionStage = 0;
unsigned long lastAttentionEscalate = 0;
bool attentionSideLook = false;
float attentionBounce = 0;
bool fakeSnoring = false;
float fakeSleepEyeOpen = 0; // one eye peeks open 0-1

// ================= STARTLE CHAIN =================
// Multi-phase: shocked → looking around → embarrassed recovery
bool isStartled = false;
int startlePhase = 0; // 0=none 1=shocked 2=look-around 3=embarrassed
unsigned long startlePhaseEnd = 0;

// ================= FORGIVENESS ARC =================
// After beat KO: huffy → side-eye → tentative → forgiven
bool isForgiving = false;
unsigned long forgiveStartAt = 0;
int forgivePhase = 0; // 0=huffy 1=side-eye 2=tentative 3=forgiven

// ================= PURRING / BLISS =================
// Sustained petting triggers purring with hearts + body sway
bool isPurring = false;
unsigned long purringStartAt = 0;
float purrVibrate = 0;
unsigned long petHoldStart = 0; // tracks continuous hold duration
bool purrStopSurprise = false;  // "why did you stop?!" face
unsigned long purrStopEnd = 0;

// ================= NOTIFICATION JEALOUSY =================
bool isJealous = false;
unsigned long jealousyEnd = 0;
int jealousyPhase = 0; // 0=narrow 1=curious 2=sulky/excited

// ================= WAKE-UP DRAMA =================
// 4-phase: shocked → grumpy → yawn → alive
int wakeUpPhase = 0; // 0=none 1=startled 2=grumpy 3=yawn 4=alive
unsigned long wakePhaseEnd = 0;

// ================= HYDRATION REMINDER =================
// Every 45min, bot gets thirsty and reminds you to drink water
const unsigned long HYDRATION_INTERVAL = 2700000UL; // 45 min
unsigned long lastHydrationTime = 0;
bool isThirsty = false;
unsigned long thirstyStart = 0;
int thirstyUrgency = 0; // 0=mild, 1=urgent, 2=desperate
bool hydrationCelebration = false;
unsigned long celebrationEnd = 0;

// ================= POSTURE CHECK =================
// Every 30min, bot reminds you to sit up straight
const unsigned long POSTURE_INTERVAL = 1800000UL; // 30 min
unsigned long lastPostureTime = 0;
bool isPostureReminder = false;
unsigned long postureStart = 0;
bool postureAcked = false;
unsigned long postureAckEnd = 0;

// ================= DRAMATIC DEATH =================
// At -100 affection, bot "dies" dramatically and reboots with amnesia
bool isDying = false;
int deathPhase = 0; // 0=none 1=shock 2=spiral 3=flicker 4=flatline 5=reboot
unsigned long deathPhaseEnd = 0;
bool hasAmnesia = false;
unsigned long amnesiaEnd = 0;

// ================= SMOOTH TRANSITION SYSTEM =================
// Instead of instant mode changes, queue with a short bridge overlay.
int pendingMode = -1;           // mode to switch to after bridge
unsigned long modeChangeAt = 0; // millis() when to apply pendingMode
float transitionFlash = 0;      // 0-1 screen flash that fades out smoothly

// ---- SCREEN TRANSITION EFFECT ----
// Circle-iris wipe when switching between face/clock/weather
bool screenTransActive = false;
float screenTransProgress =
    0; // 0.0→1.0 (closing), reset then 0.0→1.0 (opening)
bool screenTransClosing = true;
int screenTransTargetMode = -1;
unsigned long screenTransStart = 0;
const unsigned long SCREEN_TRANS_MS = 200; // 200ms per half-transition

void startScreenTransition(int targetMode) {
  screenTransActive = true;
  screenTransClosing = true;
  screenTransProgress = 0;
  screenTransTargetMode = targetMode;
  screenTransStart = millis();
}

void smoothModeChange(int newMode, unsigned long delayMs) {
  pendingMode = newMode;
  modeChangeAt = millis() + delayMs;
  transitionFlash = 1.0f; // start full-white flash, fades in loop
  if (!buzzerPlaying)
    buzzerPlay(SND_TRANSITION); // subtle blip if no other sound
}
float outdoorTemp = NAN;
int weatherCode = 0; // WMO weather code from open-meteo
bool weatherReady = false;
bool timeReady = false;
unsigned long lastWeatherUpdate = 0;
unsigned long lastWeatherAttempt = 0;

int lookDirection = 0;
unsigned long nextLookTime = 0;
bool isYawning = false;
unsigned long yawnEndTime = 0;
bool hasMidYawned = false;
bool hasFinalYawned = false;
bool isDriftingOff = false;
unsigned long randomMidYawnTime =
    30000; // initialized to prevent immediate yawn

float tearY = 0;
float spiralAngle = 0.0;
float bounceY = 0.0;
float laughShake = 0.0;
float scaredShake = 0.0;
unsigned long lastBlinkTime = 0;
bool isBlinking = false;
int blinkInterval = 2000;
float heartScale = 1.0;
float exciteScale = 1.0;

const int BASE_EYE_W = 36;
const int EYE_H = 52;
const int EYE_Y = 2;
const int EYE_X_L = 8;
const int EYE_X_R = 84;
const int EYE_RADIUS = 10;
const int MOUTH_Y = 44;

float currentEyeW_L = BASE_EYE_W, currentEyeW_R = BASE_EYE_W, currentMouthX = 0;
float currentYawnFactor = 0.0, currentEyeOpenFactor = 1.0;
float targetEyeW_L = BASE_EYE_W, targetEyeW_R = BASE_EYE_W, targetMouthX = 0;
float targetYawnFactor = 0.0, targetEyeOpenFactor = 1.0;
const float PAN_SPEED = 12.0;
const float YAWN_SPEED = 0.08;
const float SLEEP_SPEED = 0.05;

unsigned long cloudAnimTimer = 0;
float mainCloudX = -50, smallCloud1X = 20, smallCloud2X = 90;

// ================= GADGETBRIDGE BLE =================
#define BLE_DEVICE_NAME "DeskBot"
#define UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = nullptr;
BLECharacteristic *pTxChar = nullptr;
bool bleConnected = false;
char bleBuffer[512] = "";
unsigned long lastBLEKeepalive = 0;
unsigned long lastBLECheck = 0;
unsigned long lastWeatherRequest = 0;
const unsigned long WEATHER_REQUEST_INTERVAL =
    1800000UL;                                // re-request every 30 min
const unsigned long BLE_KEEPALIVE_MS = 10000; // send keepalive every 10s
const unsigned long BLE_CHECK_MS = 5000;      // check connection every 5s

// ---- BLE STABILITY / ANTI-DISCONNECT ----
unsigned long lastBLEActivity = 0; // last time we RECEIVED data from phone
const unsigned long BLE_STALE_TIMEOUT =
    90000UL;                            // 90s no data = zombie connection
unsigned long bleDisconnectedSince = 0; // when we first noticed disconnection
const unsigned long BLE_WATCHDOG_RESTART_MS =
    300000UL;                     // 5 min disconnected = full stack restart
int bleRestartCycles = 0;          // count BLE restarts to limit memory churn
const int BLE_MAX_RESTART_CYCLES = 3; // max full restarts before giving up
int bleReconnectCount = 0;        // debug counter
bool bleJustConnected = false;    // flag: send handshake from loop()
unsigned long bleConnectTime = 0; // when bleJustConnected was set
const unsigned long BLE_HANDSHAKE_DELAY =
    200;                               // ms to wait before sending handshake
int bleHandshakeStep = 0;              // 0=not started, 1-5=sending packets
unsigned long bleHandshakeNextAt = 0;  // when to send next handshake packet
bool bleNeedsRestart = false;          // flag: full BLE stack restart needed
unsigned long bleRestartAt = 0;        // when to do the restart
esp_bd_addr_t blePeerAddr;             // connected peer's Bluetooth address
bool blePeerAddrValid = false;         // true after we capture it
unsigned long bleLastConnParamReq = 0; // debounce conn param requests
int bleConsecutiveFails = 0;           // consecutive notify failures
const int BLE_MAX_NOTIFY_FAILS = 5;    // force reconnect after N failures

// ---- PHONE BATTERY ----
// Received from Gadgetbridge — shows phone battery on clock face
int phoneBattery = -1; // -1 = unknown, 0-100 = percentage

// Notification
char notifApp[32] = "";
char notifLine1[32] = ""; // increased from 22
char notifLine2[32] = ""; // increased from 22
bool showNotif = false;
unsigned long notifEnd = 0;
const unsigned long NOTIF_DURATION = 3000; // 3 seconds fullscreen
char callSource[16] = "";                  // "Phone"/"WhatsApp"/"Telegram" etc

// Music info
char musicTrack[64] = "";
char musicArtist[64] = "";
char musicLine1[22] = "";
char musicLine2[22] = "";
bool showMusic = false;
bool musicPlaying = false;
unsigned long musicEnd = 0;
bool showMusicPopup = false; // fullscreen popup on play/pause
unsigned long musicPopupEnd = 0;
const unsigned long MUSIC_POPUP_DURATION = 3000; // 3s fullscreen

// ================= INCOMING CALL =================
bool isIncomingCall = false;
bool callActive = false; // call was answered
bool callEnded = false;
char callerName[32] = "";
char callerNumber[20] = "";
unsigned long callRingStart = 0;
unsigned long callEnd = 0;
float callRingPulse = 0;
const unsigned long CALL_RING_TIMEOUT = 30000; // auto-dismiss after 30s
// post-call happy/sad reaction
bool callWasAnswered = false;
unsigned long callReactEnd = 0;

// ================= DANCE ANIMATION =================
// All dance values — driven purely by millis() so they
// automatically sync to any tempo feel
bool isDancing = false;
float danceBounceY = 0;   // body bounce up/down
float danceHeadTilt = 0;  // head sways left/right
float danceMouthOpen = 0; // mouth opens on beat
float danceEyeSquint = 0; // eyes squint on beat
float danceArmL = 0;      // left arm angle
float danceArmR = 0;      // right arm angle

// Music note particles floating up
struct MusicNote {
  float x, y; // position
  float vy;   // velocity upward
  int symbol; // 0=♩ 1=♪ 2=♫
  bool active;
};
MusicNote notes[6];
unsigned long lastNoteSpawn = 0;

// Beat pulse — flashes on downbeat
float beatPulse = 0; // 0-1, decays
unsigned long lastBeat = 0;
const unsigned long BEAT_INTERVAL = 500; // assume ~120bpm, 500ms per beat

// Dance style — cycles through modes 0=groove 1=disco 2=wave 3=hype
int danceStyle = 0;
unsigned long danceStyleStart = 0;
const unsigned long DANCE_STYLE_DURATION = 12000; // change style every 12s

// ================= SONG PERSONALITY =================
// Derived from track/artist keywords + title hash.
// Each song gets a unique feel — set once on musicinfo, used throughout dance.
struct SongPersonality {
  unsigned long beatInterval; // ms between beats  (300–900)
  float bounceAmp;            // body bounce px     (1.0–5.0)
  float armAmp;               // arm swing degrees  (10–50)
  float headAmp;              // head tilt px       (1–9)
  float noteSpeed;            // note float speed   (0.05–0.25)
  unsigned long noteInterval; // note spawn ms      (200–1000)
  int startStyle;             // forced start style (-1=free, 0-3)
  int styleOrder[4];          // cycle order
  float mouthSensitivity;     // how wide mouth opens (0.4–1.0)
  float eyeSquintDepth;       // how much eyes squint (0.2–0.8)
  bool stayOnStyle;           // lock to startStyle, no cycling
  char genreTag[12];          // debug label
};
SongPersonality songP = {600, 2.0f,         25.0f, 3.0f, 0.08f, 700,
                         -1,  {0, 1, 2, 3}, 0.7f,  0.5f, false, "default"};

void parseSongPersonality(const char *track, const char *artist) {
  // --- Hash track+artist for per-song consistency ---
  unsigned long h = 5381;
  for (const char *p = track; *p; p++)
    h = ((h << 5) + h) ^ (unsigned char)*p;
  for (const char *p = artist; *p; p++)
    h = ((h << 5) + h) ^ (unsigned char)*p;

  // --- Build lowercase combined string for keyword matching ---
  String ta = String(track) + " " + String(artist);
  ta.toLowerCase();

  // --- Hash-derived base values (used even if no keyword match) ---
  unsigned long basebeat = 420 + (h % 380);                    // 420–800ms
  float baseBounce = 1.2f + (float)((h >> 3) % 35) * 0.1f;     // 1.2–4.7
  float baseArm = 12.0f + (float)((h >> 7) % 38);              // 12–50
  float baseHead = 1.5f + (float)((h >> 11) % 8);              // 1.5–9.5
  float baseNoteSpd = 0.05f + (float)((h >> 15) % 18) * 0.01f; // 0.05–0.23
  unsigned long baseNoteInt = 350 + ((h >> 5) % 600);          // 350–950ms
  float baseMouth = 0.45f + (float)((h >> 9) % 50) * 0.01f;    // 0.45–0.95
  float baseSquint = 0.25f + (float)((h >> 13) % 55) * 0.01f;  // 0.25–0.80

  // Style cycle order from hash — shuffle using Fisher-Yates on hash bits
  int ord[4] = {0, 1, 2, 3};
  for (int i = 3; i > 0; i--) {
    int j = (h >> (i * 2)) % (i + 1);
    int tmp = ord[i];
    ord[i] = ord[j];
    ord[j] = tmp;
  }

  // --- Apply defaults ---
  songP.beatInterval = basebeat;
  songP.bounceAmp = baseBounce;
  songP.armAmp = baseArm;
  songP.headAmp = baseHead;
  songP.noteSpeed = baseNoteSpd;
  songP.noteInterval = baseNoteInt;
  songP.startStyle = ord[0];
  memcpy(songP.styleOrder, ord, sizeof(ord));
  songP.mouthSensitivity = baseMouth;
  songP.eyeSquintDepth = baseSquint;
  songP.stayOnStyle = false;
  strncpy(songP.genreTag, "default", 11);

  // ============================================================
  // KEYWORD GENRE DETECTION  (first match wins)
  // ============================================================

  // BALLAD / SLOW / ACOUSTIC — dreamy wave, slow pulse
  if (ta.indexOf("ballad") >= 0 || ta.indexOf("acoustic") >= 0 ||
      ta.indexOf("slow") >= 0 || ta.indexOf("piano") >= 0 ||
      ta.indexOf("gentle") >= 0 || ta.indexOf("soft") >= 0 ||
      ta.indexOf("lullaby") >= 0 || ta.indexOf("sleep") >= 0 ||
      ta.indexOf("calm") >= 0 || ta.indexOf("serenade") >= 0 ||
      ta.indexOf("waltz") >= 0 || ta.indexOf("adagio") >= 0) {
    songP.beatInterval = 750 + (h % 200);
    songP.bounceAmp = 0.8f + (float)((h >> 3) % 12) * 0.1f;
    songP.armAmp = 8.0f + (float)((h >> 7) % 14);
    songP.headAmp = 1.0f + (float)((h >> 11) % 5);
    songP.noteSpeed = 0.04f + (float)((h >> 15) % 8) * 0.005f;
    songP.noteInterval = 900 + ((h >> 5) % 300);
    songP.mouthSensitivity = 0.4f + (float)((h >> 9) % 25) * 0.01f;
    songP.eyeSquintDepth = 0.5f + (float)((h >> 13) % 20) * 0.01f;
    songP.startStyle = 2; // WAVE
    songP.styleOrder[0] = 2;
    songP.styleOrder[1] = 0;
    songP.styleOrder[2] = 2;
    songP.styleOrder[3] = 0;
    songP.stayOnStyle = true; // stay on WAVE
    strncpy(songP.genreTag, "ballad", 11);
  }
  // TRAP / DRILL / BASS / RAGE — hyper max energy
  else if (ta.indexOf("trap") >= 0 || ta.indexOf("drill") >= 0 ||
           ta.indexOf("bass") >= 0 || ta.indexOf("rage") >= 0 ||
           ta.indexOf("slaughter") >= 0 || ta.indexOf("savage") >= 0 ||
           ta.indexOf("gangsta") >= 0 || ta.indexOf("goon") >= 0 ||
           ta.indexOf("playboi") >= 0 || ta.indexOf("lil ") >= 0 ||
           ta.indexOf("uzi") >= 0 || ta.indexOf("carti") >= 0 ||
           ta.indexOf("ski mask") >= 0) {
    songP.beatInterval = 280 + (h % 150);
    songP.bounceAmp = 3.5f + (float)((h >> 3) % 20) * 0.1f;
    songP.armAmp = 32.0f + (float)((h >> 7) % 18);
    songP.headAmp = 4.0f + (float)((h >> 11) % 6);
    songP.noteSpeed = 0.15f + (float)((h >> 15) % 12) * 0.01f;
    songP.noteInterval = 180 + ((h >> 5) % 150);
    songP.mouthSensitivity = 0.85f + (float)((h >> 9) % 15) * 0.01f;
    songP.eyeSquintDepth = 0.6f;
    songP.startStyle = 3; // HYPE
    songP.styleOrder[0] = 3;
    songP.styleOrder[1] = 3;
    songP.styleOrder[2] = 1;
    songP.styleOrder[3] = 3;
    songP.stayOnStyle = true;
    strncpy(songP.genreTag, "trap", 11);
  }
  // DISCO / FUNK / EDM / HOUSE / CLUB
  else if (ta.indexOf("disco") >= 0 || ta.indexOf("funk") >= 0 ||
           ta.indexOf("funky") >= 0 || ta.indexOf("edm") >= 0 ||
           ta.indexOf("house") >= 0 || ta.indexOf("techno") >= 0 ||
           ta.indexOf("club") >= 0 || ta.indexOf("party") >= 0 ||
           ta.indexOf("dance") >= 0 || ta.indexOf("dj ") >= 0 ||
           ta.indexOf("rave") >= 0 || ta.indexOf("electro") >= 0 ||
           ta.indexOf("trance") >= 0 || ta.indexOf("bpm") >= 0 ||
           ta.indexOf("avicii") >= 0 || ta.indexOf("daft punk") >= 0 ||
           ta.indexOf("calvin harris") >= 0 || ta.indexOf("deadmau5") >= 0) {
    songP.beatInterval = 380 + (h % 180);
    songP.bounceAmp = 2.8f + (float)((h >> 3) % 18) * 0.1f;
    songP.armAmp = 28.0f + (float)((h >> 7) % 22);
    songP.headAmp = 3.0f + (float)((h >> 11) % 6);
    songP.noteSpeed = 0.10f + (float)((h >> 15) % 10) * 0.01f;
    songP.noteInterval = 320 + ((h >> 5) % 200);
    songP.mouthSensitivity = 0.8f;
    songP.eyeSquintDepth = 0.55f;
    songP.startStyle = 1; // DISCO
    songP.styleOrder[0] = 1;
    songP.styleOrder[1] = 3;
    songP.styleOrder[2] = 1;
    songP.styleOrder[3] = 0;
    songP.stayOnStyle = false;
    strncpy(songP.genreTag, "disco/edm", 11);
  }
  // JAZZ / BLUES / SOUL / SWING / BOSSA
  else if (ta.indexOf("jazz") >= 0 || ta.indexOf("blues") >= 0 ||
           ta.indexOf("soul") >= 0 || ta.indexOf("swing") >= 0 ||
           ta.indexOf("bossa") >= 0 || ta.indexOf("bebop") >= 0 ||
           ta.indexOf("groove") >= 0 || ta.indexOf("smooth") >= 0 ||
           ta.indexOf("lounge") >= 0 || ta.indexOf("coltrane") >= 0 ||
           ta.indexOf("miles") >= 0 || ta.indexOf("quartet") >= 0) {
    songP.beatInterval = 520 + (h % 220);
    songP.bounceAmp = 1.5f + (float)((h >> 3) % 20) * 0.1f;
    songP.armAmp = 16.0f + (float)((h >> 7) % 22);
    songP.headAmp = 2.5f + (float)((h >> 11) % 6);
    songP.noteSpeed = 0.07f + (float)((h >> 15) % 8) * 0.01f;
    songP.noteInterval = 550 + ((h >> 5) % 300);
    songP.mouthSensitivity = 0.65f;
    songP.eyeSquintDepth = 0.35f;
    songP.startStyle = 0; // GROOVE
    songP.styleOrder[0] = 0;
    songP.styleOrder[1] = 2;
    songP.styleOrder[2] = 0;
    songP.styleOrder[3] = 1;
    songP.stayOnStyle = false;
    strncpy(songP.genreTag, "jazz/soul", 11);
  }
  // ROCK / METAL / PUNK / GUITAR
  else if (ta.indexOf("rock") >= 0 || ta.indexOf("metal") >= 0 ||
           ta.indexOf("punk") >= 0 || ta.indexOf("heavy") >= 0 ||
           ta.indexOf("guitar") >= 0 || ta.indexOf("riff") >= 0 ||
           ta.indexOf("nirvana") >= 0 || ta.indexOf("acdc") >= 0 ||
           ta.indexOf("sabbath") >= 0 || ta.indexOf("metallica") >= 0 ||
           ta.indexOf("maiden") >= 0 || ta.indexOf("slayer") >= 0) {
    songP.beatInterval = 330 + (h % 180);
    songP.bounceAmp = 3.0f + (float)((h >> 3) % 22) * 0.1f;
    songP.armAmp = 30.0f + (float)((h >> 7) % 20);
    songP.headAmp = 4.0f + (float)((h >> 11) % 7);
    songP.noteSpeed = 0.12f + (float)((h >> 15) % 10) * 0.01f;
    songP.noteInterval = 280 + ((h >> 5) % 220);
    songP.mouthSensitivity = 0.9f;
    songP.eyeSquintDepth = 0.7f;
    // Rock cycles HYPE → GROOVE → HYPE
    songP.startStyle = 3;
    songP.styleOrder[0] = 3;
    songP.styleOrder[1] = 0;
    songP.styleOrder[2] = 3;
    songP.styleOrder[3] = 1;
    songP.stayOnStyle = false;
    strncpy(songP.genreTag, "rock", 11);
  }
  // HIP-HOP / RAP (non-trap)
  else if (ta.indexOf("hip hop") >= 0 || ta.indexOf("hiphop") >= 0 ||
           ta.indexOf(" rap") >= 0 || ta.indexOf("rapper") >= 0 ||
           ta.indexOf("kendrick") >= 0 || ta.indexOf("j. cole") >= 0 ||
           ta.indexOf("drake") >= 0 || ta.indexOf("kanye") >= 0 ||
           ta.indexOf("eminem") >= 0 || ta.indexOf("jay-z") >= 0 ||
           ta.indexOf("biggie") >= 0 || ta.indexOf("tupac") >= 0) {
    songP.beatInterval = 420 + (h % 200);
    songP.bounceAmp = 2.2f + (float)((h >> 3) % 18) * 0.1f;
    songP.armAmp = 22.0f + (float)((h >> 7) % 24);
    songP.headAmp = 2.5f + (float)((h >> 11) % 6);
    songP.noteSpeed = 0.09f + (float)((h >> 15) % 10) * 0.01f;
    songP.noteInterval = 380 + ((h >> 5) % 300);
    songP.mouthSensitivity = 0.75f;
    songP.eyeSquintDepth = 0.5f;
    songP.startStyle = ord[0]; // hash-based start
    songP.styleOrder[0] = ord[0];
    songP.styleOrder[1] = 3;
    songP.styleOrder[2] = ord[1];
    songP.styleOrder[3] = 1;
    songP.stayOnStyle = false;
    strncpy(songP.genreTag, "hiphop", 11);
  }
  // POP / KPOP / BOLLYWOOD / MAINSTREAM
  else if (ta.indexOf("kpop") >= 0 || ta.indexOf("k-pop") >= 0 ||
           ta.indexOf("bts") >= 0 || ta.indexOf("blackpink") >= 0 ||
           ta.indexOf("taylor") >= 0 || ta.indexOf("ariana") >= 0 ||
           ta.indexOf("bieber") >= 0 || ta.indexOf("weekend") >= 0 ||
           ta.indexOf("bollywood") >= 0 || ta.indexOf("filmi") >= 0 ||
           ta.indexOf("hindi") >= 0 || ta.indexOf("punjabi") >= 0) {
    songP.beatInterval = 450 + (h % 200);
    songP.bounceAmp = 2.0f + (float)((h >> 3) % 20) * 0.1f;
    songP.armAmp = 20.0f + (float)((h >> 7) % 25);
    songP.headAmp = 2.5f + (float)((h >> 11) % 6);
    songP.noteSpeed = 0.08f + (float)((h >> 15) % 12) * 0.01f;
    songP.noteInterval = 450 + ((h >> 5) % 300);
    songP.mouthSensitivity = 0.7f;
    songP.eyeSquintDepth = 0.45f;
    songP.startStyle = (h % 2 == 0) ? 1 : 0; // DISCO or GROOVE
    memcpy(songP.styleOrder, ord, sizeof(ord));
    songP.stayOnStyle = false;
    strncpy(songP.genreTag, "pop", 11);
  }
  // CLASSICAL / ORCHESTRAL / INSTRUMENTAL
  else if (ta.indexOf("classical") >= 0 || ta.indexOf("symphony") >= 0 ||
           ta.indexOf("orchestra") >= 0 || ta.indexOf("concerto") >= 0 ||
           ta.indexOf("sonata") >= 0 || ta.indexOf("beethoven") >= 0 ||
           ta.indexOf("mozart") >= 0 || ta.indexOf("bach") >= 0 ||
           ta.indexOf("chopin") >= 0 || ta.indexOf("vivaldi") >= 0) {
    songP.beatInterval = 680 + (h % 250);
    songP.bounceAmp = 1.0f + (float)((h >> 3) % 15) * 0.1f;
    songP.armAmp = 10.0f + (float)((h >> 7) % 18);
    songP.headAmp = 1.5f + (float)((h >> 11) % 5);
    songP.noteSpeed = 0.05f + (float)((h >> 15) % 7) * 0.005f;
    songP.noteInterval = 800 + ((h >> 5) % 400);
    songP.mouthSensitivity = 0.5f;
    songP.eyeSquintDepth = 0.6f;
    songP.startStyle = 2; // WAVE — elegant
    songP.styleOrder[0] = 2;
    songP.styleOrder[1] = 0;
    songP.styleOrder[2] = 2;
    songP.styleOrder[3] = 0;
    songP.stayOnStyle = true;
    strncpy(songP.genreTag, "classical", 11);
  }
  // LOFI / CHILLHOP / AMBIENT
  else if (ta.indexOf("lofi") >= 0 || ta.indexOf("lo-fi") >= 0 ||
           ta.indexOf("lo fi") >= 0 || ta.indexOf("chill hop") >= 0 ||
           ta.indexOf("chillhop") >= 0 || ta.indexOf("ambient") >= 0 ||
           ta.indexOf("study") >= 0 || ta.indexOf("focus") >= 0 ||
           ta.indexOf("rainy") >= 0 || ta.indexOf("coffee") >= 0) {
    songP.beatInterval = 650 + (h % 250);
    songP.bounceAmp = 1.0f + (float)((h >> 3) % 12) * 0.1f;
    songP.armAmp = 8.0f + (float)((h >> 7) % 16);
    songP.headAmp = 1.0f + (float)((h >> 11) % 4);
    songP.noteSpeed = 0.04f + (float)((h >> 15) % 7) * 0.005f;
    songP.noteInterval = 950 + ((h >> 5) % 350);
    songP.mouthSensitivity = 0.42f;
    songP.eyeSquintDepth = 0.55f;
    songP.startStyle = 2; // WAVE
    songP.styleOrder[0] = 2;
    songP.styleOrder[1] = 0;
    songP.styleOrder[2] = 2;
    songP.styleOrder[3] = 0;
    songP.stayOnStyle = true;
    strncpy(songP.genreTag, "lofi", 11);
  }

  Serial.println("SongP genre=" + String(songP.genreTag) +
                 " beat=" + String(songP.beatInterval) +
                 " bounce=" + String(songP.bounceAmp) +
                 " style=" + String(songP.startStyle) +
                 " stay=" + String(songP.stayOnStyle));
}

// Waveform bars for dance screen
float waveformBars[8] = {0};
unsigned long lastWaveUpdate = 0;

// Disco ball sparkles
struct Sparkle {
  float x, y, age;
  bool active;
};
Sparkle sparkles[10];
unsigned long lastSparkleSpawn = 0;

// ================= HELPERS =================
void centerText(const char *t, int y, int s) {
  display.setTextSize(s);
  int w = strlen(t) * 6 * s;
  display.setCursor((128 - w) / 2, y);
  display.print(t);
}
void showMessage(const char *m) {
  display.clearDisplay();
  centerText(m, 30, 1);
  display.display();
}

// ================= FORWARD DECLARATIONS =================
void syncTime();
void fetchWeather();
void showClock();
void showWeather();
void animateClouds();
void drawMainCloud(int x, int y);
void drawSmallCloud(int x, int y);
void triggerModeChange(int m);
void updateBoredom(unsigned long now);
void triggerSingleTapAction();
void triggerLongPressAction();
void releaseLongPress();
void handleInput();
void detectKnockPattern();
void drawEyes();
void drawMouth();
void drawOverlayExtras();
void drawBeatOverlay();
void updateOverlays(unsigned long now);
void updateAliveAnimations();
void updateHeartbeat();
void triggerYawn(int dur);
void registerBeatHit();
void updateBeat(unsigned long now);
void drawDeathScene(unsigned long now);
void drawSpeakerIcon(int x, int y, bool muted);
void drawHeart(int x, int y, float s);
void drawStar(int cx, int cy, int r);
void drawSweatDrop(int x, int y);
void drawExclamation(int x, int y);
void drawSingleTear(int x, int y);
void drawAngryFire(int cx, int by);
void drawSpiral(int cx, int cy, int dir, float rot);
void wrapTextToLines(const char *text, char *line1, char *line2);
void parseGBPacket(String json);
void startDancing();
void stopDancing();
void updateDance(unsigned long now);
void parseSongPersonality(const char *track, const char *artist);
void drawDanceFace();
void drawMusicNote(int x, int y, int symbol);
void drawNotifOverlay();
void drawMusicPopup();
void drawMusicBar();
void drawWeatherFace();
void drawCallScreen();
void handleCallTimeout();
void drawDisco(unsigned long now);
void drawWaveform(unsigned long now);
void drawNoteExplosion(unsigned long now);
void onBLEDisconnectCleanup(); // cancel phone-dependent activities
void updateEmotionalMemory(unsigned long now);
void updateQuirks(unsigned long now);
void updateAttentionSeeking(unsigned long now);
void updateStartleChain(unsigned long now);
void updateForgiveness(unsigned long now);
void updatePurring(unsigned long now);
void updateJealousy(unsigned long now);
void updateWakeUpDrama(unsigned long now);
void drawQuirkOverlay();
void drawPurrHearts(unsigned long now);
void changeAffection(int delta, const char *reason);
void triggerStartleChain();
void startWakeUpDrama();
void updateHydration(unsigned long now);
void updatePosture(unsigned long now);
void updateDramaticDeath(unsigned long now);
void drawHydrationOverlay(unsigned long now);
void drawPostureOverlay(unsigned long now);
void drawDeathScene(unsigned long now);
void startDramaticDeath();
void drawHeart(int x, int y, float s) {
  int r = 8 * s, ox = 8 * s, oy = 5 * s, th = 16 * s;
  display.fillCircle(x - ox, y - oy, r, WHITE);
  display.fillCircle(x + ox, y - oy, r, WHITE);
  display.fillTriangle(x - (r * 2), y - oy, x + (r * 2), y - oy, x, y + th,
                       WHITE);
}
void drawStar(int cx, int cy, int r) {
  float a = -M_PI / 2.0f, ir = r * 0.4f;
  for (int i = 0; i < 5; i++) {
    float a1 = a + i * (2.0f * M_PI / 5.0f), a2 = a1 + M_PI / 5.0f,
          a3 = a + (i + 1) * (2.0f * M_PI / 5.0f);
    int x1 = cx + (int)((float)cos(a1) * r),
        y1 = cy + (int)((float)sin(a1) * r),
        x2 = cx + (int)((float)cos(a2) * ir),
        y2 = cy + (int)((float)sin(a2) * ir),
        x3 = cx + (int)((float)cos(a3) * r),
        y3 = cy + (int)((float)sin(a3) * r);
    display.fillTriangle(cx, cy, x1, y1, x2, y2, WHITE);
    display.fillTriangle(cx, cy, x2, y2, x3, y3, WHITE);
  }
}
void drawSweatDrop(int x, int y) {
  display.fillCircle(x, y, 3, WHITE);
  display.fillTriangle(x - 2, y - 1, x + 2, y - 1, x, y - 7, WHITE);
}
void drawExclamation(int x, int y) {
  display.fillRect(x - 1, y, 3, 8, WHITE);
  display.fillRect(x - 1, y + 10, 3, 3, WHITE);
}
void drawSingleTear(int x, int y) {
  display.fillCircle(x, y, 2, WHITE);
  display.fillTriangle(x - 1, y, x + 1, y, x, y - 5, WHITE);
}
void drawAngryFire(int cx, int by) {
  int f = (millis() / 100) % 2;
  if (f == 0) {
    display.fillTriangle(cx - 6, by, cx + 6, by, cx, by - 14, WHITE);
    display.fillTriangle(cx - 9, by - 2, cx - 5, by - 2, cx - 7, by - 8, WHITE);
    display.fillTriangle(cx + 5, by - 2, cx + 9, by - 2, cx + 7, by - 8, WHITE);
  } else {
    display.fillTriangle(cx - 7, by, cx + 7, by, cx, by - 16, WHITE);
    display.fillTriangle(cx - 11, by - 4, cx - 7, by - 4, cx - 9, by - 10,
                         WHITE);
    display.fillTriangle(cx + 7, by - 4, cx + 11, by - 4, cx + 9, by - 10,
                         WHITE);
  }
}
void drawSpiral(int cx, int cy, int dir, float rot) {
  float maxR = BASE_EYE_W * 0.45f; // proportional to eye size
  float a = 0, r = 0;
  while (r < maxR) {
    float ea = (a + rot) * dir;
    int x = cx + (int)((float)cos(ea) * r), y = cy + (int)((float)sin(ea) * r);
    if (x >= 0 && x < 128 && y >= 0 && y < 64) {
      display.drawPixel(x, y, WHITE);
      display.drawPixel(min(127, x + 1), y, WHITE);
    }
    a += 0.4;
    r += 0.25;
  }
}

// ================= BLE CALLBACKS =================
// IMPORTANT: No delay() calls inside callbacks! They block the BLE stack
// thread and cause connection supervision timeouts over time.
// Instead we set flags and handle handshake from loop().
class BotServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s, esp_ble_gatts_cb_param_t *param) {
    bleConnected = true;
    bleBuffer[0] = '\0';
    lastBLEActivity = millis();
    bleDisconnectedSince = 0; // clear watchdog
    bleConsecutiveFails = 0;
    bleRestartCycles = 0; // successful connection — reset restart limit
    bleReconnectCount++;
    // Capture peer address for connection parameter updates
    if (param) {
      memcpy(blePeerAddr, param->connect.remote_bda, sizeof(esp_bd_addr_t));
      blePeerAddrValid = true;
      Serial.printf("BLE peer: %02X:%02X:%02X:%02X:%02X:%02X\n", blePeerAddr[0],
                    blePeerAddr[1], blePeerAddr[2], blePeerAddr[3],
                    blePeerAddr[4], blePeerAddr[5]);
    }
    // Defer handshake to loop() — no blocking here!
    bleJustConnected = true;
    bleConnectTime = millis();
    bleHandshakeStep = 0;
    Serial.println("BLE connected (#" + String(bleReconnectCount) + ")");
  }
  void onDisconnect(BLEServer *s) {
    bleConnected = false;
    bleJustConnected = false;
    bleHandshakeStep = 0;
    blePeerAddrValid = false;
    bleBuffer[0] = '\0';
    bleDisconnectedSince = millis();
    onBLEDisconnectCleanup();
    Serial.println("BLE disconnected — will re-advertise from loop");
    // Don't call startAdvertising() here — handled in loop() watchdog
  }
};

void parseGBPacket(String json);
void parseRawLine(String line);

class BotRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    String val = c->getValue();
    if (val.length() == 0)
      return;
    Serial.print("BLE RAW: ");
    Serial.println(val.c_str());

    // ---- HEARTBEAT: we got data, connection is alive! ----
    lastBLEActivity = millis();

    String chunk = val;

    // ---- Check for setTime IMMEDIATELY in every chunk ----
    // Don't buffer it — parse right away before anything else
    if (chunk.indexOf("setTime(") >= 0) {
      parseRawLine(chunk);
      // Don't return — chunk might also have GB() packets after
    }

    // Append to buffer
    int room = sizeof(bleBuffer) - strlen(bleBuffer) - 1;
    if (room > 0)
      strncat(bleBuffer, val.c_str(), room);
    else {
      bleBuffer[0] = '\0';
      strncat(bleBuffer, val.c_str(), sizeof(bleBuffer) - 1);
    }

    // Process all complete GB({...}) packets in buffer
    bool found = true;
    while (found) {
      found = false;

      char *sp = strstr(bleBuffer, "GB(");
      if (!sp) {
        // No GB packet — clear junk but keep if looks like incomplete packet
        if (strlen(bleBuffer) > 400)
          bleBuffer[0] = '\0';
        break;
      }

      // Clear anything before GB(
      if (sp != bleBuffer)
        memmove(bleBuffer, sp, strlen(sp) + 1);

      char *jsonStart = bleBuffer + 3;
      if (*jsonStart != '{') {
        bleBuffer[0] = '\0';
        break;
      }

      int depth = 0;
      char *ep = nullptr;
      for (char *p = jsonStart; *p; p++) {
        if (*p == '{')
          depth++;
        else if (*p == '}') {
          depth--;
          if (depth == 0) {
            ep = p;
            break;
          }
        }
      }
      if (!ep)
        break; // incomplete — wait for more chunks

      int jlen = ep - jsonStart + 1;
      char json[512] = {0};
      if (jlen > 511)
        jlen = 511;
      strncpy(json, jsonStart, jlen);
      Serial.print("GB packet: ");
      Serial.println(json);
      parseGBPacket(String(json));

      char *after = ep + 1;
      if (*after == ')')
        after++;
      if (*after == '\n')
        after++;
      memmove(bleBuffer, after, strlen(after) + 1);
      found = true;
    }
  }
};

void parseRawLine(String line) {
  Serial.println("Raw line: " + line.substring(0, 60));

  // setTime(epoch) — Gadgetbridge sends unix timestamp
  int si = line.indexOf("setTime(");
  if (si >= 0) {
    si += 8;
    String tsStr = "";
    while (si < (int)line.length()) {
      char c = line[si];
      if (isdigit(c) || c == '.') {
        tsStr += c;
        si++;
      } else
        break;
    }
    if (tsStr.length() > 5) {
      time_t epoch = (time_t)tsStr.toFloat();
      // Add IST offset (19800 seconds = 5:30 hrs) since GB sends UTC
      epoch += 19800;
      struct timeval tv = {epoch, 0};
      settimeofday(&tv, NULL);
      timeReady = true;
      Serial.println("Time set! epoch=" + tsStr);
      // Verify
      struct tm t;
      if (getLocalTime(&t)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M %d/%m/%Y", &t);
        Serial.println("Verified: " + String(buf));
      }
    }
  }

  // E.setTimeZone(offset) — we handle IST manually above, just log
  int ti = line.indexOf("setTimeZone(");
  if (ti >= 0) {
    ti += 12;
    String tz = "";
    while (ti < (int)line.length()) {
      char c = line[ti];
      if (isdigit(c) || c == '-' || c == '.') {
        tz += c;
        ti++;
      } else
        break;
    }
    Serial.println("TZ: " + tz + " hrs");
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(400000); // 400kHz I2C for faster OLED updates

  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("SH1106 OLED FAILED!");
    for (;;)
      ;
  }
  delay(100); // let charge pump stabilize
  display.clearDisplay();
  display.display();
  display.setContrast(255); // max brightness
  display.setRotation(0);   // normal orientation
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false); // prevent text wrapping off screen

  // ===== CUTE MOCHI BOOT ANIMATION =====
  // Inspired by Daiso Mochi — round, soft, adorable

  // --- Frame 1: Sleeping Mochi (500ms) ---
  display.clearDisplay();
  // Round mochi body outline
  display.drawRoundRect(29, 4, 70, 46, 18, WHITE);
  // Closed eyes — cute horizontal lines
  display.fillRect(44, 22, 12, 2, WHITE); // left eye closed
  display.fillRect(72, 22, 12, 2, WHITE); // right eye closed
  // Tiny sleeping mouth — small "u"
  display.drawPixel(63, 36, WHITE);
  display.drawPixel(64, 37, WHITE);
  display.drawPixel(65, 36, WHITE);
  // Floating zzz
  display.setTextSize(1);
  display.setCursor(96, 6);
  display.print("z");
  display.setCursor(100, 2);
  display.print("z");
  display.setCursor(105, 0);
  display.print("Z");
  // Blush circles (even in sleep!)
  display.drawCircle(40, 30, 3, WHITE);
  display.drawCircle(88, 30, 3, WHITE);
  // "MOCHII" title — large
  display.setTextSize(1);
  centerText("~ sleeping ~", 54, 1);
  display.display();
  delay(600);

  // --- Frame 2: Eyes fluttering open (400ms) ---
  display.clearDisplay();
  display.drawRoundRect(29, 4, 70, 46, 18, WHITE);
  // Half-open eyes — small ovals
  display.fillRoundRect(44, 20, 12, 5, 2, WHITE);
  display.fillRoundRect(72, 20, 12, 5, 2, WHITE);
  // Tiny pupils peeking
  display.fillRect(48, 21, 3, 3, BLACK);
  display.fillRect(76, 21, 3, 3, BLACK);
  // Mouth — tiny "o" surprise
  display.drawCircle(64, 37, 2, WHITE);
  // Blush
  display.drawCircle(40, 30, 3, WHITE);
  display.drawCircle(88, 30, 3, WHITE);
  // Sparkles appearing
  display.drawPixel(20, 15, WHITE);
  display.drawPixel(108, 10, WHITE);
  display.drawPixel(15, 35, WHITE);
  centerText("* MOCHII *", 54, 1);
  display.display();
  delay(400);

  // --- Frame 3: Fully awake! Big cute eyes (800ms) ---
  display.clearDisplay();
  // Mochi body — slightly bounced up
  display.drawRoundRect(29, 2, 70, 46, 18, WHITE);
  display.drawRoundRect(30, 3, 68, 44, 17, WHITE); // double line = chonky
  // BIG round eyes — the signature look!
  display.fillRoundRect(41, 14, 16, 20, 7, WHITE); // left eye
  display.fillRoundRect(71, 14, 16, 20, 7, WHITE); // right eye
  // Pupils — looking at you!
  display.fillRoundRect(46, 17, 7, 10, 3, BLACK); // left pupil
  display.fillRoundRect(76, 17, 7, 10, 3, BLACK); // right pupil
  // Cute shine dots in eyes
  display.fillRect(47, 18, 2, 2, WHITE); // left eye shine
  display.fillRect(77, 18, 2, 2, WHITE); // right eye shine
  // Happy mouth — wide smile curve
  display.fillCircle(64, 38, 6, WHITE);
  display.fillCircle(64, 35, 6, BLACK); // cut top half for smile shape
  // Rosy cheek blush
  for (int i = 0; i < 3; i++) {
    display.drawCircle(36, 31, 3 + i, WHITE);
    display.drawCircle(92, 31, 3 + i, WHITE);
  }
  // Sparkle decorations around
  display.fillCircle(18, 8, 1, WHITE); // star dot
  display.fillCircle(110, 5, 1, WHITE);
  display.fillCircle(12, 28, 1, WHITE);
  display.fillCircle(116, 25, 1, WHITE);
  display.fillCircle(22, 42, 1, WHITE);
  display.fillCircle(106, 40, 1, WHITE);
  // Small hearts
  // Left heart
  display.fillCircle(14, 18, 2, WHITE);
  display.fillCircle(18, 18, 2, WHITE);
  display.fillTriangle(12, 19, 20, 19, 16, 24, WHITE);
  // Right heart
  display.fillCircle(108, 16, 2, WHITE);
  display.fillCircle(112, 16, 2, WHITE);
  display.fillTriangle(106, 17, 114, 17, 110, 22, WHITE);
  // Title text
  display.setTextSize(1);
  centerText("~ MOCHII ~", 52, 1);
  display.display();
  delay(800);

  // --- Frame 4: Waking up text with bounce (600ms) ---
  display.clearDisplay();
  display.drawRoundRect(29, 4, 70, 46, 18, WHITE);
  display.drawRoundRect(30, 5, 68, 44, 17, WHITE);
  // Happy squint eyes — ^  ^
  // Left eye squint
  display.drawLine(42, 24, 49, 18, WHITE);
  display.drawLine(49, 18, 56, 24, WHITE);
  display.drawLine(42, 25, 49, 19, WHITE);
  display.drawLine(49, 19, 56, 25, WHITE);
  // Right eye squint
  display.drawLine(72, 24, 79, 18, WHITE);
  display.drawLine(79, 18, 86, 24, WHITE);
  display.drawLine(72, 25, 79, 19, WHITE);
  display.drawLine(79, 19, 86, 25, WHITE);
  // Big happy open mouth
  display.fillCircle(64, 38, 7, WHITE);
  display.fillCircle(64, 34, 7, BLACK);
  // Blush
  display.fillCircle(38, 30, 3, WHITE);
  display.drawCircle(38, 30, 4, WHITE);
  display.fillCircle(90, 30, 3, WHITE);
  display.drawCircle(90, 30, 4, WHITE);
  // Stars burst
  // 4-point stars
  display.drawLine(16, 12, 16, 6, WHITE);
  display.drawLine(13, 9, 19, 9, WHITE);
  display.drawLine(110, 8, 110, 2, WHITE);
  display.drawLine(107, 5, 113, 5, WHITE);
  // More sparkle dots
  display.fillCircle(8, 30, 1, WHITE);
  display.fillCircle(120, 28, 1, WHITE);
  display.fillCircle(20, 44, 1, WHITE);
  display.fillCircle(108, 42, 1, WHITE);
  // Title + subtitle
  centerText("waking upp ~", 54, 1);
  display.display();
  delay(600);

  // Buzzer init — LEDC PWM channel for tone generation
  ledcAttach(BUZZER_PIN, 2000, BUZZER_LEDC_RES);
  buzzerToneOff();      // start silent
  buzzerPlay(SND_BOOT); // boot-up chime ♪
  // Calibrate touch sensor (must be done before anything touches it!)
  calibrateTouch();
  // Time set via Gadgetbridge (GB sends setTime packet on connect)
  // Weather set via Gadgetbridge weather packets
  randomSeed(analogRead(0));
  lastInteractionTime = millis();
  randomMidYawnTime = random(20000, 40000);
  nextMoodChangeAt = millis() + random(300000, 600000);

  // Init music notes as inactive
  for (int i = 0; i < 6; i++)
    notes[i].active = false;
  // Init sparkles as inactive
  for (int i = 0; i < 10; i++)
    sparkles[i].active = false;
  danceStyle = 0;
  danceStyleStart = 0;
  isIncomingCall = false;
  callActive = false;
  callEnded = false;
  isBored = false;
  boredStage = 0;
  boredStartTime = 0;
  pendingMode = -1;
  transitionFlash = 0;
  holdHappyTriggered = false;
  holdAngryTriggered = false;

  // BLE setup — advertise as BangleJS for Gadgetbridge
  BLEDevice::init(BLE_DEVICE_NAME);

  // Boost TX power to maximum (+9dBm) for stronger, more stable connection
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, ESP_PWR_LVL_P9);
  Serial.println("BLE TX power set to +9dBm (max)");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BotServerCallbacks());
  BLEService *pSvc = pServer->createService(UART_SERVICE_UUID);
  pTxChar = pSvc->createCharacteristic(UART_TX_UUID,
                                       BLECharacteristic::PROPERTY_NOTIFY);
  pTxChar->addDescriptor(new BLE2902());
  BLECharacteristic *pRx = pSvc->createCharacteristic(
      UART_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRx->setCallbacks(new BotRxCallbacks());
  pSvc->start();
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(UART_SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->setMinPreferred(0x06);
  pAdv->setMinInterval(0x20); // 20ms — fast advertising for quick reconnection
  pAdv->setMaxInterval(0x40); // 40ms
  BLEDevice::startAdvertising();
  Serial.println("BLE ready: " + String(BLE_DEVICE_NAME));

  // Hardware watchdog — auto-reboot if system completely freezes (30s timeout)
  {
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms = 30000,  // 30 seconds
        .idle_core_mask = 0,  // don't watch idle tasks
        .trigger_panic = true // reboot on timeout
    };
    esp_task_wdt_reconfigure(&wdtCfg);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    Serial.println("Hardware WDT enabled (30s)");
  }
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  // Feed hardware watchdog — prevents auto-reboot on system freeze
  esp_task_wdt_reset();

  // Update buzzer tone sequencer (non-blocking)
  buzzerUpdate();

  handleInput();

  // ---- SMOOTH TRANSITION: apply pending mode after bridge delay ----
  if (pendingMode >= 0 && now >= modeChangeAt) {
    triggerModeChange(pendingMode);
    pendingMode = -1;
  }
  // Fade transition flash
  if (transitionFlash > 0)
    transitionFlash *= 0.80f;
  if (transitionFlash < 0.02f)
    transitionFlash = 0;

  // ---- BOREDOM STATE MACHINE ----
  updateBoredom(now);

  // Sleep sequence — works from ANY base mood after long idle
  // In alive mode: yawn → drift → sleep (the cute sequence)
  // In other moods: just fade eyes closed → sleep (you don't yawn while angry!)
  if (!isSleeping && currentMode <= 7 && !isBeingPetted && !isDancing &&
      !isIncomingCall && !callActive && !isBored) {
    unsigned long el = now - lastInteractionTime;

    if (currentMode == 0) {
      // === ALIVE MODE: full yawn → drift → sleep sequence ===
      if (!hasMidYawned && el > randomMidYawnTime) {
        triggerYawn(2500);
        hasMidYawned = true;
      }
      if (!hasFinalYawned && el > (SLEEP_TIMEOUT - 6000)) {
        triggerYawn(3500);
        hasFinalYawned = true;
      }
      if (!isDriftingOff && el > (SLEEP_TIMEOUT - 2000)) {
        isDriftingOff = true;
        if (!buzzerPlaying) buzzerPlay(SND_SLEEP); // play only once
        targetEyeOpenFactor = 0;
        targetYawnFactor = 0;
        targetMouthX = 0;
      }
      if (el > SLEEP_TIMEOUT)
        isSleeping = true;
    } else {
      // === OTHER MOODS: gentle fade to sleep ===
      // Start drifting off at timeout-3s, sleep at timeout
      if (!isDriftingOff && el > (SLEEP_TIMEOUT - 3000)) {
        isDriftingOff = true;
        targetEyeOpenFactor = 0; // eyes close
        // Clear any active overlays so sleep isn't fighting them
        ovNervous = false;
        ovSleepy = false;
        ovShocked = false;
        ovDisgusted = false;
        ovEmbarrassed = false;
        ovThinking = false;
        ovHyper = false;
        ovPain = false;
        ovMelting = false;
        ovCryLaugh = false;
        isDoingQuirk = false;
        isPurring = false;
        isStartled = false;
        Serial.println("Fading to sleep from mood " + String(currentMode));
      }
      if (el > SLEEP_TIMEOUT) {
        triggerModeChange(0); // reset to alive for clean wake-up
        isSleeping = true; // MUST be after triggerModeChange (which clears it)
        Serial.println("Sleeping! (was in mood " + String(currentMode) + ")");
      }
    }
  }

  // ================= HUMAN-LIKE DYNAMIC MOOD SYSTEM =================
  // Bot reacts to time, weather, affection, day-of-week — like a real buddy.
  // Mood changes feel earned, not random. Affection & personality shape
  // reactions.

  // Pause mood timer when in clock/weather/sleeping/needsConsoling
  // When sad and needsConsoling, the bot WON'T auto-switch moods.
  // You must hold/pet it to console it first!
  // Safety: only block if actually in sad mode — prevents stale flag from
  // locking mood
  if (currentMode >= 8 || isSleeping || (needsConsoling && currentMode == 3)) {
    nextMoodChangeAt = now + 600000;
  }
  // Safety cleanup: if needsConsoling is set but we're not in sad mode, clear
  // it
  if (needsConsoling && currentMode != 3) {
    needsConsoling = false;
    isBeingConsoled = false;
  }

  // Mood cooldown — don't switch if current mood just started
  bool moodCooldownOk = (now - moodStartedAt > MIN_MOOD_DURATION);

  if (!isSleeping && currentMode <= 7 && now > nextMoodChangeAt &&
      moodCooldownOk) {
    struct tm tm;
    bool hasTime = getLocalTime(&tm);
    int hr = hasTime ? tm.tm_hour : 12;
    int dow = hasTime ? tm.tm_wday : 3; // 0=Sun..6=Sat
    unsigned long idle = now - lastInteractionTime;

    int newMood = currentMode;
    const char *reason = "";

    // --- AFFECTION-BIASED MOOD SELECTION ---
    // High affection → more love/happy. Low → more smug/dismissive.
    bool isAdoring = (affectionScore >= 80);
    bool isFriendly = (affectionScore >= 50);
    bool isDismissive = (affectionScore < 20);
    bool isHostile = (affectionScore < 0);

    // --- DAY-OF-WEEK PERSONALITY ---
    bool isMonday = (dow == 1);
    bool isFriday = (dow == 5);
    bool isWeekend = (dow == 0 || dow == 6);

    // --- TIME OF DAY personality (enhanced with affection + day) ---
    if (hr >= 5 && hr < 7) {
      // Early morning — DRAMATICALLY groggy
      if (isMonday) {
        newMood = 2;
        reason = "monday morning rage";
      } // angry on mondays!
      else {
        newMood = 3;
        reason = "early morning tired";
      }
    } else if (hr >= 7 && hr < 9) {
      // Morning — waking up
      if (isFriday) {
        newMood = 5;
        reason = "FRIDAY energy!!";
      } else if (isMonday) {
        newMood = (random(0, 2) == 0) ? 3 : 4;
        reason = "monday ugh";
      } else if (isAdoring) {
        newMood = (random(0, 2) == 0) ? 1 : 5;
        reason = "morning love";
      } else {
        newMood = (random(0, 3) == 0) ? 5 : 0;
        reason = "morning energy";
      }
    } else if (hr >= 9 && hr < 12) {
      // Late morning — productive vibes
      if (isDismissive) {
        newMood = 7;
        reason = "smug morning (low affection)";
      } else if (isAdoring) {
        newMood = (random(0, 3) == 0) ? 1 : 0;
        reason = "happy focused";
      } else {
        int r = random(0, 4);
        newMood = (r == 0) ? 7 : (r == 1) ? 0 : 0;
        reason = "focused morning";
      }
    } else if (hr >= 12 && hr < 14) {
      // Lunch — food excitement arc
      if (random(0, 3) == 0) {
        newMood = 5;
        reason = "FOOD TIME!!";
      } else if (isFriendly) {
        newMood = 1;
        reason = "lunch love";
      } else {
        newMood = 0;
        reason = "lunch calm";
      }
    } else if (hr >= 14 && hr < 16) {
      // Post lunch slump — sleepy, dramatic
      if (isWeekend) {
        newMood = 0;
        reason = "weekend chill";
      } else {
        newMood = (random(0, 3) == 0) ? 3 : 4;
        reason = "afternoon slump";
      }
    } else if (hr >= 16 && hr < 18) {
      // Late afternoon — getting active
      if (isFriday) {
        newMood = 5;
        reason = "almost done for the week!";
      } else {
        newMood = (random(0, 2) == 0) ? 5 : 0;
        reason = "late afternoon";
      }
    } else if (hr >= 18 && hr < 20) {
      // Evening — loving, content
      if (isAdoring) {
        newMood = 1;
        reason = "evening love";
      } else if (isDismissive) {
        newMood = 7;
        reason = "smug evening";
      } else {
        newMood = (random(0, 3) == 0) ? 1 : (random(0, 2) == 0) ? 5 : 0;
        reason = "evening vibes";
      }
    } else if (hr >= 20 && hr < 22) {
      // Night — chilling
      if (isWeekend) {
        newMood = (random(0, 2) == 0) ? 5 : 1;
        reason = "weekend night!";
      } else if (isHostile) {
        newMood = 2;
        reason = "angry night (low affection)";
      } else {
        newMood = (random(0, 3) == 0) ? 7 : 0;
        reason = "night chill";
      }
    } else if (hr >= 22 && hr < 24) {
      // Late night — existential, dramatic
      if (isAdoring) {
        newMood = (random(0, 2) == 0) ? 1 : 3;
        reason = "emotional late night";
      } else {
        newMood = (random(0, 3) == 0) ? 3 : 4;
        reason = "late night mood";
      }
    } else if (hr >= 0 && hr < 2) {
      // Midnight — dramatic "why are we still awake" energy
      newMood = (random(0, 2) == 0) ? 4 : 3;
      reason = "existential midnight";
    } else { // 2-5am
      // Deep night — sad/scared
      newMood = (random(0, 3) == 0) ? 6 : 3;
      reason = "deep night spooky";
    }

    // --- WEATHER OVERRIDES ---
    if (weatherReady && !isnan(outdoorTemp)) {
      if (outdoorTemp > 38 && newMood == 0) {
        newMood = 3;
        reason = "too hot suffering";
      }
      if (outdoorTemp < 15 && newMood == 0) {
        newMood = 5;
        reason = "cool weather energy";
      }
      if (outdoorTemp > 42) {
        newMood = 4;
        reason = "MELTING";
      }
    }

    // --- IDLE TIME — boredom system handles prolonged ignoring separately ---
    if (isBored && boredStage >= 2)
      goto mood_done;

    // Never auto angry/scared — touch only (unless hostile affection or
    // specific time triggers)
    if (!isHostile && !isMonday && (newMood == 2 || newMood == 6))
      newMood = 0;
    if (newMood == currentMode) {
      nextMoodChangeAt = now + random(180000, 420000);
      goto mood_done;
    }

    // Smooth transition overlays — brief bridge before the mode applies
    {
      unsigned long n = millis();
      if (newMood == 1) {
        ovEmbarrassed = true;
        ovEmbarrassedEnd = n + 1200;
        transitionFlash = 0.5f;
      } else if (newMood == 5) {
        ovShocked = true;
        ovShockedEnd = n + 350;
        transitionFlash = 0.8f;
      } else if (newMood == 3) {
        ovSleepy = true;
        ovSleepyEnd = n + 1800;
        sleepDroop = 0;
        transitionFlash = 0.3f;
      } else if (newMood == 7) {
        isSuspicious = true;
        suspiciousEndTime = n + 1200;
        transitionFlash = 0.4f;
      } else if (newMood == 4) {
        spiralAngle = 0;
        transitionFlash = 0.6f;
      } else if (newMood == 0) {
        transitionFlash = 0.3f;
      } else if (newMood == 2) {
        ovPain = true;
        ovPainEnd = n + 400;
        transitionFlash = 1.0f;
      }
    }
    smoothModeChange(newMood, 400);
    Serial.println("Mood->" + String(newMood) + " (" + String(reason) +
                   ") aff=" + String(affectionScore));
    nextMoodChangeAt = now + random(180000, 480000);
    goto mood_done;
  }
mood_done:;

  // Weather comes from Gadgetbridge — no HTTP needed

  // ================= BLE STABILITY ENGINE =================

  // ---- DEFERRED HANDSHAKE (moved out of callback to avoid blocking BLE
  // thread) ----
  if (bleJustConnected && bleConnected) {
    if (bleHandshakeStep == 0 && now - bleConnectTime >= BLE_HANDSHAKE_DELAY) {
      // Step 1: Send status handshake
      bleHandshakeStep = 1;
      bleHandshakeNextAt = now;
    }
    if (bleHandshakeStep >= 1 && now >= bleHandshakeNextAt && pTxChar) {
      switch (bleHandshakeStep) {
      case 1: {
        String hello = "\x10{\"t\":\"status\",\"bat\":100,\"volt\":5.0,"
                       "\"charging\":true}\n";
        pTxChar->setValue(hello.c_str());
        pTxChar->notify();
        bleHandshakeStep = 2;
        bleHandshakeNextAt = now + 100;
        break;
      }
      case 2: {
        String timeReq = "\x10{\"t\":\"act\",\"hrm\":0,\"stp\":0}\n";
        pTxChar->setValue(timeReq.c_str());
        pTxChar->notify();
        bleHandshakeStep = 3;
        bleHandshakeNextAt = now + 100;
        break;
      }
      case 3: {
        String weatherReq = "\x10{\"t\":\"force\",\"id\":\"weather\"}\n";
        pTxChar->setValue(weatherReq.c_str());
        pTxChar->notify();
        bleHandshakeStep = 4;
        bleHandshakeNextAt = now + 100;
        break;
      }
      case 4: {
        String weatherReq2 = "\x10{\"t\":\"info\",\"id\":\"weather\"}\n";
        pTxChar->setValue(weatherReq2.c_str());
        pTxChar->notify();
        lastWeatherRequest = now;
        bleHandshakeStep = 5;
        bleHandshakeNextAt = now + 200;
        break;
      }
      case 5: {
        // Request relaxed connection parameters from Android
        // This tells Android to use longer intervals + higher timeout
        // = much less likely to drop the connection to save battery
        if (blePeerAddrValid) {
          esp_ble_conn_update_params_t connParams;
          memset(&connParams, 0, sizeof(connParams));
          memcpy(connParams.bda, blePeerAddr, sizeof(esp_bd_addr_t));
          connParams.min_int = 40;  // 40 * 1.25ms = 50ms
          connParams.max_int = 80;  // 80 * 1.25ms = 100ms
          connParams.latency = 4;   // can skip 4 intervals (saves power)
          connParams.timeout = 800; // 800 * 10ms = 8s supervision timeout
          esp_err_t err = esp_ble_gap_update_conn_params(&connParams);
          if (err == ESP_OK) {
            Serial.println("BLE: Conn params updated (50-100ms, lat=4, to=8s)");
          } else {
            Serial.println("BLE: Conn param update failed: " + String(err));
          }
          bleLastConnParamReq = now;
        } else {
          Serial.println("BLE: No peer addr, skipping conn param update");
        }
        Serial.println("BLE: Handshake complete (#" +
                       String(bleReconnectCount) + ")");
        bleJustConnected = false;
        bleHandshakeStep = 0;
        break;
      }
      }
    }
  }

  // ---- BLE KEEPALIVE + ZOMBIE DETECTION ----
  if (bleConnected && !bleJustConnected) {
    // Send keepalive every 10s so Gadgetbridge doesn't think we died
    if (now - lastBLEKeepalive > BLE_KEEPALIVE_MS) {
      lastBLEKeepalive = now;
      if (pTxChar) {
        String ping = "\x10{\"t\":\"status\",\"bat\":100}\n";
        pTxChar->setValue(ping.c_str());
        // notify() returns void in ESP32 Arduino 3.x — use try/catch for errors
        try {
          pTxChar->notify();
          bleConsecutiveFails = 0; // success — reset counter
        } catch (...) {
          bleConsecutiveFails++;
          Serial.println("BLE: notify failed (" + String(bleConsecutiveFails) +
                         "/" + String(BLE_MAX_NOTIFY_FAILS) + ")");
          if (bleConsecutiveFails >= BLE_MAX_NOTIFY_FAILS) {
            Serial.println(
                "BLE: Too many notify failures — forcing disconnect");
            pServer->disconnect(pServer->getConnId());
            bleConnected = false;
            bleDisconnectedSince = now;
            lastBLEActivity = 0;
            onBLEDisconnectCleanup();
          }
        }

        // Periodically re-request weather (every 30 min)
        if (now - lastWeatherRequest > WEATHER_REQUEST_INTERVAL) {
          lastWeatherRequest = now;
          String weatherReq = "\x10{\"t\":\"force\",\"id\":\"weather\"}\n";
          pTxChar->setValue(weatherReq.c_str());
          pTxChar->notify();
          Serial.println("BLE: Periodic weather re-request");
        }
      }
    }

    // ---- ZOMBIE CONNECTION DETECTION ----
    // If we haven't received ANY data from the phone in 90s, the connection is
    // dead. Gadgetbridge sends time sync, weather updates, notifications —
    // silence means zombie.
    if (lastBLEActivity > 0 && now - lastBLEActivity > BLE_STALE_TIMEOUT) {
      Serial.println("BLE STALE: No data for " +
                     String((now - lastBLEActivity) / 1000) +
                     "s — forcing disconnect");
      pServer->disconnect(pServer->getConnId());
      bleConnected = false;
      bleJustConnected = false;
      bleHandshakeStep = 0;
      blePeerAddrValid = false;
      bleBuffer[0] = '\0';
      bleDisconnectedSince = now;
      lastBLEActivity = 0;
      // Re-advertise immediately — no delay() to avoid blocking
      BLEDevice::startAdvertising();
      onBLEDisconnectCleanup();
      Serial.println("BLE: Forced disconnect, re-advertising");
    }
  }

  // ---- DISCONNECTED: RE-ADVERTISE + WATCHDOG RESTART ----
  if (!bleConnected) {
    // Safety net: if disconnected 5s+ but phone states still active, force
    // clean
    if (bleDisconnectedSince > 0 && now - bleDisconnectedSince > 5000) {
      if (isIncomingCall || callActive || isDancing || musicPlaying) {
        onBLEDisconnectCleanup();
        Serial.println("BLE safety: cleared stale phone states");
      }
    }

    // Re-advertise periodically in case it stopped
    if (now - lastBLECheck > BLE_CHECK_MS) {
      lastBLECheck = now;
      if (pServer && pServer->getConnectedCount() == 0) {
        BLEDevice::startAdvertising();
      }
    }

    // ---- BLE RECONNECT WATCHDOG ----
    // If disconnected for 5+ minutes, aggressively re-advertise.
    // NOTE: Full BLE deinit/reinit removed — it causes crashes on ESP32
    // Arduino 3.x due to use-after-free in the BLE stack internals.
    // Simple re-advertising is safe and sufficient for reconnection.
    if (bleDisconnectedSince > 0 &&
        now - bleDisconnectedSince > BLE_WATCHDOG_RESTART_MS) {
      bleDisconnectedSince = now; // reset to avoid rapid-fire
      if (pServer && pServer->getConnectedCount() == 0) {
        BLEDevice::startAdvertising();
        Serial.println("BLE WATCHDOG: Re-advertising (safe mode)");
      }
    }
  }

  // ---- UPDATE OVERLAYS ----
  updateOverlays(now);
  updateBeat(now);
  if (isDancing)
    updateDance(now);

  // ---- UPDATE PERSONALITY SYSTEMS ----
  updateEmotionalMemory(now);
  updateQuirks(now);
  updateAttentionSeeking(now);
  updateStartleChain(now);
  updateForgiveness(now);
  updatePurring(now);
  updateJealousy(now);
  updateWakeUpDrama(now);
  updateHydration(now);
  updatePosture(now);
  updateDramaticDeath(now);

  // Expire popups
  if (showNotif && now > notifEnd)
    showNotif = false;
  if (showMusicPopup && now > musicPopupEnd)
    showMusicPopup = false;
  if (showMuteOverlay && now > muteOverlayEnd)
    showMuteOverlay = false;
  if (showMusic && now > musicEnd) {
    showMusic = false;
    if (!musicPlaying)
      stopDancing();
  }
  // Keep bot awake while dancing — prevent sleep timer
  if (isDancing)
    lastInteractionTime = now;
  if ((isIncomingCall || callActive) && !callEnded) {
    // After 20s ringing with no accept/end → missed call, dismiss
    if (isIncomingCall && now - callRingStart > 20000) {
      isIncomingCall = false;
      callActive = false;
      callEnded = true;
      callWasAnswered = false; // missed!
      callReactEnd = now + 2000;
      buzzerStop();
      buzzerPlay(SND_CRY); // sad that no one picked up
      triggerModeChange(0);
      Serial.println("Call missed — rang for 20s with no answer");
    }
    // Safety: auto-dismiss active call after 10 minutes (in case end packet
    // never arrives)
    if (callActive && now - callEnd > 600000) {
      callActive = false;
      callEnded = true;
      callWasAnswered = true;
      callReactEnd = now + 2000;
      triggerModeChange(0);
      Serial.println("Call auto-dismissed after 10min");
    }
  }
  // Post-call reaction expires
  if (callEnded && now > callReactEnd) {
    callEnded = false;
    triggerModeChange(0);
  }

  // ---- UPDATE BASE ANIMATIONS ----
  if (!isSleeping) {
    if (currentMode == 0)
      updateAliveAnimations();
    if (currentMode == 1)
      updateHeartbeat();
    if (currentMode == 4)
      spiralAngle += 0.3;
    if (currentMode == 5) {
      bounceY = (float)sin((float)now * 0.015f) * 3.0f;
      exciteScale = 1.0f + (float)fabs((float)sin((float)now * 0.02f)) * 0.3f;
    }
    if (currentMode == 6)
      scaredShake = (float)sin(now * 0.04) * 2.5;
    if (currentMode == 7) {
      if (now - lastBlinkTime > 4000) {
        isBlinking = true;
        if (now - lastBlinkTime > 4150) {
          isBlinking = false;
          lastBlinkTime = now;
        }
      }
    }
    if (isLaughing)
      laughShake = (float)sin(now * 0.05) * 3.0;
    if (ovCryLaugh)
      cryLaughShake = (float)sin(now * 0.06) * 2.5;
    if (isPuppySquint && now > squintEndTime)
      isPuppySquint = false;
    if (isRejected && now > rejectEndTime)
      isRejected = false;
    if (isWinking && now > winkEndTime)
      isWinking = false;
    if (isSurprised && now > surpriseEndTime)
      isSurprised = false;
    if (isLaughing && now > laughEndTime) {
      isLaughing = false;
      laughShake = 0;
    }
    if (isSuspicious && now > suspiciousEndTime)
      isSuspicious = false;
    if (isSmugWink && now > smugWinkEndTime)
      isSmugWink = false;
  }

  // ---- RENDER ----
  // Priority: notif > music popup > dancing > face > clock
  // Music and notifications ALWAYS wake from sleep/clock mode
  display.clearDisplay();

  // DEATH SCENE — highest priority, takes over everything
  if (isDying) {
    drawDeathScene(now);
    goto render_done;
  }

  if (showNotif) {
    // Notification always shows — even over sleep/clock
    if (isSleeping) {
      isSleeping = false;
      isDriftingOff = false;
      targetEyeOpenFactor = 1.0f;
      currentMode = 0;
    }
    drawNotifOverlay();
  } else if (isIncomingCall || callActive) {
    // Call screen shows for both ringing and active call
    if (isSleeping) {
      isSleeping = false;
      isDriftingOff = false;
      targetEyeOpenFactor = 1.0f;
      currentMode = 0;
      hasMidYawned = false;
      hasFinalYawned = false;
    }
    if (isDancing)
      stopDancing();
    drawCallScreen();
  } else if (showMusicPopup) {
    // Music popup always shows — wakes from sleep
    if (isSleeping) {
      isSleeping = false;
      isDriftingOff = false;
      targetEyeOpenFactor = 1.0f;
      currentMode = 0;
      hasMidYawned = false;
      hasFinalYawned = false;
    }
    drawMusicPopup();
  } else if (isDancing) {
    // Dancing always shows — wakes from sleep
    if (isSleeping) {
      isSleeping = false;
      isDriftingOff = false;
      targetEyeOpenFactor = 1.0f;
      currentMode = 0;
      hasMidYawned = false;
      hasFinalYawned = false;
    }
    drawDanceFace();
  } else if (isSleeping)
    showClock();
  else if (currentMode == 8)
    showClock();
  else if (currentMode == 9)
    drawWeatherFace();
  else {
    drawEyes();
    drawMouth();
    drawOverlayExtras();
    drawBeatOverlay();
    drawQuirkOverlay();
    drawPurrHearts(now);
    // Hydration & posture overlays
    if (isThirsty)
      drawHydrationOverlay(now);
    if (isPostureReminder)
      drawPostureOverlay(now);
    if (hydrationCelebration && now < celebrationEnd) {
      // Water celebration — floating droplets
      display.setTextSize(1);
      centerText("HYDRATED!", 2, 1);
      for (int i = 0; i < 5; i++) {
        int dx = (int)(sinf(now * 0.01f + i * 1.2f) * 20);
        int dy = (now / 50 + i * 13) % 64;
        drawSweatDrop(64 + dx, 64 - dy);
      }
    }
    if (postureAcked && now < postureAckEnd) {
      // Posture acknowledged — thumbs up text
      display.setTextSize(1);
      centerText("Good posture!", 2, 1);
    }
    // Amnesia overlay — confused face after death reboot
    if (hasAmnesia) {
      if (now < amnesiaEnd) {
        display.setTextSize(1);
        if ((now / 500) % 2 == 0)
          centerText("...who am I?", 2, 1);
        else
          centerText("...where am I?", 2, 1);
        // Question marks floating around
        for (int i = 0; i < 3; i++) {
          int qx = 20 + i * 40 + (int)(sinf(now * 0.005f + i) * 8);
          int qy = 5 + (int)(sinf(now * 0.008f + i * 2) * 4);
          display.setCursor(qx, qy);
          display.print("?");
        }
      } else {
        hasAmnesia = false; // amnesia is over!
        Serial.println("Amnesia cleared. Bot is back to normal.");
      }
    }
    // ---- MINI TIME in angry mode corner — so user can still see the clock
    // ----
    if (currentMode == 2 && timeReady) {
      struct tm mt;
      if (getLocalTime(&mt)) {
        char miniTime[6];
        int hr12m = mt.tm_hour % 12;
        if (hr12m == 0)
          hr12m = 12;
        sprintf(miniTime, "%d:%02d", hr12m, mt.tm_min);
        display.setTextSize(1);
        int mtw = strlen(miniTime) * 6;
        display.setCursor(127 - mtw, 0);
        display.print(miniTime);
      }
    }
  }
  // ---- TRANSITION FLASH — smooth white-fade between mode changes ----
  if (transitionFlash > 0.05f) {
    int flashAlpha = (int)(transitionFlash * 6); // 1-6 pixel border layers
    for (int i = 0; i < min(flashAlpha, 3); i++)
      display.drawRect(i, i, 128 - 2 * i, 64 - 2 * i, WHITE);
  }
  // ---- SCREEN TRANSITION (iris wipe between face/clock/weather) ----
  if (screenTransActive) {
    unsigned long elapsed = now - screenTransStart;
    float t = (float)elapsed / (float)SCREEN_TRANS_MS;
    if (t > 1.0f)
      t = 1.0f;

    if (screenTransClosing) {
      // Closing: fill screen from edges inward with black circle mask
      // Draw shrinking white circle, everything outside is black
      int maxR = 72;                    // diagonal of 128x64 / 2
      int r = (int)(maxR * (1.0f - t)); // shrinks to 0
      // Fill whole screen black, then cut out the circle
      // We'll use horizontal lines to create the iris mask
      for (int y = 0; y < 64; y++) {
        int dy = y - 32;
        if (dy * dy <= r * r) {
          int dx = (int)sqrtf((float)(r * r - dy * dy));
          int x1 = max(0, 64 - dx);
          int x2 = min(127, 64 + dx);
          // Black out LEFT side
          if (x1 > 0)
            display.fillRect(0, y, x1, 1, BLACK);
          // Black out RIGHT side
          if (x2 < 127)
            display.fillRect(x2 + 1, y, 128 - x2 - 1, 1, BLACK);
        } else {
          // This row is fully outside the circle
          display.fillRect(0, y, 128, 1, BLACK);
        }
      }

      if (t >= 1.0f) {
        // Midpoint: switch mode, start opening
        triggerModeChange(screenTransTargetMode);
        screenTransClosing = false;
        screenTransStart = millis();
      }
    } else {
      // Opening: circle expands outward revealing new content
      int maxR = 72;
      int r = (int)(maxR * t); // grows from 0 to full
      for (int y = 0; y < 64; y++) {
        int dy = y - 32;
        if (dy * dy <= r * r) {
          int dx = (int)sqrtf((float)(r * r - dy * dy));
          int x1 = max(0, 64 - dx);
          int x2 = min(127, 64 + dx);
          if (x1 > 0)
            display.fillRect(0, y, x1, 1, BLACK);
          if (x2 < 127)
            display.fillRect(x2 + 1, y, 128 - x2 - 1, 1, BLACK);
        } else {
          display.fillRect(0, y, 128, 1, BLACK);
        }
      }

      if (t >= 1.0f) {
        screenTransActive = false; // done!
      }
    }
  }

  // ---- MUTE TOGGLE OVERLAY — brief confirmation on sound toggle ----
  if (showMuteOverlay) {
    // Centered rounded rect with "SOUND ON" or "SOUND OFF" + speaker icon
    display.fillRoundRect(14, 18, 100, 28, 6, BLACK);
    display.drawRoundRect(14, 18, 100, 28, 6, WHITE);
    display.drawRoundRect(15, 19, 98, 26, 5, WHITE);
    drawSpeakerIcon(22, 26, !soundEnabled);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    if (soundEnabled) {
      display.setCursor(40, 28);
      display.print("SOUND ON");
    } else {
      display.setCursor(40, 28);
      display.print("SOUND OFF");
    }
  }

render_done:;
  display.display();
  delay(20);
}

// ================= OVERLAY UPDATE =================
void updateOverlays(unsigned long now) {
  // Expire overlays
  if (ovNervous && now > ovNervousEnd) {
    ovNervous = false;
    nervShake = 0;
  }
  if (ovSleepy && now > ovSleepyEnd) {
    ovSleepy = false;
    sleepDroop = 0;
  }
  if (ovShocked && now > ovShockedEnd)
    ovShocked = false;
  if (ovDisgusted && now > ovDisgustedEnd)
    ovDisgusted = false;
  if (ovEmbarrassed && now > ovEmbarrassedEnd)
    ovEmbarrassed = false;
  if (ovThinking && now > ovThinkingEnd)
    ovThinking = false;
  if (ovHyper && now > ovHyperEnd) {
    ovHyper = false;
    hyperPupilX = 0;
    hyperPupilY = 0;
  }
  if (ovPain && now > ovPainEnd)
    ovPain = false;
  if (ovMelting && now > ovMeltingEnd) {
    ovMelting = false;
    meltDroop = 0;
  }
  if (ovCryLaugh && now > ovCryLaughEnd) {
    ovCryLaugh = false;
    cryLaughShake = 0;
  }

  // Animate active overlays
  if (ovNervous)
    nervShake = (float)sin(now * 0.06) * 3.5;
  if (ovSleepy)
    sleepDroop = min(sleepDroop + 0.03f, 1.0f);
  if (ovEmbarrassed)
    blushPulse = 0.6 + 0.4f * (float)sin(now * 0.005);
  if (ovMelting)
    meltDroop = min(meltDroop + 0.04f, 14.0f);
  if (ovHyper) {
    if (now > nextHyperDart) {
      hyperTargetX = random(-10, 11);
      hyperTargetY = random(-5, 6);
      nextHyperDart = now + random(60, 200);
    }
    hyperPupilX += (hyperTargetX - hyperPupilX) * 0.5;
    hyperPupilY += (hyperTargetY - hyperPupilY) * 0.5;
  }
}

// ================= DRAW EYES =================
void drawEyes() {
  unsigned long now = millis();

  // --- OVERLAY: PAIN — X eyes override everything ---
  if (ovPain) {
    int ey = EYE_Y + EYE_H / 2;
    int xSz = BASE_EYE_W / 2; // scale X size to eye width
    int ySz = EYE_H / 4;      // scale Y size to eye height
    int shk = (int)((float)sin((float)now * 0.08f) * 2.0f);
    int lcx = EYE_X_L + BASE_EYE_W / 2 + shk;
    int rcx = EYE_X_R + BASE_EYE_W / 2 + shk;
    display.drawLine(lcx - xSz, ey - ySz, lcx + xSz, ey + ySz, WHITE);
    display.drawLine(lcx - xSz, ey + ySz, lcx + xSz, ey - ySz, WHITE);
    display.drawLine(rcx - xSz, ey - ySz, rcx + xSz, ey + ySz, WHITE);
    display.drawLine(rcx - xSz, ey + ySz, rcx + xSz, ey - ySz, WHITE);
    return;
  }

  // --- OVERLAY: SHOCKED — maxed wide eyes override ---
  if (ovShocked) {
    int shk = (int)((float)sin((float)now * 0.03f) * 1.0f);
    display.fillRoundRect(EYE_X_L + shk, EYE_Y, BASE_EYE_W + 4, EYE_H,
                          EYE_RADIUS, WHITE);
    display.fillRoundRect(EYE_X_R + shk, EYE_Y, BASE_EYE_W + 4, EYE_H,
                          EYE_RADIUS, WHITE);
    // Tiny pinpoint pupils in corners (proportional)
    int pp = max(4, BASE_EYE_W / 6);
    display.fillRect(EYE_X_L + shk + 4, EYE_Y + 4, pp, pp, BLACK);
    display.fillRect(EYE_X_R + shk + 4, EYE_Y + 4, pp, pp, BLACK);
    return;
  }

  // --- OVERLAY: CRY LAUGH — normal eyes but with shake ---
  if (ovCryLaugh) {
    int shk = (int)cryLaughShake;
    int clH = max(6, EYE_H * 3 / 5); // squinted but proportional
    int clY = EYE_Y + (EYE_H - clH) / 2;
    display.fillRoundRect(EYE_X_L + shk, clY, BASE_EYE_W, clH, EYE_RADIUS,
                          WHITE);
    display.fillRoundRect(EYE_X_R + shk, clY, BASE_EYE_W, clH, EYE_RADIUS,
                          WHITE);
    // Tears streaming down
    for (int i = 0; i < 6; i++) {
      int d = (int)(tearY + i * 8) % 28;
      int cy = EYE_Y + EYE_H - 10 + d;
      if (d < 26) {
        drawSingleTear(EYE_X_L + BASE_EYE_W / 2 + shk, cy);
        drawSingleTear(EYE_X_R + BASE_EYE_W / 2 - shk, cy);
      }
    }
    tearY += 0.8;
    return;
  }

  // --- BASE MODE EYES (then overlays applied on top) ---

  if (currentMode == 0) {
    int wL = (int)(currentEyeW_L + 0.5), wR = (int)(currentEyeW_R + 0.5);
    float h = EYE_H;
    if (currentYawnFactor > 0)
      h = map((int)(currentYawnFactor * 100), 0, 100, EYE_H, 6);
    h *= currentEyeOpenFactor;
    if (isPuppySquint)
      h = 10;
    if (isBeingPetted)
      h = 4;
    if (isBlinking && !isDriftingOff && !isBeingPetted)
      h = 4;
    if (h < 2 && currentEyeOpenFactor > 0.1)
      h = 2;
    int fH = (int)h;
    int elx = (EYE_X_L + BASE_EYE_W / 2) - wL / 2,
        erx = (EYE_X_R + BASE_EYE_W / 2) - wR / 2;
    int ely = EYE_Y + (EYE_H - fH) / 2;

    // OVERLAY: SLEEPY — droop lids on top of alive eyes
    if (ovSleepy) {
      display.fillRoundRect(elx, ely, wL, fH, EYE_RADIUS, WHITE);
      display.fillRoundRect(erx, ely, wR, fH, EYE_RADIUS, WHITE);
      int lidH = (int)(fH * sleepDroop * 0.7);
      if (lidH > 0) {
        display.fillRect(elx, ely, wL, lidH, BLACK);
        display.fillRect(erx, ely, wR, lidH, BLACK);
      }
      return;
    }
    // OVERLAY: HYPER — draw eyes with darting pupils
    if (ovHyper) {
      display.fillRoundRect(elx, ely, wL, fH, EYE_RADIUS, WHITE);
      display.fillRoundRect(erx, ely, wR, fH, EYE_RADIUS, WHITE);
      int px = (int)hyperPupilX, py = (int)hyperPupilY;
      int pcx = elx + wL / 2 + px, pcy = ely + fH / 2 + py;
      int prx = erx + wR / 2 + px, pry = ely + fH / 2 + py;
      int pupR = max(3, BASE_EYE_W / 6); // proportional pupil
      display.fillCircle(pcx, pcy, pupR, BLACK);
      display.fillCircle(prx, pry, pupR, BLACK);
      return;
    }
    // OVERLAY: THINKING — right eye normal, left eye squinted + brow up
    if (ovThinking) {
      // Left eye squint
      display.fillRect(elx, ely + fH / 2 - 2, wL, 4, WHITE);
      // Right eye normal
      display.fillRoundRect(erx, ely, wR, fH, EYE_RADIUS, WHITE);
      // Raised brow above right eye (clamped to screen)
      int browY = max(0, ely - 6);
      display.fillRect(erx, browY, wR, 3, WHITE);
      // Look up-left — pupil offset (proportional to eye size)
      int pW = max(2, wR * 2 / 3), pH = max(2, fH * 2 / 3);
      display.fillRect(erx + 3, ely + 2, pW, pH, BLACK);
      return;
    }
    // OVERLAY: EMBARRASSED — both eyes look away (right)
    if (ovEmbarrassed) {
      display.fillRoundRect(elx, ely, wL, fH, EYE_RADIUS, WHITE);
      display.fillRoundRect(erx, ely, wR, fH, EYE_RADIUS, WHITE);
      // Pupils shifted hard right (proportional)
      int pupW = max(4, wL / 4), pupH = max(6, fH - fH / 4);
      display.fillRoundRect(elx + wL - pupW - 2, ely + 3, pupW, pupH, 3, BLACK);
      display.fillRoundRect(erx + wR - pupW - 2, ely + 3, pupW, pupH, 3, BLACK);
      return;
    }
    // OVERLAY: DISGUSTED — left eye squint, right normal
    if (ovDisgusted) {
      // Left = thin squint
      display.fillRect(elx, ely + fH / 2 - 3, wL, 5, WHITE);
      // Right = normal
      display.fillRoundRect(erx, ely, wR, fH, EYE_RADIUS, WHITE);
      return;
    }

    if (fH <= 6) {
      display.fillRect(elx, ely, wL, fH, WHITE);
      display.fillRect(erx, ely, wR, fH, WHITE);
    } else {
      display.fillRoundRect(elx, ely, wL, fH, EYE_RADIUS, WHITE);
      display.fillRoundRect(erx, ely, wR, fH, EYE_RADIUS, WHITE);
    }
    return;
  }

  if (currentMode == 1) {
    if (isWinking) {
      drawHeart(EYE_X_L + BASE_EYE_W / 2, EYE_Y + EYE_H / 2, heartScale);
      display.fillRect(EYE_X_R, EYE_Y + EYE_H / 2, BASE_EYE_W, 4, WHITE);
    } else {
      drawHeart(EYE_X_L + BASE_EYE_W / 2, EYE_Y + EYE_H / 2, heartScale);
      drawHeart(EYE_X_R + BASE_EYE_W / 2, EYE_Y + EYE_H / 2, heartScale);
    }
    return;
  }
  if (currentMode == 2) {
    int shX = 0, shY = 0, ebH = 4, aOff = isRejected ? -15 : 0;
    if (isFurious) {
      shX = random(-2, 3);
      shY = random(-2, 3);
      ebH = 12;
      drawAngryFire(64 + shX, max(18, EYE_Y + 16) + shY);
    }
    if (isBeingLoved)
      ebH = 2;
    int angryOff = EYE_H * 35 / 100; // proportional brow offset (~35%)
    int angryH = EYE_H - angryOff;
    display.fillRoundRect(EYE_X_L + shX + aOff, EYE_Y + angryOff + shY,
                          BASE_EYE_W, angryH, 6, WHITE);
    display.fillRect(EYE_X_L + shX + aOff, EYE_Y + angryOff + shY, BASE_EYE_W,
                     ebH, BLACK);
    display.fillRoundRect(EYE_X_R + shX + aOff, EYE_Y + angryOff + shY,
                          BASE_EYE_W, angryH, 6, WHITE);
    display.fillRect(EYE_X_R + shX + aOff, EYE_Y + angryOff + shY, BASE_EYE_W,
                     ebH, BLACK);
    return;
  }
  if (currentMode == 3) {
    if (isCryingHard) {
      int ey = EYE_Y + EYE_H / 2;
      int xSz = BASE_EYE_W / 2, ySz = EYE_H / 6;
      int lcx = EYE_X_L + BASE_EYE_W / 2, rcx = EYE_X_R + BASE_EYE_W / 2;
      display.drawLine(lcx - xSz, ey - ySz, lcx + xSz, ey + ySz, WHITE);
      display.drawLine(lcx - xSz, ey + ySz, lcx + xSz, ey - ySz, WHITE);
      display.drawLine(rcx - xSz, ey - ySz, rcx + xSz, ey + ySz, WHITE);
      display.drawLine(rcx - xSz, ey + ySz, rcx + xSz, ey - ySz, WHITE);
    } else {
      int sadEyeY = EYE_Y + EYE_H / 2;
      display.fillRect(EYE_X_L, sadEyeY, BASE_EYE_W, 5, WHITE);
      display.fillRect(EYE_X_R, sadEyeY, BASE_EYE_W, 5, WHITE);
    }
    // Tears — faster if crying hard
    for (int i = 0; i < (isCryingHard ? 16 : 8); i++) {
      int d = (int)(tearY + i * (isCryingHard ? 4 : 8)) % 28;
      int cy = EYE_Y + EYE_H - 8 + d;
      int w = (int)((float)sin((tearY + i) * 0.4f) * 2.0f);
      if (d < 26) {
        drawSingleTear(EYE_X_L + BASE_EYE_W / 2 + w, cy);
        drawSingleTear(EYE_X_R + BASE_EYE_W / 2 - w, cy);
      }
    }
    tearY += isCryingHard ? 1.4 : 0.6;
    return;
  }
  if (currentMode == 4) {
    drawSpiral(EYE_X_L + BASE_EYE_W / 2, EYE_Y + EYE_H / 2, -1, spiralAngle);
    drawSpiral(EYE_X_R + BASE_EYE_W / 2, EYE_Y + EYE_H / 2, 1, spiralAngle);
    return;
  }
  if (currentMode == 5) {
    int by = constrain((int)bounceY, -EYE_Y, 4); // clamp so eyes stay on screen
    float sc = min(exciteScale, 1.25f); // cap scale to prevent off-screen
    int eyeH = (int)(EYE_H * 0.9 * sc);
    int eyeW = (int)(BASE_EYE_W * 1.1 * sc);
    int elx = (EYE_X_L + BASE_EYE_W / 2) - eyeW / 2,
        erx = (EYE_X_R + BASE_EYE_W / 2) - eyeW / 2;
    display.fillRoundRect(elx, EYE_Y + by + (EYE_H - eyeH) / 2, eyeW, eyeH,
                          EYE_RADIUS, WHITE);
    display.fillRoundRect(erx, EYE_Y + by + (EYE_H - eyeH) / 2, eyeW, eyeH,
                          EYE_RADIUS, WHITE);
    int so = (millis() / 300) % 2;
    drawStar(EYE_X_L + BASE_EYE_W / 2, EYE_Y + EYE_H / 2 + by, 6 + so);
    drawStar(EYE_X_R + BASE_EYE_W / 2, EYE_Y + EYE_H / 2 + by, 6 + so);
    if (isSurprised || (millis() / 500) % 2 == 0) {
      int excY = max(0, EYE_Y - 4 + (int)by); // clamp to screen top
      drawExclamation(EYE_X_L + BASE_EYE_W / 2 - 1, excY);
      drawExclamation(EYE_X_R + BASE_EYE_W / 2 - 1, excY);
    }
    return;
  }
  if (currentMode == 6) {
    int sh = (int)scaredShake;
    int eyeH = isSurprised ? EYE_H : (int)(EYE_H * 0.85);
    display.fillRoundRect(EYE_X_L + sh, EYE_Y + (EYE_H - eyeH) / 2, BASE_EYE_W,
                          eyeH, EYE_RADIUS, WHITE);
    display.fillRoundRect(EYE_X_R + sh, EYE_Y + (EYE_H - eyeH) / 2, BASE_EYE_W,
                          eyeH, EYE_RADIUS, WHITE);
    int pW = BASE_EYE_W / 3, pH = eyeH / 3;
    display.fillRect(EYE_X_L + sh + (BASE_EYE_W - pW) / 2,
                     EYE_Y + (EYE_H - eyeH) / 2 + 2, pW, pH, BLACK);
    display.fillRect(EYE_X_R + sh + (BASE_EYE_W - pW) / 2,
                     EYE_Y + (EYE_H - eyeH) / 2 + 2, pW, pH, BLACK);
    int sdx1 = constrain(EYE_X_L - 5 + sh, 0, 120);
    int sdx2 = constrain(EYE_X_R + BASE_EYE_W + 3 + sh, 0, 120);
    drawSweatDrop(sdx1, EYE_Y + 8);
    drawSweatDrop(sdx2, EYE_Y + 8);
    return;
  }
  if (currentMode == 7) {
    int eyeH = (int)(EYE_H * 0.45), ely = EYE_Y + (EYE_H - eyeH);
    if (isSmugWink) {
      display.fillRoundRect(EYE_X_L, ely, BASE_EYE_W, eyeH, 4, WHITE);
      display.fillRect(EYE_X_R, EYE_Y + EYE_H / 2, BASE_EYE_W, 4, WHITE);
    } else if (isSuspicious) {
      display.fillRoundRect(EYE_X_L, ely + 4, BASE_EYE_W, eyeH - 4, 3, WHITE);
      display.fillRoundRect(EYE_X_R, ely, BASE_EYE_W, eyeH, 4, WHITE);
    } else {
      display.fillRoundRect(EYE_X_L, ely, BASE_EYE_W, eyeH, 4, WHITE);
      display.fillRoundRect(EYE_X_R, ely, BASE_EYE_W, eyeH, 4, WHITE);
    }
    if (isBlinking) {
      display.fillRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, BLACK);
      display.fillRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, BLACK);
    }
    return;
  }

  // OVERLAY: MELTING — droop everything downward
  // (applied after base mode draws eyes, melts them)
  if (ovMelting) {
    int d = min((int)meltDroop, 63 - EYE_Y - EYE_H);
    if (d < 0)
      d = 0;
    // Left eye drip
    display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H + d, EYE_RADIUS,
                          WHITE);
    display.fillRect(EYE_X_L, EYE_Y, BASE_EYE_W, 8,
                     BLACK); // top erased = drooping
    // Right eye drip
    display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H + d, EYE_RADIUS,
                          WHITE);
    display.fillRect(EYE_X_R, EYE_Y, BASE_EYE_W, 8, BLACK);
  }
}

// ================= DRAW MOUTH =================
void drawMouth() {
  unsigned long now = millis();
  int cx = 64;

  // --- OVERLAY: PAIN — zigzag mouth ---
  if (ovPain) {
    int shk = (int)(sin(now * 0.08) * 2);
    for (int x = -14; x < 14; x++) {
      int yo = (int)((float)sin(x * 1.1f) * 6.0f) + 10;
      display.fillCircle(cx + x + shk, MOUTH_Y + yo, 1, WHITE);
    }
    return;
  }

  // --- OVERLAY: SHOCKED — dropped jaw O mouth ---
  if (ovShocked) {
    display.fillRoundRect(cx - 10, MOUTH_Y + 2, 20, min(20, 63 - MOUTH_Y - 2),
                          6, WHITE);
    display.fillRoundRect(cx - 8, MOUTH_Y + 4, 16, min(16, 63 - MOUTH_Y - 4), 4,
                          BLACK);
    return;
  }

  // --- OVERLAY: CRY LAUGH — big open laugh ---
  if (ovCryLaugh) {
    int shk = (int)cryLaughShake;
    display.fillRoundRect(cx - 16 + shk, MOUTH_Y, 32, 18, 8, WHITE);
    display.fillRoundRect(cx - 14 + shk, MOUTH_Y + 2, 28, 14, 6, BLACK);
    for (int i = 0; i < 4; i++)
      display.fillRect(cx - 12 + shk + i * 7, MOUTH_Y + 2, 5, 6, WHITE);
    return;
  }

  // --- OVERLAY: MELTING — drooping smile ---
  if (ovMelting) {
    int d = (int)(meltDroop * 0.5);
    display.fillCircle(cx, MOUTH_Y + 5 + d, 9, WHITE);
    display.fillCircle(cx, MOUTH_Y + 1 + d, 9, BLACK);
    // Drip from chin
    {
      int dripH = min((int)meltDroop, 63 - (MOUTH_Y + 12 + d));
      if (dripH > 0)
        display.fillRect(cx - 2, MOUTH_Y + 12 + d, 4, dripH, WHITE);
    }
    return;
  }

  // --- OVERLAY: DISGUSTED — curled lip sneer ---
  if (ovDisgusted) {
    // Asymmetric mouth — one side up, one side down
    display.fillCircle(cx - 8, MOUTH_Y + 8, 6, WHITE);
    display.fillCircle(cx - 9, MOUTH_Y + 5, 6, BLACK);
    display.fillCircle(cx + 8, MOUTH_Y + 12, 6, WHITE);
    display.fillCircle(cx + 9, MOUTH_Y + 14, 6, BLACK);
    return;
  }

  // --- OVERLAY: EMBARRASSED — small tight line ---
  if (ovEmbarrassed) {
    display.fillRect(cx - 6, MOUTH_Y + 12, 12, 3, WHITE);
    return;
  }

  // --- OVERLAY: THINKING — small pursed mouth ---
  if (ovThinking) {
    display.fillCircle(cx + 6, MOUTH_Y + 8, 5, WHITE);
    display.fillCircle(cx + 4, MOUTH_Y + 6, 5, BLACK);
    return;
  }

  // --- OVERLAY: NERVOUS — wavy trembling mouth ---
  if (ovNervous) {
    int shk = (int)nervShake;
    for (int x = -12; x < 12; x++) {
      int yo = (int)((float)sin(x * 0.8f + (float)now * 0.01f) * 3.0f) + 10;
      display.fillCircle(cx + x + shk, MOUTH_Y + yo, 1, WHITE);
    }
    return;
  }

  // --- OVERLAY: SLEEPY — small droopy smile ---
  if (ovSleepy) {
    int d = (int)(sleepDroop * 4);
    display.fillCircle(cx, MOUTH_Y + 5 + d, 7, WHITE);
    display.fillCircle(cx, MOUTH_Y + 2 + d, 7, BLACK);
    return;
  }

  // --- OVERLAY: HYPER — wide grin ---
  if (ovHyper) {
    display.fillRoundRect(cx - 18, MOUTH_Y + 1, 36, 18, 8, WHITE);
    display.fillRoundRect(cx - 16, MOUTH_Y + 3, 32, 14, 6, BLACK);
    for (int i = 0; i < 5; i++)
      display.fillRect(cx - 15 + i * 7, MOUTH_Y + 3, 5, 7, WHITE);
    return;
  }

  // --- BASE MODE MOUTHS ---
  if (currentMode == 0) {
    int x = cx + (int)(currentMouthX + 0.5);
    if (isBeingPetted) {
      display.fillCircle(x, MOUTH_Y + 2, 12, WHITE);
      display.fillCircle(x, MOUTH_Y - 2, 12, BLACK);
      return;
    }
    if (currentYawnFactor > 0.05) {
      int yW = 16 + (int)(currentYawnFactor * 4),
          yH = (int)(currentYawnFactor * 18);
      int yTop = MOUTH_Y + 5 - yH / 2;
      int yBot = min(yH + 5, 63 - yTop); // clamp to screen bottom
      display.fillRoundRect(x - yW / 2, yTop, yW, yBot, 5, WHITE);
      display.fillRoundRect(x - yW / 2 + 2, yTop + 2, yW - 4, max(1, yBot - 4),
                            3, BLACK);
    } else {
      display.fillCircle(x, MOUTH_Y + 5, 9, WHITE);
      display.fillCircle(x, MOUTH_Y + 1, 9, BLACK);
    }
    return;
  }
  if (currentMode == 1) {
    display.fillCircle(cx, MOUTH_Y + 5, 9, WHITE);
    display.fillCircle(cx, MOUTH_Y + 1, 9, BLACK);
    return;
  }
  if (currentMode == 2) {
    int shX = isFurious ? random(-2, 3) : 0, aOff = isRejected ? -15 : 0;
    if (isBeingLoved) {
      display.fillCircle(cx + shX + aOff, MOUTH_Y + 5, 9, WHITE);
      display.fillCircle(cx + shX + aOff, MOUTH_Y + 1, 9, BLACK);
    } else
      display.fillRoundRect(cx - 12 + shX + aOff, MOUTH_Y + 10, 24, 4, 1,
                            WHITE);
    return;
  }
  if (currentMode == 3) {
    if (isComforted)
      display.fillRect(cx - 8, MOUTH_Y + 14, 16, 3, WHITE);
    else if (isCryingHard) {
      display.fillRoundRect(cx - 14, MOUTH_Y + 4, 28, min(16, 63 - MOUTH_Y - 4),
                            6, WHITE);
      display.fillRoundRect(cx - 12, MOUTH_Y + 6, 24, min(12, 63 - MOUTH_Y - 6),
                            4, BLACK);
    } else {
      display.fillCircle(cx, min(MOUTH_Y + 10, 56), 7, WHITE);
      display.fillCircle(cx, min(MOUTH_Y + 14, 60), 7, BLACK);
    }
    return;
  }
  if (currentMode == 4) {
    for (int x = -14; x < 14; x++) {
      int yo = (int)((float)sin(x * 0.6f) * 3.0f);
      display.fillCircle(cx + x, MOUTH_Y + 10 + yo, 1, WHITE);
    }
    return;
  }
  if (currentMode == 5) {
    int by = (int)bounceY;
    int mouthTop = MOUTH_Y + 2 + by;
    if (isLaughing) {
      int mH = min(18, max(2, 63 - mouthTop));
      display.fillRoundRect(cx - 16 + (int)laughShake, mouthTop, 32, mH, 8,
                            WHITE);
      display.fillRoundRect(cx - 14 + (int)laughShake, mouthTop + 2, 28,
                            max(2, mH - 4), 6, BLACK);
      for (int i = 0; i < 4; i++)
        display.fillRect(cx - 12 + (int)laughShake + i * 7, mouthTop + 2, 5,
                         min(6, max(1, mH - 4)), WHITE);
    } else {
      int mH = min(18, max(2, 63 - (MOUTH_Y + 1 + by)));
      display.fillRoundRect(cx - 18, MOUTH_Y + 1 + by, 36, mH, 8, WHITE);
      display.fillRoundRect(cx - 16, MOUTH_Y + 3 + by, 32, max(2, mH - 4), 6,
                            BLACK);
      for (int i = 0; i < 5; i++)
        display.fillRect(cx - 15 + i * 7, MOUTH_Y + 3 + by, 5,
                         min(7, max(1, mH - 4)), WHITE);
    }
    return;
  }
  if (currentMode == 6) {
    int sh = (int)scaredShake;
    if (isSurprised) {
      display.fillCircle(cx + sh, min(MOUTH_Y + 8, 54), 8, WHITE);
      display.fillCircle(cx + sh, min(MOUTH_Y + 8, 54), 4, BLACK);
    } else {
      for (int x = -12; x < 12; x++) {
        int yo =
            (int)((float)sin(x * 0.5f + (float)(millis()) * 0.01f) * 4.0f) + 8;
        display.fillCircle(cx + x + sh, MOUTH_Y + yo, 1, WHITE);
      }
    }
    return;
  }
  if (currentMode == 7) {
    if (isSmugWink) {
      display.fillCircle(cx + 8, MOUTH_Y + 8, 7, WHITE);
      display.fillCircle(cx + 6, MOUTH_Y + 6, 7, BLACK);
    } else if (isSuspicious) {
      display.fillRect(cx - 4, MOUTH_Y + 12, 18, 3, WHITE);
    } else {
      display.fillCircle(cx + 6, MOUTH_Y + 6, 8, WHITE);
      display.fillCircle(cx + 4, MOUTH_Y + 4, 8, BLACK);
    }
    return;
  }
}

// ================= DRAW OVERLAY EXTRAS =================
// Blush circles, sweat drops, nervous particles
void drawOverlayExtras() {
  unsigned long now = millis();

  // Embarrassed blush circles
  if (ovEmbarrassed) {
    int r = (int)(4 + blushPulse * 3);
    // Filled circles on cheeks (inverted = white on black)
    for (int i = 0; i < r; i += 2) {
      display.drawCircle(EYE_X_L + BASE_EYE_W / 2, MOUTH_Y + 2, i, WHITE);
      display.drawCircle(EYE_X_R + BASE_EYE_W / 2, MOUTH_Y + 2, i, WHITE);
    }
  }

  // Nervous sweat drops flying off
  if (ovNervous) {
    int shk = (int)nervShake;
    drawSweatDrop(8 + shk, 20);
    drawSweatDrop(118 + shk, 20);
    drawSweatDrop(5 + shk, 35);
    drawSweatDrop(121 + shk, 35);
  }

  // Melting — extra drips from bottom (clamped to screen)
  if (ovMelting) {
    int d = (int)meltDroop;
    int dy1 = min(50 + d / 2, 60), dh1 = max(1, min(d / 2, 63 - dy1));
    int dy2 = min(52 + d / 2, 61), dh2 = max(1, min(d / 2, 63 - dy2));
    display.fillRect(EYE_X_L + BASE_EYE_W / 2, dy1, 4, dh1, WHITE);
    display.fillRect(64, dy2, 3, dh2, WHITE);
    display.fillRect(EYE_X_R + BASE_EYE_W / 2, dy1, 4, dh1, WHITE);
  }

  // Thinking — thought bubble dots
  if (ovThinking) {
    display.fillCircle(110, 8, 2, WHITE);
    display.fillCircle(117, 5, 3, WHITE);
    display.fillCircle(124, 2, 4, WHITE);
  }

  // Pain — impact stars
  if (ovPain) {
    int shk = (int)((float)sin((float)now * 0.08f) * 2.0f);
    drawStar(10 + shk, 10, 5);
    drawStar(118 + shk, 10, 5);
  }
}

// ================= HANDLE INPUT =================
void handleInput() {
  bool touch = isTouchActive();
  unsigned long now = millis();

  if (touch && !isTouching) {
    isTouching = true;
    touchStartTime = now;
    lastInteractionTime = now;
    // Non-bored touch: reset boredom stage 1 immediately
    if (isBored && boredStage == 1) {
      isBored = false;
      boredStage = 0;
      boredStartTime = 0;
      triggerModeChange(0); // return to normal — was just mildly bored
      ovShocked = true;
      ovShockedEnd = now + 400;
      transitionFlash = 0.6f;
    }
    if (knockCount < 6 &&
        (now - lastKnockTime < KNOCK_WINDOW || knockCount == 0))
      knockTimes[knockCount++] = now;
    else {
      knockCount = 1;
      knockTimes[0] = now;
    }
    lastKnockTime = now;

    // Register as beat hit ONLY for genuinely rapid sustained hitting.
    // Skip when: navigating (clock/weather), sleeping, or if it's just a few
    // normal taps. The beat system requires 5+ rapid hits, so normal 1-4 tap
    // patterns won't reach it.
    bool isNavigating = (currentMode >= 8 || isSleeping);
    if (!isNavigating && now - lastBeatHit < BEAT_RAPID_WINDOW) {
      registerBeatHit();
    } else if (!isNavigating) {
      beatHitCount = 1;
      beatWindowStart = now; // reset window
    }
    lastBeatHit = now;

    if (isSleeping) {
      isSleeping = false;
      isDriftingOff = false;
      targetEyeOpenFactor = 1.0;
      currentMode = 0;
      hasMidYawned = false;
      hasFinalYawned = false;
      randomMidYawnTime =
          random(20000, SLEEP_TIMEOUT - 15000); // set for next cycle
      startWakeUpDrama();                       // dramatic 4-phase wake-up
      changeAffection(1, "woke me up");
    }
  }

  if (touch && isTouching && !isLongPressing &&
      now - touchStartTime > LONG_PRESS_TIME) {
    isLongPressing = true;
    triggerLongPressAction();
    tapCount = 0;
  }

  // --- MID-HOLD timers for bored/angry bot flip ---
  if (touch && isTouching && isLongPressing && isBored) {
    unsigned long holdDur = now - touchStartTime;
    if (!holdAngryTriggered && holdDur > HOLD_ANGRY_MS) {
      holdAngryTriggered = true;
      // More furious — visual escalation
      isFurious = true;
      ovNervous = true;
      ovNervousEnd = now + 99999; // stays until released
      ovPain = true;
      ovPainEnd = now + 700;
      transitionFlash = 1.0f;
      Serial.println("Still holding angry bot → more furious");
    }
    if (!holdHappyTriggered && holdDur > HOLD_HAPPY_MS) {
      holdHappyTriggered = true;
      // Bot MELTS into happiness — the long patient hold finally works!
      isFurious = false;
      ovNervous = false;
      ovPain = false;
      ovDisgusted = false;
      ovMelting = true;
      ovMeltingEnd = now + 2000;
      meltDroop = 0; // melt of relief
      ovEmbarrassed = true;
      ovEmbarrassedEnd = now + 2500; // bashful after tantrum
      isBeingPetted = true;
      smoothModeChange(1, 1200); // → love, with delay for melt to play
      transitionFlash = 0.9f;
      // Clear boredom
      isBored = false;
      boredStage = 0;
      boredStartTime = 0;
      lastInteractionTime = now;
      Serial.println("Patient hold worked! Bot is happy ❤");
    }
  }

  if (!touch && isTouching) {
    isTouching = false;
    if (isLongPressing) {
      isLongPressing = false;
      releaseLongPress();
    } else {
      tapCount++;
      lastTapTime = now;
    }
  }

  if (!touch && !isLongPressing && tapCount > 0 &&
      now - lastTapTime > DOUBLE_TAP_DELAY) {
    if (tapCount == 1)
      triggerSingleTapAction();
    else if (tapCount == 2) {
      // Smooth transition between face/clock/weather
      if (currentMode == 8)
        startScreenTransition(9);
      else if (currentMode == 9)
        startScreenTransition(previousMode);
      else {
        previousMode = currentMode;
        startScreenTransition(8);
      }
    } else if (tapCount == 3) {
      // Triple-tap on clock/weather = toggle sound mute
      if (currentMode == 8 || currentMode == 9) {
        soundEnabled = !soundEnabled;
        if (soundEnabled) {
          buzzerPlay(SND_CLICK); // confirmation beep (only plays if now unmuted)
        } else {
          buzzerStop(); // silence any playing sound immediately
        }
        showMuteOverlay = true;
        muteOverlayEnd = now + 1500;
        Serial.println(soundEnabled ? "Sound ON" : "Sound OFF");
      } else {
        triggerStartleChain();
      }
    } // multi-phase startle reaction / sound toggle
    else if (tapCount == 4) {
      isLaughing = true;
      laughEndTime = now + 4000;
      changeAffection(2, "made me laugh");
      buzzerPlay(SND_LAUGH);
    } else if (tapCount >= 5)
      detectKnockPattern();
    tapCount = 0;
  }

  if (!touch && knockCount >= 3 && now - lastKnockTime > KNOCK_WINDOW) {
    detectKnockPattern();
    knockCount = 0;
  }
}

// ================= SINGLE TAP — triggers overlays =================
void triggerSingleTapAction() {
  lastInteractionTime = millis();
  unsigned long now = millis();
  buzzerPlay(SND_CLICK); // touch feedback click

  // Hydration and posture reminders auto-dismiss — no tap needed
  // (Previously required tap which caused screen to get stuck)

  // --- BORED/ANGRY: touching an irritated bot makes it worse ---
  if (isBored) {
    if (boredStage == 1) {
      // Stage 1 bored: tap → disgusted, jumps to angry
      ovDisgusted = true;
      ovDisgustedEnd = now + 2500;
      isSuspicious = true;
      suspiciousEndTime = now + 1200;
      boredStage = 2;
      boredStartTime = now - BORED_ANGRY_AT; // fast-track to stage 2
      smoothModeChange(2, 400);
      transitionFlash = 0.9f;
      Serial.println("Bored bot tapped → irritated → angry");
    } else if (boredStage >= 2) {
      // Stage 2/3 angry: tap → furious spike
      isFurious = true;
      isRejected = true;
      rejectEndTime = now + 600;
      ovPain = true;
      ovPainEnd = now + 800;
      ovShocked = true;
      ovShockedEnd = now + 300;
      transitionFlash = 1.0f;
      Serial.println("Angry bot tapped → furious spike");
    }
    return; // skip normal tap logic
  }

  // --- NORMAL tap reactions by base mood (with affection-flavored personality)
  // ---
  recentTapCount++;

  switch (currentMode) {
  case 0:
    // Alive: friendly blink
    changeAffection(1, "gentle tap");
    isPuppySquint = true;
    squintEndTime = now + 1200;
    break;
  case 1:
    // Love mode: shy blush + look away — extra affection
    changeAffection(2, "tapped while loving");
    isWinking = true;
    winkEndTime = now + 600;
    if (random(0, 3) == 0) {
      ovEmbarrassed = true;
      ovEmbarrassedEnd = now + 1500;
    }
    break;
  case 2:
    // Angry mode: DON'T reward tapping! Snaps back angrily
    changeAffection(-1, "poked angry bot");
    isRejected = true;
    rejectEndTime = now + 800;
    buzzerPlay(SND_ANGRY);
    if (random(0, 2) == 0) {
      isSuspicious = true;
      suspiciousEndTime = now + 2000;
    }
    break;
  case 3:
    // Sad mode: tapping makes it cry harder — you need to HOLD to comfort!
    changeAffection(-2, "tapped while sad");
    isCryingHard = true;
    isComforted = false;
    needsConsoling = true;
    ovShocked = true;
    ovShockedEnd = now + 300; // brief flinch
    buzzerPlay(SND_CRY);
    break;
  case 4:
    // Dramatic mode: theatrical sigh, extra spiral
    spiralAngle += 2.0;
    ovSleepy = true;
    ovSleepyEnd = now + 1500;
    sleepDroop = 0;
    buzzerPlay(SND_BORED);
    break;
  case 5:
    // Excited mode: tapping makes it even more hyper!
    changeAffection(1, "excitement tap");
    isSurprised = true;
    surpriseEndTime = now + 700;
    buzzerPlay(SND_CELEBRATE);
    break;
  case 6:
    // Scared mode: tap reassures it a tiny bit
    changeAffection(1, "reassured scared bot");
    isSurprised = true;
    surpriseEndTime = now + 600;
    buzzerPlay(SND_CONSOLED);
    // Small chance to calm down to alive
    if (random(0, 4) == 0) {
      smoothModeChange(0, 400);
      transitionFlash = 0.5f;
    }
    break;
  case 7:
    // Smug mode: extra slow wink, smirk — smugly tolerates you
    isSuspicious = true;
    suspiciousEndTime = now + 2000;
    if (random(0, 3) == 0) {
      isSmugWink = true;
      smugWinkEndTime = now + 1500;
    }
    break;
  }

  int r = random(0, 4);
  if (currentMode == 0) {
    if (r == 0) {
      ovThinking = true;
      ovThinkingEnd = now + 4000;
    } else if (r == 1) {
      ovSleepy = true;
      ovSleepyEnd = now + 3000;
      sleepDroop = 0;
    } else if (r == 2) {
      ovNervous = true;
      ovNervousEnd = now + 2500;
    }
  } else if (currentMode == 1) {
    if (r == 0) {
      ovEmbarrassed = true;
      ovEmbarrassedEnd = now + 3500;
    } else if (r == 1) {
      ovCryLaugh = true;
      ovCryLaughEnd = now + 4000;
    }
  } else if (currentMode == 2) {
    if (r == 0) {
      ovPain = true;
      ovPainEnd = now + 1500;
    } else if (r == 1) {
      ovDisgusted = true;
      ovDisgustedEnd = now + 3000;
    }
  } else if (currentMode == 3) {
    if (r == 0) {
      ovCryLaugh = true;
      ovCryLaughEnd = now + 4000;
    } else if (r == 1) {
      ovNervous = true;
      ovNervousEnd = now + 2500;
    }
  } else if (currentMode == 4) {
    ovHyper = true;
    ovHyperEnd = now + 2000;
  } else if (currentMode == 5) {
    if (r == 0) {
      ovHyper = true;
      ovHyperEnd = now + 1500;
    } else if (r == 1) {
      ovCryLaugh = true;
      ovCryLaughEnd = now + 2500;
    }
  } else if (currentMode == 6) {
    if (r == 0) {
      ovShocked = true;
      ovShockedEnd = now + 2000;
    } else if (r == 1) {
      ovNervous = true;
      ovNervousEnd = now + 2000;
    }
  } else if (currentMode == 7) {
    if (r == 0) {
      ovDisgusted = true;
      ovDisgustedEnd = now + 3000;
    } else if (r == 1) {
      ovThinking = true;
      ovThinkingEnd = now + 4000;
    }
  }
}

// ================= LONG PRESS =================
void triggerLongPressAction() {
  lastInteractionTime = millis();
  unsigned long now = millis();
  holdHappyTriggered = false;
  holdAngryTriggered = false;

  if (isBored) {
    // Holding an angry bot: first burst of anger
    isFurious = true;
    ovPain = true;
    ovPainEnd = now + 1200;
    transitionFlash = 1.0f;
    // holdAngryTriggered stays false — will be set after HOLD_ANGRY_MS in
    // handleInput
    Serial.println("Angry bot held — phase 1: furious");
    return;
  }

  // Normal long press per mood
  recentPetCount++;
  changeAffection(3, "being petted");
  switch (currentMode) {
  case 0:
    isBeingPetted = true;
    break;
  case 1:
    isBeingPetted = true;
    break;
  case 2:
    isBeingLoved = true;
    break;
  case 3:
    // Sad mode: consoling arc — hold to comfort
    isComforted = true;
    isCryingHard = false;
    isBeingConsoled = true;
    consoleStartAt = now;
    needsConsoling = true; // stays true until fully consoled
    Serial.println("Consoling sad bot... hold for 3s");
    break;
  case 4:
    isFurious = true;
    break;
  case 5:
    isLaughing = true;
    laughEndTime = now + 4000;
    buzzerPlay(SND_LAUGH);
    break;
  case 6:
    isBeingPetted = true;
    break;
  case 7:
    isSmugWink = true;
    smugWinkEndTime = now + 1500;
    break;
  }
  // Melting overlay — only for receptive moods (not during sad consoling or
  // anger)
  if (currentMode != 3 && currentMode != 2 && currentMode != 4) {
    ovMelting = true;
    ovMeltingEnd = now + 5000;
    meltDroop = 0;
  }
}

void releaseLongPress() {
  if (isBeingLoved && currentMode == 2) {
    smoothModeChange(1, 300);
    transitionFlash = 0.7f;
    buzzerPlay(SND_CONSOLED);
    changeAffection(5, "loved angry bot into calm");
  }
  if (isBeingPetted && currentMode == 6) {
    smoothModeChange(0, 300);
    transitionFlash = 0.6f;
    buzzerPlay(SND_CONSOLED);
    changeAffection(3, "comforted scared bot");
  }

  // If bored bot was held long enough for happy flip, clear boredom
  if (holdHappyTriggered) {
    isBored = false;
    boredStage = 0;
    boredStartTime = 0;
    isFurious = false;
    lastInteractionTime = millis();
    changeAffection(10, "you came back! forgiven!");
    buzzerPlay(SND_FORGIVEN);
    // smoothModeChange already called from handleInput mid-hold
    Serial.println("Angry bot held long enough → happy! Boredom cleared.");
  } else if (isBored && boredStage >= 2) {
    // Released too early — stays angry, maybe a bit less
    isFurious = false;
  }

  // --- CONSOLING: if held long enough while sad, recover mood ----
  if (isBeingConsoled && currentMode == 3) {
    unsigned long holdDur = millis() - consoleStartAt;
    if (holdDur > CONSOLE_HOLD_MS) {
      // Fully consoled! Transition to alive with grateful face
      needsConsoling = false;
      isBeingConsoled = false;
      smoothModeChange(0, 400);
      ovEmbarrassed = true;
      ovEmbarrassedEnd = millis() + 2000;
      transitionFlash = 0.5f;
      changeAffection(5, "consoled me");
      buzzerPlay(SND_CONSOLED);
      Serial.println("Bot consoled! Recovering to happy");
    }
  } else {
    isBeingConsoled = false;
  }

  isBeingPetted = false;
  isFurious = false;
  // Only clear isComforted if consoling wasn't just completed
  if (!isBeingConsoled)
    isComforted = false;
  isBeingLoved = false;
  holdHappyTriggered = false;
  holdAngryTriggered = false;
}

// ================= KNOCK PATTERNS =================
void detectKnockPattern() {
  if (knockCount < 3)
    return;
  bool iv[5];
  for (int i = 1; i < knockCount && i <= 5; i++)
    iv[i - 1] = (knockTimes[i] - knockTimes[i - 1]) < 400;
  String p = "";
  for (int i = 0; i < knockCount - 1 && i < 5; i++)
    p += iv[i] ? "S" : "L";
  if (p == "SSS" || p == "SSSS")
    triggerModeChange(5);
  else if (p == "LLL" || p == "LL")
    triggerModeChange(0);
  else if (p == "SLS")
    triggerModeChange(2);
  else if (p == "LSL")
    triggerModeChange(1);
  else if (p == "SS")
    triggerModeChange(3);
  else if (p == "SL")
    triggerModeChange(6);
  else if (p == "LS")
    triggerModeChange(7);
  else
    triggerModeChange(random(0, 8));
  knockCount = 0;
}

// ================= BASE ANIMATIONS =================
void triggerYawn(int dur) {
  if (isBeingPetted)
    return;
  isYawning = true;
  yawnEndTime = millis() + dur;
  targetYawnFactor = 1.0;
  buzzerPlay(SND_YAWN);
}
void updateHeartbeat() {
  float b = (float)sin((float)millis() * 0.015f);
  heartScale = b > 0.8 ? 1.2 : (b > 0.0 ? 1.0 + b * 0.2 : 1.0);
}

void updateAliveAnimations() {
  unsigned long now = millis();
  if (isRejected && now > rejectEndTime)
    isRejected = false;
  if (isYawning) {
    if (now > yawnEndTime) {
      isYawning = false;
      targetYawnFactor = 0;
    } else
      targetYawnFactor = 1.0;
  }
  bool canLook = !isDriftingOff && currentYawnFactor < 0.1 && !isYawning &&
                 !isBeingPetted && !isPuppySquint && !ovThinking &&
                 !ovEmbarrassed;
  if (canLook) {
    if (now > nextLookTime) {
      int r = random(0, 10);
      if (r < 6) {
        lookDirection = 0;
        nextLookTime = now + random(2000, 5000);
      } else if (r < 8) {
        lookDirection = -1;
        nextLookTime = now + 600;
      } else {
        lookDirection = 1;
        nextLookTime = now + 600;
      }
    }
    // Look direction — scale shift to ~30% of eye width for natural glance
    int lookShift = BASE_EYE_W * 3 / 10; // proportional to eye size
    int mouthShift = BASE_EYE_W * 4 / 10;
    if (lookDirection == 0) {
      targetEyeW_L = BASE_EYE_W;
      targetEyeW_R = BASE_EYE_W;
      targetMouthX = 0;
    } else if (lookDirection == -1) {
      targetEyeW_L = BASE_EYE_W - lookShift;
      targetEyeW_R = BASE_EYE_W + lookShift;
      targetMouthX = -mouthShift;
    } else {
      targetEyeW_L = BASE_EYE_W + lookShift;
      targetEyeW_R = BASE_EYE_W - lookShift;
      targetMouthX = mouthShift;
    }
  } else {
    targetEyeW_L = BASE_EYE_W;
    targetEyeW_R = BASE_EYE_W;
    targetMouthX = 0;
  }
  auto mv = [](float c, float t, float s) {
    if (abs(c - t) < s)
      return t;
    return c < t ? c + s : c - s;
  };
  currentEyeW_L = mv(currentEyeW_L, targetEyeW_L, PAN_SPEED);
  currentEyeW_R = mv(currentEyeW_R, targetEyeW_R, PAN_SPEED);
  currentMouthX = mv(currentMouthX, targetMouthX, PAN_SPEED);
  currentYawnFactor = mv(currentYawnFactor, targetYawnFactor, YAWN_SPEED);
  currentEyeOpenFactor =
      mv(currentEyeOpenFactor, targetEyeOpenFactor, SLEEP_SPEED);
  if (!isDriftingOff && !isBeingPetted && currentYawnFactor < 0.1 &&
      !ovSleepy) {
    if (now - lastBlinkTime > (unsigned long)blinkInterval) {
      isBlinking = true;
      if (now - lastBlinkTime > (unsigned long)blinkInterval + 120) {
        isBlinking = false;
        lastBlinkTime = now;
        // Human-like blink timing: mostly 2-5s, occasional rapid double-blink
        int r = random(0, 100);
        if (r < 15)
          blinkInterval = random(200, 400); // quick double-blink (15%)
        else if (r < 70)
          blinkInterval = random(2000, 4000); // normal interval (55%)
        else
          blinkInterval = random(4000, 7000); // relaxed long pause (30%)
      }
    }
  }
}

// ================= BEAT SYSTEM =================
void registerBeatHit() {
  unsigned long now = millis();
  beatHitCount++;
  beatShake = 4.0;
  beatStarsActive = true;
  beatStarEnd = now + 600;
  buzzerPlay(SND_HIT); // impact thud

  int prevStage = beatStage;
  if (beatHitCount >= BEAT_HITS_KO) {
    beatStage = 4;
    isKO = true;
    koEnd = now + 4000;
    buzzerPlay(SND_KO);
  } else if (beatHitCount >= BEAT_HITS_STAGE3)
    beatStage = 3;
  else if (beatHitCount >= BEAT_HITS_STAGE2)
    beatStage = 2;
  else if (beatHitCount >= BEAT_HITS_STAGE1)
    beatStage = 1;

  if (beatStage != prevStage) {
    ovNervous = false;
    ovSleepy = false;
    ovShocked = false;
    ovDisgusted = false;
    ovEmbarrassed = false;
    ovThinking = false;
    ovHyper = false;
    ovMelting = false;
    ovCryLaugh = false;

    if (beatStage == 1) {
      ovShocked = true;
      ovShockedEnd = now + 600;
      ovPain = true;
      ovPainEnd = now + 600;
      changeAffection(-2, "ouch!");
    } else if (beatStage == 2) {
      ovPain = true;
      ovPainEnd = now + 99999;
      ovNervous = true;
      ovNervousEnd = now + 99999;
      changeAffection(-5, "that hurt!");
      buzzerPlay(SND_HIT); // louder impact
    } else if (beatStage == 3) {
      triggerModeChange(3);
      isCryingHard = true;
      ovShocked = true;
      ovShockedEnd = now + 800;
      changeAffection(-7, "why are you doing this?!");
      buzzerPlay(SND_SOB);
    } else if (beatStage == 4) {
      triggerModeChange(4);
      ovPain = true;
      ovPainEnd = now + 99999;
      changeAffection(-10, "KO!! that was awful");
      recentBeatCount++;
    }
  }
  beatRecoverAt = now + 3000;
}

void updateBeat(unsigned long now) {
  beatShake *= 0.85;
  if (beatShake < 0.3)
    beatShake = 0;

  if (beatStarsActive) {
    beatStarAngle += 0.2;
    if (now > beatStarEnd)
      beatStarsActive = false;
  }

  if (isKO) {
    koSpinAngle += 0.15;
    if (now > koEnd) {
      isKO = false;
      triggerModeChange(3); // recover to sad after KO
      beatStage = 0;
      beatHitCount = 0;
      beatShake = 0;
      ovPain = false;
      ovNervous = false;
      // Start forgiveness arc — bot is huffy and needs to decide if it forgives
      // you
      isForgiving = true;
      forgiveStartAt = millis();
      forgivePhase = 0;
      Serial.println("KO recovery -> forgiveness arc started");
    }
  }

  // Auto-recover if no hits for 3s
  if (beatStage > 0 && now > beatRecoverAt && !isKO) {
    beatStage--;
    beatRecoverAt = now + 2000;
    if (beatStage == 0) {
      beatHitCount = 0;
      beatShake = 0;
      ovPain = false;
      ovNervous = false;
      if (currentMode == 3 || currentMode == 4)
        triggerModeChange(0);
    }
  }
}

void drawBeatOverlay() {
  if (beatStage == 0 && !beatStarsActive && !isKO)
    return;
  unsigned long now = millis();

  // Screen border flash on hit
  if (beatShake > 1.5) {
    display.drawRect(0, 0, 128, 64, WHITE);
    if (beatShake > 3.0)
      display.drawRect(1, 1, 126, 62, WHITE);
  }

  // Impact stars flying off on each hit
  if (beatStarsActive) {
    for (int i = 0; i < 4; i++) {
      float ang = beatStarAngle + i * (M_PI / 2.0f);
      int sx = 64 + (int)((float)cos(ang) * 28.0f);
      int sy = 20 + (int)((float)sin(ang) * 16.0f);
      if (sx > 2 && sx < 126 && sy > 2 && sy < 62)
        drawStar(sx, sy, 4);
    }
  }

  // KO — big spinning stars + forced X eyes
  if (isKO) {
    for (int i = 0; i < 5; i++) {
      float ang = koSpinAngle + i * (2.0f * M_PI / 5.0f);
      int sx = 64 + (int)((float)cos(ang) * 38.0f);
      int sy = 22 + (int)((float)sin(ang) * 18.0f);
      if (sx > 2 && sx < 126 && sy > 2 && sy < 62)
        drawStar(sx, sy, 5);
    }
    // Force X eyes over everything — scaled to face size
    int ey = EYE_Y + EYE_H / 2;
    int xSz = BASE_EYE_W / 2, ySz = EYE_H / 4;
    int lcx = EYE_X_L + BASE_EYE_W / 2, rcx = EYE_X_R + BASE_EYE_W / 2;
    display.drawLine(lcx - xSz, ey - ySz, lcx + xSz, ey + ySz, WHITE);
    display.drawLine(lcx - xSz, ey + ySz, lcx + xSz, ey - ySz, WHITE);
    display.drawLine(rcx - xSz, ey - ySz, rcx + xSz, ey + ySz, WHITE);
    display.drawLine(rcx - xSz, ey + ySz, rcx + xSz, ey - ySz, WHITE);
  }

  // Stage 2+ — pulsing pain aura dots
  if (beatStage >= 2) {
    int d = (int)(now / 100) % 3;
    display.fillCircle(8 + d, 32, 2, WHITE);
    display.fillCircle(120 - d, 32, 2, WHITE);
    display.fillCircle(64, 4 + d, 2, WHITE);
  }
}

// ================= EMOTIONAL MEMORY =================
void changeAffection(int delta, const char *reason) {
  int old = affectionScore;
  affectionScore = constrain(affectionScore + delta, -100, 100);
  if (affectionScore != old) {
    Serial.println("Affection " + String(old) + "->" + String(affectionScore) +
                   " (" + String(reason) + ")");
  }
}

void updateEmotionalMemory(unsigned long now) {
  // Decay affection over time when idle (lonely bot loses trust)
  if (now - lastAffectionDecay > AFFECTION_DECAY_MS) {
    lastAffectionDecay = now;
    if (!isDancing && !isIncomingCall && !callActive) {
      changeAffection(-1, "idle decay");
    }
  }

  // Reset sliding memory window
  if (now - lastMemoryReset > MEMORY_WINDOW) {
    lastMemoryReset = now;
    recentPetCount = 0;
    recentTapCount = 0;
    recentBeatCount = 0;
  }
}

// ================= PERSONALITY QUIRKS =================
// Random cute micro-animations: sigh, side-eye, wiggle, nose-scrunch,
// suspicious-squint, mini-bounce, sneeze
void updateQuirks(unsigned long now) {
  // Only do quirks when in alive mode, not sleeping, not bored, not doing
  // overlays
  if (currentMode != 0 || isSleeping || isBored || isDancing) {
    isDoingQuirk = false;
    return;
  }
  // Don't quirk if an overlay is already playing
  if (ovNervous || ovSleepy || ovShocked || ovDisgusted || ovEmbarrassed ||
      ovThinking || ovHyper || ovPain || ovMelting || ovCryLaugh) {
    isDoingQuirk = false;
    return;
  }

  // Schedule next quirk
  if (nextQuirkAt == 0)
    nextQuirkAt = now + random(15000, 45000);

  // Expire current quirk
  if (isDoingQuirk && now > quirkEndAt) {
    isDoingQuirk = false;
    quirkParam = 0;
    nextQuirkAt = now + random(15000, 45000);
  }

  // --- IDLE CHIRPS: random happy sounds when content ---
  static unsigned long nextIdleSound = 0;
  if (nextIdleSound == 0)
    nextIdleSound = now + random(20000, 50000);
  if (!isDoingQuirk && !buzzerPlaying && now > nextIdleSound &&
      (currentMode == 0 || currentMode == 1) && !isSleeping && !isBored &&
      !isIncomingCall && !callActive && affectionScore > 20) {
    int sndType = random(0, 4);
    switch (sndType) {
    case 0:
      buzzerPlay(SND_CHIRP);
      break; // cheerful peep
    case 1:
      buzzerPlay(SND_HUM);
      break; // gentle hum
    case 2:
      buzzerPlay(SND_GIGGLE);
      break; // tiny giggle
    case 3:
      break; // sometimes silence is golden
    }
    nextIdleSound = now + random(25000, 60000); // next in 25-60s
  }

  // Trigger new quirk
  if (!isDoingQuirk && now > nextQuirkAt) {
    isDoingQuirk = true;
    quirkType = random(0, 7);
    quirkParam = 0;

    switch (quirkType) {
    case 0:
      quirkEndAt = now + 1800;
      buzzerPlay(SND_SIGH);
      break; // sigh
    case 1:
      quirkEndAt = now + 1200;
      break; // side-eye (silent — it's subtle)
    case 2:
      quirkEndAt = now + 800;
      buzzerPlay(SND_WIGGLE);
      break; // wiggle
    case 3:
      quirkEndAt = now + 600;
      buzzerPlay(SND_HICCUP);
      break; // nose scrunch / hiccup
    case 4:
      quirkEndAt = now + 1500;
      break; // suspicious squint (silent)
    case 5:
      quirkEndAt = now + 900;
      buzzerPlay(SND_GIGGLE);
      break; // mini bounce
    case 6:
      quirkEndAt = now + 1400;
      buzzerPlay(SND_SNEEZE);
      break; // sneeze
    }
    Serial.println("Quirk! type=" + String(quirkType));
  }

  // Animate quirk progress
  if (isDoingQuirk) {
    unsigned long quirkDuration =
        quirkEndAt - (quirkEndAt > 1800 ? 1800 : quirkEndAt);
    // Calculate actual duration based on quirk type
    unsigned long actualDur;
    switch (quirkType) {
    case 0:
      actualDur = 1800;
      break;
    case 1:
      actualDur = 1200;
      break;
    case 2:
      actualDur = 800;
      break;
    case 3:
      actualDur = 600;
      break;
    case 4:
      actualDur = 1500;
      break;
    case 5:
      actualDur = 900;
      break;
    case 6:
      actualDur = 1400;
      break;
    default:
      actualDur = 1800;
      break;
    }
    unsigned long quirkStart = quirkEndAt - actualDur;
    float elapsed =
        (now > quirkStart) ? (float)(now - quirkStart) / (float)actualDur : 0;
    quirkParam = min(elapsed, 1.0f);
  }
}

void drawQuirkOverlay() {
  if (!isDoingQuirk)
    return;
  unsigned long now = millis();
  int cx = 64;

  switch (quirkType) {
  case 0: { // SIGH — puff circle expanding from mouth area
    float progress = quirkParam;
    if (progress > 0.3f) {
      float puffSize = (progress - 0.3f) * 14.0f;
      int puffX = cx + 14 + (int)(puffSize * 2.0f);
      int puffY = MOUTH_Y + 5 - (int)(puffSize * 0.8f);
      int r = 2 + (int)(puffSize * 0.5f);
      if (r > 1 && puffX < 126)
        display.drawCircle(puffX, puffY, r, WHITE);
      if (r > 3 && puffX + 4 < 126)
        display.drawCircle(puffX + 4, puffY - 2, r - 1, WHITE);
    }
    break;
  }
  case 1: { // SIDE-EYE — handled by alive animations look direction
    // Force a look direction during quirk
    if (quirkParam < 0.8f) {
      lookDirection = (quirkParam < 0.4f) ? 0 : ((random(0, 2) == 0) ? -1 : 1);
      nextLookTime = now + 500;
    }
    break;
  }
  case 2: { // WIGGLE — whole screen shifts left-right 3x
    float t = quirkParam * 3.0f * 3.14159f;
    int shift = (int)(sinf(t) * 3.0f * (1.0f - quirkParam));
    // Apply as screen offset — small border effect
    if (abs(shift) > 0) {
      display.drawLine(64 + shift, 0, 64 + shift, 63, WHITE);
    }
    break;
  }
  case 3: { // NOSE SCRUNCH — mouth squishes up briefly
    if (quirkParam < 0.6f) {
      int squishY = -3 + (int)(sinf(quirkParam * 6.28f) * 3.0f);
      display.fillCircle(cx, MOUTH_Y + squishY + 4, 4, WHITE);
      display.fillCircle(cx, MOUTH_Y + squishY + 1, 4, BLACK);
    }
    break;
  }
  case 4: { // SUSPICIOUS SQUINT — small eye indicator
    if (quirkParam > 0.2f && quirkParam < 0.8f) {
      // Draw "hmm" indicator — thought dots
      display.fillCircle(108, 10, 2, WHITE);
      display.fillCircle(115, 7, 3, WHITE);
    }
    break;
  }
  case 5: { // MINI BOUNCE — simple bounce indicator dots
    if (quirkParam < 0.7f) {
      float b = sinf(quirkParam * 12.0f) * 2.0f;
      display.fillCircle(4, 32 + (int)b, 2, WHITE);
      display.fillCircle(124, 32 + (int)b, 2, WHITE);
    }
    break;
  }
  case 6: { // SNEEZE — eyes squeeze then particles fly
    if (quirkParam > 0.6f) {
      // "PCHOO!" particles
      float burst = (quirkParam - 0.6f) * 2.5f;
      for (int i = 0; i < 5; i++) {
        float a = i * 1.2566f + burst * 2.0f; // 2π/5 + spin
        int px = cx + (int)(cosf(a) * burst * 20.0f);
        int py = MOUTH_Y + 5 + (int)(sinf(a) * burst * 12.0f);
        if (px > 0 && px < 128 && py > 0 && py < 64)
          display.fillCircle(px, py, 1, WHITE);
      }
    }
    break;
  }
  }
}

// ================= ATTENTION SEEKING =================
// Graduated stages before boredom
void updateAttentionSeeking(unsigned long now) {
  unsigned long idle = now - lastInteractionTime;

  // Reset if interacted, dancing, sleeping, bored, etc.
  if (idle < 60000 || isDancing || isSleeping || isBored || isIncomingCall ||
      callActive) {
    if (attentionStage > 0) {
      attentionStage = 0;
      attentionSideLook = false;
      attentionBounce = 0;
      fakeSnoring = false;
      fakeSleepEyeOpen = 0;
    }
    return;
  }

  // Only in base modes 0-7
  if (currentMode >= 8)
    return;

  // Stage 1: 3 min — subtle side-eye + occasional sigh quirk
  if (attentionStage == 0 && idle > 180000UL) {
    attentionStage = 1;
    lastAttentionEscalate = now;
    buzzerPlay(SND_SIGH);
    Serial.println("Attention stage 1: side-eye");
  }
  // Stage 2: 5 min — bouncy expectant look
  if (attentionStage == 1 && idle > 300000UL) {
    attentionStage = 2;
    lastAttentionEscalate = now;
    ovShocked = true;
    ovShockedEnd = now + 400;
    buzzerPlay(SND_ATTENTION);
    Serial.println("Attention stage 2: bouncy");
  }
  // Stage 3: 8 min — dramatic slump, fake pouting
  if (attentionStage == 2 && idle > 480000UL) {
    attentionStage = 3;
    lastAttentionEscalate = now;
    ovSleepy = true;
    ovSleepyEnd = now + 3000;
    sleepDroop = 0;
    buzzerPlay(SND_BORED);
    Serial.println("Attention stage 3: dramatic slump");
  }
  // Stage 4: 12 min — fake sleep but one eye peeks open
  if (attentionStage == 3 && idle > 720000UL) {
    attentionStage = 4;
    lastAttentionEscalate = now;
    fakeSnoring = true;
    if (!buzzerPlaying) buzzerPlay(SND_SLEEP); // play only once
    Serial.println("Attention stage 4: fake sleeping");
  }

  // Animate attention stage effects
  if (attentionStage == 1) {
    // Periodic side-look towards user
    if ((now / 4000) % 3 == 0) {
      attentionSideLook = true;
      lookDirection = 1;
      nextLookTime = now + 2000;
    } else {
      attentionSideLook = false;
    }
  }
  if (attentionStage == 2) {
    // Bouncy expectant — gentle up/down
    attentionBounce = sinf(now * 0.008f) * 2.0f;
  }
  if (attentionStage >= 3) {
    // Dramatic slump — eyes droop
    if (!ovSleepy && attentionStage == 3) {
      ovSleepy = true;
      ovSleepyEnd = now + 99999;
      sleepDroop = 0.4f;
    }
  }
  if (attentionStage == 4) {
    // Fake sleep — but one eye peeks open periodically
    fakeSleepEyeOpen = ((now / 5000) % 3 == 0) ? 0.3f : 0.0f;
  }
}

// ================= STARTLE CHAIN =================
// Multi-phase: shocked → look-around → embarrassed recovery
void triggerStartleChain() {
  unsigned long now = millis();
  isStartled = true;
  startlePhase = 1; // shocked
  startlePhaseEnd = now + 500;
  ovShocked = true;
  ovShockedEnd = now + 500;
  transitionFlash = 0.8f;
  buzzerPlay(SND_SCARED);
  Serial.println("Startle chain started!");
}

void updateStartleChain(unsigned long now) {
  if (!isStartled)
    return;

  if (now > startlePhaseEnd) {
    startlePhase++;
    if (startlePhase == 2) {
      // Phase 2: looking around nervously
      startlePhaseEnd = now + 1500;
      ovNervous = true;
      ovNervousEnd = now + 1500;
      lookDirection = -1;
      nextLookTime = now + 500;
    } else if (startlePhase == 3) {
      // Phase 3: embarrassed recovery — "oh it was nothing..."
      startlePhaseEnd = now + 2000;
      ovEmbarrassed = true;
      ovEmbarrassedEnd = now + 2000;
      ovNervous = false;
    } else {
      // Done
      isStartled = false;
      startlePhase = 0;
      ovEmbarrassed = false;
    }
  }

  // Phase 2: look around animation
  if (startlePhase == 2) {
    int lookPhase = (int)((now / 500) % 3);
    lookDirection = (lookPhase == 0) ? -1 : (lookPhase == 1) ? 1 : 0;
    nextLookTime = now + 200;
  }
}

// ================= FORGIVENESS ARC =================
// After KO: huffy → side-eye → tentative → forgiven
void updateForgiveness(unsigned long now) {
  if (!isForgiving)
    return;

  unsigned long elapsed = now - forgiveStartAt;

  if (forgivePhase == 0 && elapsed > 3000) {
    // Phase 1: side-eye peek
    forgivePhase = 1;
    isSuspicious = true;
    suspiciousEndTime = now + 3000;
    Serial.println("Forgiveness: side-eye");
  }
  if (forgivePhase == 1 && elapsed > 6000) {
    // Phase 2: tentative — nervous look
    forgivePhase = 2;
    isSuspicious = false;
    ovNervous = true;
    ovNervousEnd = now + 4000;
    Serial.println("Forgiveness: tentative");
  }
  if (forgivePhase == 2 && elapsed > 10000) {
    // Phase 3: forgiven — based on affection
    forgivePhase = 3;
    ovNervous = false;
    if (affectionScore > 30) {
      // Forgives! Happy + embarrassed
      smoothModeChange(0, 400);
      ovEmbarrassed = true;
      ovEmbarrassedEnd = now + 2000;
      transitionFlash = 0.6f;
      changeAffection(5, "forgave you");
      buzzerPlay(SND_FORGIVEN);
      Serial.println("Forgiveness: forgiven! <3");
    } else {
      // Still mad
      smoothModeChange(7, 400); // smug/dismissive
      ovDisgusted = true;
      ovDisgustedEnd = now + 2000;
      buzzerPlay(SND_ANGRY);
      Serial.println("Forgiveness: still mad (low affection)");
    }
    isForgiving = false;
    forgivePhase = 0;
  }
}

// ================= PURRING / BLISS =================
void updatePurring(unsigned long now) {
  // Check if petting long enough to trigger purring
  if (isBeingPetted && !isPurring) {
    if (petHoldStart == 0)
      petHoldStart = now;
    if (now - petHoldStart > 2000) {
      isPurring = true;
      purringStartAt = now;
      buzzerPlay(SND_PURR, true, 0); // loop purr sound until stopped
      Serial.println("Purring started! <3");
    }
  }

  // Stop purring when not being petted
  if (!isBeingPetted && isPurring) {
    isPurring = false;
    purrVibrate = 0;
    petHoldStart = 0;
    buzzerStop(); // stop purr sound
    // "Why did you stop?!" surprise face
    purrStopSurprise = true;
    purrStopEnd = now + 1200;
    ovShocked = true;
    ovShockedEnd = now + 800;
    Serial.println("Purr stopped! Surprised face");
  }

  if (!isBeingPetted) {
    petHoldStart = 0;
  }

  // Expire surprise face
  if (purrStopSurprise && now > purrStopEnd) {
    purrStopSurprise = false;
  }

  // Animate purr vibration
  if (isPurring) {
    purrVibrate = sinf(now * 0.03f) * 1.5f;
    // Build affection while purring (every 3 seconds, using dedicated timer)
    static unsigned long lastPurrAffection = 0;
    if (now - lastPurrAffection > 3000) {
      lastPurrAffection = now;
      changeAffection(1, "purring bonding");
    }
  }
}

void drawPurrHearts(unsigned long now) {
  if (!isPurring && !purrStopSurprise)
    return;

  if (isPurring) {
    // Floating mini hearts
    for (int i = 0; i < 3; i++) {
      float t = now * 0.002f + i * 2.0f;
      float y = MOUTH_Y - fmod(t * 8.0f, 40.0f);
      float x = 64.0f + sinf(t * 3.0f + i) * 20.0f;
      if (y > 0 && y < 60 && x > 2 && x < 126) {
        // Tiny heart: two pixels + triangle
        int hx = (int)x, hy = (int)y;
        display.fillCircle(hx - 2, hy, 2, WHITE);
        display.fillCircle(hx + 2, hy, 2, WHITE);
        display.fillTriangle(hx - 4, hy + 1, hx + 4, hy + 1, hx, hy + 5, WHITE);
      }
    }

    // Purr vibration indicator — small waves on sides
    float v = sinf(now * 0.04f) * 2.0f;
    for (int i = 0; i < 3; i++) {
      int y = 30 + i * 10;
      display.drawPixel(2 + (int)(v), y, WHITE);
      display.drawPixel(3 + (int)(v), y, WHITE);
      display.drawPixel(125 - (int)(v), y, WHITE);
      display.drawPixel(124 - (int)(v), y, WHITE);
    }
  }

  if (purrStopSurprise) {
    // "?!" above head
    display.setTextSize(1);
    display.setCursor(58, 0);
    display.print("?!");
  }
}

// ================= NOTIFICATION JEALOUSY =================
void updateJealousy(unsigned long now) {
  if (!isJealous)
    return;

  if (now > jealousyEnd) {
    isJealous = false;
    jealousyPhase = 0;
    return;
  }

  unsigned long elapsed = jealousyEnd - now;
  // Phase progression: narrow(1s) → curious(1s) → sulky/excited(1s)
  if (elapsed > 2000)
    jealousyPhase = 0; // narrow eyes
  else if (elapsed > 1000)
    jealousyPhase = 1; // curious big eyes
  else
    jealousyPhase = 2; // sulky or excited based on affection

  if (jealousyPhase == 0) {
    // Narrow eyes — suspicious of who's messaging
    isSuspicious = true;
    suspiciousEndTime = now + 500;
  } else if (jealousyPhase == 1) {
    // Curious — big shocked eyes
    isSuspicious = false;
    ovShocked = true;
    ovShockedEnd = now + 500;
  }
}

// ================= WAKE-UP DRAMA =================
void startWakeUpDrama() {
  unsigned long now = millis();
  wakeUpPhase = 1;
  wakePhaseEnd = now + 500;
  // Phase 1: startled — wide eyes
  ovShocked = true;
  ovShockedEnd = now + 500;
  transitionFlash = 0.9f;
  buzzerPlay(SND_WAKE);
  Serial.println("Wake-up drama phase 1: STARTLED");
}

void updateWakeUpDrama(unsigned long now) {
  if (wakeUpPhase == 0)
    return;

  if (now > wakePhaseEnd) {
    wakeUpPhase++;

    if (wakeUpPhase == 2) {
      // Phase 2: grumpy squint
      wakePhaseEnd = now + 1500;
      ovShocked = false;
      isPuppySquint = true;
      squintEndTime = now + 1500;
      // Grumpy mouth
      ovDisgusted = true;
      ovDisgustedEnd = now + 1500;
      Serial.println("Wake-up drama phase 2: GRUMPY");
    } else if (wakeUpPhase == 3) {
      // Phase 3: big yawn
      wakePhaseEnd = now + 2500;
      isPuppySquint = false;
      ovDisgusted = false;
      triggerYawn(2500);
      Serial.println("Wake-up drama phase 3: YAWN");
    } else if (wakeUpPhase == 4) {
      // Phase 4: fully awake — normal
      wakeUpPhase = 0;
      currentEyeOpenFactor = 1.0f;
      targetEyeOpenFactor = 1.0f;
      // Brief happy/alive expression
      if (affectionScore > 50) {
        ovEmbarrassed = true;
        ovEmbarrassedEnd = now + 1500;
      }
      Serial.println("Wake-up drama done: ALIVE!");
    }
  }
}

// ================= BOREDOM STATE MACHINE =================
// ================= HYDRATION REMINDER =================
void updateHydration(unsigned long now) {
  if (isDying || isSleeping || isIncomingCall || callActive)
    return;
  if (hydrationCelebration && now > celebrationEnd)
    hydrationCelebration = false;

  // Initialize timer on first run
  if (lastHydrationTime == 0)
    lastHydrationTime = now;

  // Check if 45 min passed since last hydration
  if (!isThirsty && (now - lastHydrationTime > HYDRATION_INTERVAL)) {
    isThirsty = true;
    thirstyStart = now;
    thirstyUrgency = 0;
    buzzerPlay(SND_REMIND); // reminder beeps
    Serial.println("Hydration reminder! Drink water!");
  }

  // Escalate urgency if ignored
  if (isThirsty) {
    unsigned long thirstDur = now - thirstyStart;

    // Auto-dismiss after 15 seconds — no tap needed
    if (thirstDur > 15000) {
      isThirsty = false;
      lastHydrationTime = now;
      Serial.println("Hydration reminder dismissed. Next in 45min.");
    }
  }
}

void drawHydrationOverlay(unsigned long now) {
  // Top banner
  display.fillRect(0, 0, 128, 14, BLACK);
  display.drawRect(0, 0, 128, 14, WHITE);
  display.setTextSize(1);
  centerText("Drink water!", 3, 1);

  // Water droplet icon — bottom right, bouncing
  int dropY = 48 + (int)(sinf(now * 0.008f) * 4);
  drawSweatDrop(110, dropY);

  // Gentle blinking hint
  display.setTextSize(1);
  if ((now / 1000) % 2 == 0)
    centerText("stay hydrated~", 56, 1);
}

// ================= POSTURE CHECK =================
void updatePosture(unsigned long now) {
  if (isDying || isSleeping || isIncomingCall || callActive)
    return;
  if (postureAcked && now > postureAckEnd)
    postureAcked = false;

  // Initialize timer on first run
  if (lastPostureTime == 0)
    lastPostureTime = now;

  // Check if 30 min passed
  if (!isPostureReminder && !postureAcked &&
      (now - lastPostureTime > POSTURE_INTERVAL)) {
    isPostureReminder = true;
    postureStart = now;
    buzzerPlay(SND_REMIND); // posture reminder beeps
    Serial.println("Posture check! Sit up straight!");
  }

  // Auto-dismiss after 15 seconds (no tap required)
  if (isPostureReminder && (now - postureStart > 15000)) {
    isPostureReminder = false;
    lastPostureTime = now;
    Serial.println("Posture reminder dismissed (timed out).");
  }
}

void drawPostureOverlay(unsigned long now) {
  // Bottom banner
  display.fillRect(0, 50, 128, 14, BLACK);
  display.drawRect(0, 50, 128, 14, WHITE);
  display.setTextSize(1);
  centerText("Sit up straight!", 53, 1);

  // Exclamation marks bouncing at sides
  int bounce = (int)(sinf(now * 0.01f) * 3);
  drawExclamation(8, 18 + bounce);
  drawExclamation(118, 18 - bounce);
}

// ================= DRAMATIC DEATH =================
void startDramaticDeath() {
  isDying = true;
  deathPhase = 1;
  deathPhaseEnd = millis() + 1500;
  buzzerPlay(SND_DEATH); // dramatic death sound // Phase 1: shock (1.5s)
  Serial.println("DRAMATIC DEATH SEQUENCE INITIATED!");
}

void updateDramaticDeath(unsigned long now) {
  // Check if affection hit rock bottom
  if (!isDying && affectionScore <= -100) {
    startDramaticDeath();
    return;
  }

  if (!isDying)
    return;

  if (now > deathPhaseEnd) {
    deathPhase++;
    switch (deathPhase) {
    case 2: // Spiral dizzy
      deathPhaseEnd = now + 2000;
      Serial.println("Death phase 2: SPIRALS");
      break;
    case 3: // Flicker/static
      deathPhaseEnd = now + 1500;
      Serial.println("Death phase 3: FLICKER");
      break;
    case 4: // Flatline
      deathPhaseEnd = now + 2500;
      Serial.println("Death phase 4: FLATLINE");
      break;
    case 5: // Reboot
      deathPhaseEnd = now + 2000;
      Serial.println("Death phase 5: REBOOTING");
      break;
    default:
      // Fully rebooted — amnesia!
      isDying = false;
      deathPhase = 0;
      affectionScore = 0;
      hasAmnesia = true;
      amnesiaEnd = now + 5000;
      buzzerPlay(SND_CONFUSED);
      triggerModeChange(0);
      ovShocked = true;
      ovShockedEnd = now + 2000;
      isBored = false;
      boredStage = 0;
      needsConsoling = false;
      Serial.println("REBOOT COMPLETE. Amnesia active. Affection reset to 0.");
      break;
    }
  }
}

void drawDeathScene(unsigned long now) {
  display.fillRect(0, 0, 128, 64, BLACK);

  if (deathPhase == 1) {
    // Phase 1: SHOCK — wide X eyes, shaking
    int shake = (int)(sinf(now * 0.05f) * 4);
    display.setTextSize(2);
    display.setCursor(20 + shake, 10);
    display.print("X");
    display.setCursor(90 + shake, 10);
    display.print("X");
    // Open mouth scream
    display.drawCircle(64 + shake, 42, 8, WHITE);
    // "NOOO" text
    if ((now / 200) % 2 == 0) {
      display.setTextSize(1);
      centerText("NOOOOO!!", 56, 1);
    }
  } else if (deathPhase == 2) {
    // Phase 2: SPIRALS — dizzy death spirals
    float rot = now * 0.01f;
    drawSpiral(32, 20, 1, rot);
    drawSpiral(96, 20, -1, rot);
    drawSpiral(64, 40, 1, rot + 1.0f);
    // Shaking text
    int shake = (int)(sinf(now * 0.03f) * 3);
    display.setTextSize(1);
    display.setCursor(30 + shake, 56);
    display.print("*dying noises*");
  } else if (deathPhase == 3) {
    // Phase 3: FLICKER — screen static/noise
    for (int i = 0; i < 80; i++) {
      int x = random(0, 128);
      int y = random(0, 64);
      display.drawPixel(x, y, WHITE);
    }
    // Flicker the X eyes
    if ((now / 100) % 3 != 0) {
      display.setTextSize(2);
      display.setCursor(20, 15);
      display.print("X   X");
    }
  } else if (deathPhase == 4) {
    // Phase 4: FLATLINE — heartbeat monitor going flat
    display.drawLine(0, 32, 128, 32, WHITE); // flat line
    // One last blip
    float progress = (float)(now - (deathPhaseEnd - 2500)) / 2500.0f;
    if (progress < 0.3f) {
      int blipX = (int)(progress * 128 / 0.3f);
      display.drawLine(blipX, 32, blipX + 5, 20, WHITE);
      display.drawLine(blipX + 5, 20, blipX + 10, 44, WHITE);
      display.drawLine(blipX + 10, 44, blipX + 15, 32, WHITE);
    }
    display.setTextSize(1);
    centerText("...", 50, 1);
  } else if (deathPhase == 5) {
    // Phase 5: REBOOT — loading bar
    float progress = (float)(now - (deathPhaseEnd - 2000)) / 2000.0f;
    if (progress > 1.0f)
      progress = 1.0f;

    display.setTextSize(1);
    centerText("REBOOTING...", 15, 1);

    // Loading bar
    display.drawRect(14, 30, 100, 10, WHITE);
    int barW = (int)(96 * progress);
    if (barW > 0)
      display.fillRect(16, 32, barW, 6, WHITE);

    // Percentage
    char pctStr[6];
    sprintf(pctStr, "%d%%", (int)(progress * 100));
    centerText(pctStr, 45, 1);

    // Memory wipe text
    if (progress > 0.5f) {
      centerText("memory: cleared", 55, 1);
    }
  }
}

void updateBoredom(unsigned long now) {
  unsigned long idle = now - lastInteractionTime;

  // Don't run boredom while dancing, sleeping, on a call, or in clock/weather
  if (isDancing || isSleeping || isIncomingCall || callActive ||
      currentMode == 8 || currentMode == 9) {
    // Reset boredom if any of these are active
    if (isBored) {
      isBored = false;
      boredStage = 0;
      boredStartTime = 0;
      isFurious = false;
      Serial.println("Boredom reset (active state)");
    }
    return;
  }

  // --- ENTRY: 15 min idle → become bored (stage 1) ---
  if (!isBored && idle > BORED_TIMEOUT) {
    isBored = true;
    boredStage = 1;
    boredStartTime = now;
    // Transition: smug dismissive look — "are you seriously ignoring me?"
    isSuspicious = true;
    suspiciousEndTime = now + 2500;
    ovDisgusted = true;
    ovDisgustedEnd = now + 3000;
    smoothModeChange(7, 800); // → smug
    transitionFlash = 0.8f;
    buzzerPlay(SND_BORED);
    changeAffection(-3, "you're ignoring me!");
    Serial.println("Bot is BORED (stage 1)");
  }

  if (!isBored)
    return;

  unsigned long boredFor = now - boredStartTime;

  // --- STAGE 2: 2 min of being bored → angry ---
  if (boredStage == 1 && boredFor > BORED_ANGRY_AT) {
    boredStage = 2;
    ovShocked = true;
    ovShockedEnd = now + 400;
    ovDisgusted = true;
    ovDisgustedEnd = now + 1500;
    smoothModeChange(2, 600); // → angry
    transitionFlash = 1.0f;
    buzzerPlay(SND_ANGRY);
    changeAffection(-5, "still ignoring me!!");
    Serial.println("Bot is ANGRY (bored stage 2)");
  }

  // --- STAGE 3: 5 min of being bored → furious ---
  if (boredStage == 2 && boredFor > BORED_FURIOUS_AT) {
    boredStage = 3;
    isFurious = true;
    ovPain = true;
    ovPainEnd = now + 600;
    smoothModeChange(2, 400);
    transitionFlash = 1.0f;
    changeAffection(-8, "FINE I DONT CARE");
    Serial.println("Bot is FURIOUS (bored stage 3)");
  }

  // --- PERIODIC EYE ROLLS while bored (stage 1) ---
  if (boredStage == 1 && now - lastBoredEyeRoll > 8000) {
    lastBoredEyeRoll = now;
    // Random bored gesture
    int r = random(0, 3);
    if (r == 0) {
      isSuspicious = true;
      suspiciousEndTime = now + 2000;
    } else if (r == 1) {
      ovThinking = true;
      ovThinkingEnd = now + 2500;
    } else {
      ovSleepy = true;
      ovSleepyEnd = now + 2000;
      sleepDroop = 0;
    }
  }

  // --- STAGE 2/3: Angry twitching fire ---
  if (boredStage >= 2) {
    isFurious = (boredStage == 3);
  }
}

void triggerModeChange(int m) {
  currentMode = m;
  moodStartedAt = millis(); // track when this mood started for cooldown
  // Consoling: entering sad mode requires comforting; leaving clears it
  if (m == 3) {
    needsConsoling = true;
  } else {
    needsConsoling = false;
    isBeingConsoled = false;
  }
  isPuppySquint = false;
  isBeingPetted = false;
  isRejected = false;
  isFurious = false;
  isComforted = false;
  isBeingLoved = false;
  // Clear multi-phase systems that shouldn't persist across mode changes
  isForgiving = false;
  forgivePhase = 0;
  isStartled = false;
  startlePhase = 0;
  isDoingQuirk = false;
  isYawning = false;
  currentYawnFactor = targetYawnFactor = 0;
  if (isPurring) {
    isPurring = false;
    buzzerStop();
  }
  isWinking = false;
  isSurprised = false;
  isLaughing = false;
  isSuspicious = false;
  isCryingHard = false;
  isSmugWink = false;
  isSleeping = false;
  isDriftingOff = false;
  hasMidYawned = false;
  hasFinalYawned = false;
  currentEyeOpenFactor = targetEyeOpenFactor = 1.0f;
  bounceY = 0;
  laughShake = 0;
  scaredShake = 0;
  tearY = 0;
  // Reset eye look direction so it doesn't freeze
  lookDirection = 0;
  targetEyeW_L = BASE_EYE_W;
  targetEyeW_R = BASE_EYE_W;
  targetMouthX = 0;
  // Clear overlays only if not mid-beat and not in transition
  // (transition overlays like embarrassed/shocked are set BEFORE
  // triggerModeChange
  //  so we must not clear them here — we check if they were just set)
  if (beatStage == 0) {
    // Only clear overlays that weren't just freshly set (within last 100ms)
    unsigned long now = millis();
    if (!ovShocked || ovShockedEnd < now + 500)
      ovShocked = false;
    if (!ovEmbarrassed || ovEmbarrassedEnd < now + 1500)
      ovEmbarrassed = false;
    if (!ovSleepy || ovSleepyEnd < now + 2500) {
      ovSleepy = false;
      sleepDroop = 0;
    }
    ovNervous = false;
    nervShake = 0;
    ovDisgusted = false;
    ovThinking = false;
    ovHyper = false;
    ovPain = false;
    ovMelting = false;
    ovCryLaugh = false;
    cryLaughShake = 0;
    meltDroop = 0;
    hyperPupilX = 0;
    hyperPupilY = 0;
  }
}

// ================= BLE DISCONNECT CLEANUP =================
// Called when BLE drops — cancel everything that depends on the phone.
// Without this, the bot stays stuck on call screens, music, or dancing forever.
void onBLEDisconnectCleanup() {
  // Cancel active calls
  if (isIncomingCall || callActive) {
    isIncomingCall = false;
    callActive = false;
    callEnded = false;
    buzzerStop();
    triggerModeChange(0);
    Serial.println("BLE cleanup: cancelled active call");
  }
  // Stop dancing (music was playing from phone)
  if (isDancing) {
    stopDancing();
    Serial.println("BLE cleanup: stopped dancing");
  }
  // Clear music state
  musicPlaying = false;
  showMusicPopup = false;
  // Clear notification
  showNotif = false;
  // Clear jealousy (requires BLE notification)
  isJealous = false;
  jealousyEnd = 0;
  Serial.println("BLE cleanup: all phone-dependent states cleared");
}

// Minimal JSON field extractor — no ArduinoJson needed
String gbGet(const String &json, const char *key) {
  String search = String('"') + key + '"' + ':' + '"';
  int i = json.indexOf(search);
  if (i == -1)
    return "";
  i += search.length();
  int e = json.indexOf('"', i);
  if (e == -1)
    return "";
  return json.substring(i, e);
}

// Get numeric value from JSON (no quotes around value)
float gbGetNum(const String &json, const char *key) {
  String search = String('"') + key + "\":";
  int i = json.indexOf(search);
  if (i == -1)
    return -999;
  i += search.length();
  while (i < (int)json.length() && json[i] == ' ')
    i++;
  String val = "";
  while (i < (int)json.length()) {
    char c = json[i];
    if (isdigit(c) || c == '-' || c == '.') {
      val += c;
      i++;
    } else
      break;
  }
  if (val.length() == 0)
    return -999;
  return val.toFloat();
}

// Convert OpenWeatherMap codes to our icon types (0-6)
// OWM: 8xx=clear/clouds, 7xx=atmosphere, 6xx=snow, 5xx=rain, 3xx=drizzle,
// 2xx=thunder
int convertOWMCode(int owm) {
  if (owm == 800)
    return 0; // clear
  if (owm == 801 || owm == 802)
    return 1; // partly cloudy
  if (owm >= 803 && owm <= 804)
    return 2; // cloudy
  if (owm >= 700 && owm <= 799)
    return 3; // fog/mist/haze
  if (owm >= 300 && owm <= 399)
    return 4; // drizzle
  if (owm >= 500 && owm <= 599)
    return 4; // rain
  if (owm >= 600 && owm <= 699)
    return 6; // snow
  if (owm >= 200 && owm <= 299)
    return 5; // thunder
  return 2;
}

void parseGBPacket(String json) {
  Serial.println("Parsing: " + json);
  if (json.length() == 0 || json[0] != '{')
    return;

  String t = gbGet(json, "t");
  Serial.println("Type: " + t);

  // Use notification id as time fallback — it's a unix timestamp!
  if (!timeReady) {
    float idVal = gbGetNum(json, "id");
    if (idVal > 1000000000) { // sanity: valid unix timestamp
      time_t epoch = (time_t)idVal;
      struct timeval tv = {epoch, 0};
      settimeofday(&tv, NULL);
      timeReady = true;
      Serial.println("Time set from packet id: " + String((long)epoch));
    }
  }

  if (t == "is_gps_active" || t == "find" || t == "alarm") {
    // Alarm reaction
    if (t == "alarm") {
      if (isSleeping) {
        isSleeping = false;
        isDriftingOff = false;
        targetEyeOpenFactor = 1.0f;
        currentMode = 0;
        hasMidYawned = false;
        hasFinalYawned = false;
      }
      triggerModeChange(6);
      ovShocked = true;
      ovShockedEnd = millis() + 1000;
    }
    return;
  }
  // ---- PHONE BATTERY from Gadgetbridge ----
  if (t == "status" || t == "battery" || t == "bat") {
    float bat = gbGetNum(json, "bat");
    if (bat > -999 && bat >= 0 && bat <= 100) {
      phoneBattery = (int)bat;
      Serial.println("Phone battery: " + String(phoneBattery) + "%");
    }
    return;
  }
  if (t == "call") {
    String cmd = gbGet(json, "cmd");
    if (cmd == "incoming") {
      String name = gbGet(json, "name");
      String num = gbGet(json, "number");
      if (name.length() == 0)
        name = num;
      if (name.length() == 0)
        name = "Unknown";
      strncpy(callerName, name.c_str(), 31);
      callerName[31] = '\0';
      strncpy(callerNumber, num.c_str(), 19);
      callerNumber[19] = '\0';
      strncpy(callSource, "Phone", 15);
      callSource[15] = '\0';
      isIncomingCall = true;
      callActive = false;
      callRingStart = millis();
      callEnded = false;
      callWasAnswered = false;
      buzzerPlay(SND_RING, true, CALL_RING_TIMEOUT); // looping ringtone
      // Wake from sleep/dancing
      if (isSleeping) {
        isSleeping = false;
        isDriftingOff = false;
        targetEyeOpenFactor = 1.0f;
        hasMidYawned = false;
        hasFinalYawned = false;
      }
      if (isDancing)
        stopDancing();
      triggerModeChange(6); // scared — "something is ringing!"
      ovShocked = true;
      ovShockedEnd = millis() + 600;
      lastInteractionTime = millis();
      Serial.println("Incoming call from: " + name);
    } else if (cmd == "accept" || cmd == "outgoing" || cmd == "start") {
      // Read caller info — outgoing calls also send name/number
      String name = gbGet(json, "name");
      String num = gbGet(json, "number");
      if (name.length() == 0)
        name = num;
      if (name.length() > 0) {
        strncpy(callerName, name.c_str(), 31);
        callerName[31] = '\0';
        strncpy(callerNumber, num.c_str(), 19);
        callerNumber[19] = '\0';
      }
      // If still no name from this packet, keep the incoming call's name
      // (accept case)
      callActive = true;
      isIncomingCall = false;
      callWasAnswered = true;
      callEnd = millis();
      buzzerStop();            // stop ringtone
      buzzerPlay(SND_CALL_OK); // call accepted chime
      strncpy(callSource, "Phone", 15);
      callSource[15] = '\0';
      triggerModeChange(1); // love — happy to talk
      ovShocked = true;
      ovShockedEnd = millis() + 500;
      Serial.println("Call active: " + String(callerName));
    } else if (cmd == "end" || cmd == "reject") {
      if (isIncomingCall || callActive) {
        isIncomingCall = false;
        callActive = false;
        callEnded = true;
        callReactEnd = millis() + 2500;
        if (callWasAnswered) {
          triggerModeChange(0);
          ovEmbarrassed = true;
          ovEmbarrassedEnd = millis() + 2000;
        } else {
          triggerModeChange(3);
        } // sad — missed the call
      }
      buzzerStop();             // stop any ringtone
      buzzerPlay(SND_CALL_END); // call end tone
      Serial.println("Call ended/rejected");
    }
    return;
  }
  if (t == "notify-")
    return; // notification dismissed — ignore

  if (t == "musicinfo") {
    String track = gbGet(json, "track");
    String artist = gbGet(json, "artist");
    if (track.length() == 0)
      track = gbGet(json, "title");
    strncpy(musicTrack, track.c_str(), 63);
    musicTrack[63] = '\0';
    strncpy(musicArtist, artist.c_str(), 63);
    musicArtist[63] = '\0';
    String line = track + " - " + artist;
    wrapTextToLines(line.c_str(), musicLine1, musicLine2);
    // Parse song personality for dynamic dance behaviour
    parseSongPersonality(musicTrack, musicArtist);
    showMusic = true;
    musicEnd = millis() + 30000; // extend timer on new track
    showMusicPopup = true;
    musicPopupEnd = millis() + MUSIC_POPUP_DURATION;
    musicPlaying = true;
    if (!isDancing)
      startDancing(); // only start if not already dancing
    Serial.println("Music: " + line);
  } else if (t == "musicstate") {
    String state = gbGet(json, "state");
    bool wasPlaying = musicPlaying;
    musicPlaying = (state == "play");
    Serial.println("Music state: " + state);

    if (musicPlaying) {
      // PLAY — wake up and dance
      showMusic = true;
      musicEnd = millis() + 30000;
      showMusicPopup = true;
      musicPopupEnd = millis() + MUSIC_POPUP_DURATION;
      if (!isDancing)
        startDancing();
    } else {
      // PAUSE/STOP — stop dancing, show face
      showMusicPopup = true;
      musicPopupEnd = millis() + MUSIC_POPUP_DURATION;
      if (isDancing)
        stopDancing();
    }
  } else if (t == "weather") {
    // Gadgetbridge sends temp in Kelvin — convert to Celsius
    float tempK = gbGetNum(json, "temp");
    if (tempK > -999 && tempK > 200) { // sanity check — Kelvin is always >200
      outdoorTemp = tempK - 273.15f;
      weatherReady = true;
      lastWeatherUpdate = millis();
    }
    // Weather condition code (OpenWeatherMap codes)
    float code = gbGetNum(json, "code");
    if (code > -999)
      weatherCode = (int)convertOWMCode((int)code);
    // Location name from Gadgetbridge
    String loc = gbGet(json, "loc");
    if (loc.length() > 0) {
      strncpy(weatherCity, loc.c_str(), 31);
      weatherCity[31] = '\0';
      Serial.println("Weather location: " + loc);
    }
    Serial.println("GB Weather: " + String(outdoorTemp) + "C code:" +
                   String(weatherCode) + " loc:" + String(weatherCity));
  } else if (t == "notify") {
    String src = gbGet(json, "src");
    if (src.length() == 0)
      src = gbGet(json, "sender");
    if (src.length() == 0)
      src = gbGet(json, "app");
    String body = gbGet(json, "title");
    if (body.length() == 0)
      body = gbGet(json, "subject");
    if (body.length() == 0)
      body = gbGet(json, "body");
    if (body.length() == 0)
      body = gbGet(json, "message");
    if (src.length() == 0)
      src = "Alert";
    if (body.length() == 0)
      body = "New notification";
    // Sanitize text to ASCII-only (OLED can't display UTF-8/emojis)
    String cleanBody = "";
    for (unsigned int i = 0; i < body.length(); i++) {
      char c = body.charAt(i);
      if (c >= 32 && c <= 126)
        cleanBody += c; // printable ASCII only
      else if (c == '\n')
        cleanBody += ' '; // newline → space
    }
    if (cleanBody.length() == 0)
      cleanBody = "New notification";
    String cleanSrc = "";
    for (unsigned int i = 0; i < src.length(); i++) {
      char c = src.charAt(i);
      if (c >= 32 && c <= 126)
        cleanSrc += c;
    }
    if (cleanSrc.length() == 0)
      cleanSrc = "Alert";

    strncpy(notifApp, cleanSrc.c_str(), 31);
    notifApp[31] = '\0';
    wrapTextToLines(cleanBody.c_str(), notifLine1, notifLine2);

    // ---- DETECT APP CALLS (WhatsApp/Telegram/Discord voice/video calls) ----
    String bodyLower = cleanBody;
    bodyLower.toLowerCase();
    String srcLower2 = cleanSrc;
    srcLower2.toLowerCase();
    bool isAppCall = false;
    if (bodyLower.indexOf("incoming") >= 0 || bodyLower.indexOf("call") >= 0 ||
        bodyLower.indexOf("ringing") >= 0 || bodyLower.indexOf("audio") >= 0 ||
        bodyLower.indexOf("video") >= 0 || bodyLower.indexOf("voice") >= 0) {
      // Check if it's from a calling app
      if (srcLower2.indexOf("whatsapp") >= 0 ||
          srcLower2.indexOf("telegram") >= 0 ||
          srcLower2.indexOf("discord") >= 0 || srcLower2.indexOf("duo") >= 0 ||
          srcLower2.indexOf("meet") >= 0 || srcLower2.indexOf("teams") >= 0 ||
          srcLower2.indexOf("skype") >= 0 || srcLower2.indexOf("zoom") >= 0 ||
          srcLower2.indexOf("signal") >= 0 ||
          srcLower2.indexOf("messenger") >= 0) {
        isAppCall = true;
      }
    }

    if (isAppCall) {
      // Treat as incoming call — show call screen instead of notification
      strncpy(callerName, cleanBody.c_str(), 31);
      callerName[31] = '\0';
      // Extract app name for call source
      if (srcLower2.indexOf("whatsapp") >= 0)
        strncpy(callSource, "WhatsApp", 15);
      else if (srcLower2.indexOf("telegram") >= 0)
        strncpy(callSource, "Telegram", 15);
      else if (srcLower2.indexOf("discord") >= 0)
        strncpy(callSource, "Discord", 15);
      else if (srcLower2.indexOf("teams") >= 0)
        strncpy(callSource, "Teams", 15);
      else if (srcLower2.indexOf("zoom") >= 0)
        strncpy(callSource, "Zoom", 15);
      else
        strncpy(callSource, cleanSrc.c_str(), 15);
      callSource[15] = '\0';

      isIncomingCall = true;
      callActive = false;
      callRingStart = millis();
      callEnded = false;
      callWasAnswered = false;
      buzzerPlay(SND_RING, true, CALL_RING_TIMEOUT); // looping ringtone
      showNotif = false; // don't show notification, show call screen instead
      if (isSleeping) {
        isSleeping = false;
        isDriftingOff = false;
        targetEyeOpenFactor = 1.0f;
        hasMidYawned = false;
        hasFinalYawned = false;
      }
      if (isDancing)
        stopDancing();
      triggerModeChange(6);
      ovShocked = true;
      ovShockedEnd = millis() + 600;
      lastInteractionTime = millis();
      Serial.println("APP CALL from " + String(callSource) + ": " + cleanBody);
      return;
    }

    showNotif = true;
    notifEnd = millis() + NOTIF_DURATION;
    lastInteractionTime = millis(); // reset idle timer — stops yawn/sleep/attention sounds

    // Sound cooldown — Gadgetbridge sends multiple packets per notification
    // Only ding once per 2 seconds to avoid repeated buzzing
    static unsigned long lastNotifSoundAt = 0;
    if (millis() - lastNotifSoundAt > 2000) {
      buzzerPlay(SND_NOTIF); // notification beep
      lastNotifSoundAt = millis();
    }

    // ---- MOOD REACTION BASED ON APP ----
    // Wake up from sleep/clock if needed
    if (isSleeping) {
      isSleeping = false;
      isDriftingOff = false;
      targetEyeOpenFactor = 1.0f;
      currentMode = 0;
      hasMidYawned = false;
      hasFinalYawned = false;
    }

    String srcLower = cleanSrc;
    srcLower.toLowerCase();

    // Jealousy trigger: if bot was in love/happy mode when notification arrives
    if (currentMode == 1) {
      isJealous = true;
      jealousyEnd = millis() + 3000;
      jealousyPhase = 0;
    }

    if (srcLower.indexOf("whatsapp") >= 0 ||
        srcLower.indexOf("telegram") >= 0 || srcLower.indexOf("message") >= 0) {
      // Message from someone — excited + love
      triggerModeChange(1);
      ovShocked = true;
      ovShockedEnd = millis() + 600;
    } else if (srcLower.indexOf("instagram") >= 0 ||
               srcLower.indexOf("twitter") >= 0 ||
               srcLower.indexOf("facebook") >= 0) {
      // Social media — excited
      triggerModeChange(5);
      ovShocked = true;
      ovShockedEnd = millis() + 500;
    } else if (srcLower.indexOf("gmail") >= 0 ||
               srcLower.indexOf("email") >= 0 ||
               srcLower.indexOf("mail") >= 0) {
      // Email — thinking/smug
      triggerModeChange(7);
      ovThinking = true;
      ovThinkingEnd = millis() + 3000;
    } else if (srcLower.indexOf("alarm") >= 0 ||
               srcLower.indexOf("reminder") >= 0) {
      // Alarm — scared/surprised
      triggerModeChange(6);
      ovShocked = true;
      ovShockedEnd = millis() + 800;
    } else if (srcLower.indexOf("battery") >= 0 ||
               srcLower.indexOf("error") >= 0) {
      // Warning — nervous
      triggerModeChange(0);
      ovNervous = true;
      ovNervousEnd = millis() + 2500;
    } else {
      // Generic — just shocked briefly
      ovShocked = true;
      ovShockedEnd = millis() + 800;
    }
    Serial.println("Notif from: " + src + " body: " + body);
  }
} // end parseGBPacket

// ================= DANCE SYSTEM =================
void startDancing() {
  // Always wake from sleep/clock when music plays
  if (isSleeping || currentMode == 8) {
    isSleeping = false;
    isDriftingOff = false;
    targetEyeOpenFactor = 1.0f;
    hasMidYawned = false;
    hasFinalYawned = false;
  }
  // Switch to excited mood for dancing
  triggerModeChange(5);
  isDancing = true;
  // Apply song personality — start on personality's chosen style
  danceStyle = (songP.startStyle >= 0) ? songP.startStyle : 0;
  danceStyleStart = millis();
  for (int i = 0; i < 6; i++)
    notes[i].active = false;
  for (int i = 0; i < 10; i++)
    sparkles[i].active = false;
  bounceY = 0;
  danceBounceY = 0;
  // Excited flash
  ovShocked = true;
  ovShockedEnd = millis() + 500;
  lastInteractionTime = millis(); // prevent sleep while dancing
  Serial.println("Dancing start!");
}

void stopDancing() {
  isDancing = false;
  showMusic = false;
  for (int i = 0; i < 6; i++)
    notes[i].active = false;

  // Wake up and show a natural "music stopped" reaction
  if (isSleeping) {
    isSleeping = false;
    isDriftingOff = false;
    targetEyeOpenFactor = 1.0f;
    hasMidYawned = false;
    hasFinalYawned = false;
  }

  // Pick mood based on time of day after music stops
  struct tm t;
  bool hasTime = getLocalTime(&t);
  int returnMood = 0;
  if (hasTime) {
    int hr = t.tm_hour;
    if (hr >= 6 && hr < 12)
      returnMood = 0; // morning — calm
    else if (hr >= 12 && hr < 18)
      returnMood = 0; // afternoon — calm
    else if (hr >= 18 && hr < 22)
      returnMood = 1; // evening — love (happy)
    else
      returnMood = 3; // late night — tired/sad
  }
  triggerModeChange(returnMood);

  // Show a "winding down" reaction
  ovSleepy = true;
  ovSleepyEnd = millis() + 2500;
  sleepDroop = 0;
  lastInteractionTime = millis();
  nextMoodChangeAt =
      millis() + random(120000, 300000); // mood change sooner after music
  Serial.println("Dancing stopped, mood=" + String(returnMood));
}

void updateDance(unsigned long now) {
  float t = now * 0.001f;

  // --- CYCLE DANCE STYLES — respect personality's style order and stayOnStyle
  // ---
  if (!songP.stayOnStyle && now - danceStyleStart > DANCE_STYLE_DURATION) {
    // Find current index in styleOrder, advance to next
    int curIdx = 0;
    for (int i = 0; i < 4; i++)
      if (songP.styleOrder[i] == danceStyle) {
        curIdx = i;
        break;
      }
    danceStyle = songP.styleOrder[(curIdx + 1) % 4];
    danceStyleStart = now;
    ovShocked = true;
    ovShockedEnd = now + 300;
    Serial.println("Style -> " + String(danceStyle) + " (" +
                   String(songP.genreTag) + ")");
  }

  // --- BEAT — uses personality beat interval ---
  if (now - lastBeat > songP.beatInterval) {
    lastBeat = now;
    beatPulse = 1.0f;
  }
  beatPulse *= 0.90f;

  // Convenience: beat-frequency in rad/s for sin-based anims
  float beatFreq = 3.14159f * 2.0f / (songP.beatInterval * 0.001f);

  // --- STYLE-SPECIFIC ANIMATIONS (all use songP amplitudes) ---
  if (danceStyle == 0) {
    // GROOVE — smooth bounce, personality sets amplitude
    danceBounceY = sinf(t * beatFreq * 0.5f) * songP.bounceAmp;
    danceHeadTilt = sinf(t * beatFreq * 0.25f) * songP.headAmp;
    float rawMouth = sinf(t * beatFreq * 0.5f);
    danceMouthOpen = rawMouth > 0 ? rawMouth * songP.mouthSensitivity : 0;
    float rawSquint = sinf(t * beatFreq * 0.5f + 1.0f);
    danceEyeSquint = rawSquint > 0 ? rawSquint * songP.eyeSquintDepth : 0;
    danceArmL = sinf(t * beatFreq * 0.5f) * songP.armAmp;
    danceArmR = sinf(t * beatFreq * 0.5f + 3.14159f) * songP.armAmp;

  } else if (danceStyle == 1) {
    // DISCO — sharp jerky bobble, wide arms, amplitude from personality
    float jerk = (beatPulse > 0.5f) ? -songP.bounceAmp : songP.bounceAmp * 0.4f;
    danceBounceY = jerk;
    danceHeadTilt = sinf(t * beatFreq * 0.75f) * songP.headAmp * 1.4f;
    danceMouthOpen = beatPulse * songP.mouthSensitivity;
    danceEyeSquint = beatPulse * songP.eyeSquintDepth;
    danceArmL = sinf(t * beatFreq * 0.5f) * songP.armAmp * 1.3f;
    danceArmR = cosf(t * beatFreq * 0.5f) * songP.armAmp * 1.3f;

  } else if (danceStyle == 2) {
    // WAVE — dreamy sinusoidal, personality tunes how slow/wide
    float waveFreq = beatFreq * 0.3f;
    danceBounceY = sinf(t * waveFreq) * songP.bounceAmp * 1.2f;
    danceHeadTilt = sinf(t * waveFreq * 0.6f) * songP.headAmp * 1.8f;
    float rm = sinf(t * waveFreq + 0.5f);
    danceMouthOpen = rm > 0 ? rm * songP.mouthSensitivity * 0.75f : 0;
    danceEyeSquint = 0.25f + sinf(t * waveFreq) * songP.eyeSquintDepth * 0.6f;
    danceArmL = 18.0f + sinf(t * waveFreq * 1.3f) * songP.armAmp * 1.6f;
    danceArmR =
        -18.0f + sinf(t * waveFreq * 1.3f + 1.57f) * songP.armAmp * 1.6f;

  } else {
    // HYPE — rapid jitter, personality sets chaos level
    float chaos = songP.bounceAmp / 3.0f; // normalized 0-1.5
    float jit = (float)(random(0, 3) - 1);
    danceBounceY = jit * songP.bounceAmp * 0.7f;
    danceHeadTilt = (float)(random(0, 3) - 1) * songP.headAmp * 1.2f;
    danceMouthOpen = (0.5f + jit * 0.3f) * songP.mouthSensitivity;
    danceEyeSquint = sinf(t * beatFreq * 2.5f) * songP.eyeSquintDepth;
    danceArmL = (float)random(-(int)songP.armAmp, (int)songP.armAmp);
    danceArmR = (float)random(-(int)songP.armAmp, (int)songP.armAmp);
  }

  // --- SPAWN MUSIC NOTES — personality controls rate and speed ---
  if (now - lastNoteSpawn > songP.noteInterval) {
    lastNoteSpawn = now;
    for (int i = 0; i < 6; i++) {
      if (!notes[i].active) {
        notes[i].active = true;
        notes[i].x = (random(0, 2) == 0) ? random(2, 14) : random(114, 126);
        notes[i].y = random(20, 50);
        notes[i].vy = songP.noteSpeed + (float)random(0, 6) * 0.01f;
        notes[i].symbol = random(0, 3);
        break;
      }
    }
  }

  // Move notes upward
  for (int i = 0; i < 6; i++) {
    if (notes[i].active) {
      notes[i].y -= notes[i].vy;
      if (notes[i].y < -8)
        notes[i].active = false;
    }
  }
}

void drawMusicNote(int x, int y, int symbol) {
  if (symbol == 0) {
    display.fillCircle(x, y + 3, 3, WHITE);
    display.drawLine(x + 3, y + 3, x + 3, y - 6, WHITE);
  } else if (symbol == 1) {
    display.fillCircle(x, y + 3, 3, WHITE);
    display.drawLine(x + 3, y + 3, x + 3, y - 6, WHITE);
    display.drawLine(x + 3, y - 6, x + 7, y - 3, WHITE);
  } else {
    display.fillCircle(x, y + 3, 2, WHITE);
    display.fillCircle(x + 7, y + 1, 2, WHITE);
    display.drawLine(x + 2, y + 3, x + 2, y - 4, WHITE);
    display.drawLine(x + 9, y + 1, x + 9, y - 6, WHITE);
    display.drawLine(x + 2, y - 4, x + 9, y - 6, WHITE);
  }
}

void drawDanceFace() {
  unsigned long now = millis();
  int by = (int)danceBounceY;
  int ht = (int)danceHeadTilt;

  // ---- STYLE-SPECIFIC BACKGROUND EFFECTS ----
  if (danceStyle == 1) {
    // DISCO — disco ball + sparkles
    drawDisco(now);
  } else if (danceStyle == 3) {
    // HYPE — rapid screen border flicker
    if ((now / 100) % 2 == 0)
      display.drawRect(0, 0, 128, 64, WHITE);
  } else if (danceStyle == 2) {
    // WAVE — sinusoidal line at top
    for (int wx = 0; wx < 128; wx++) {
      int wy = 2 + (int)(sin(wx * 0.15f + now * 0.003f) * 2);
      display.drawPixel(wx, wy, WHITE);
    }
  }

  // ---- MUSIC NOTES — draw first (behind face) ----
  for (int i = 0; i < 6; i++) {
    if (notes[i].active)
      drawMusicNote((int)notes[i].x, (int)notes[i].y, notes[i].symbol);
  }

  // ---- ARMS ----
  float aL = danceArmL * 3.14159f / 180.0f;
  float aR = danceArmR * 3.14159f / 180.0f;
  int aLx1 = 24, aLy1 = 40 + by;
  int aLx2 = aLx1 + (int)(sin(aL + 3.14f) * 16.0f);
  int aLy2 = aLy1 + (int)(cos(aL + 3.14f) * 12.0f) + 4;
  display.drawLine(aLx1, aLy1, aLx2, aLy2, WHITE);
  display.fillCircle(aLx2, aLy2, 2, WHITE);
  int aRx1 = 104, aRy1 = 40 + by;
  int aRx2 = aRx1 + (int)(sin(aR) * 16.0f);
  int aRy2 = aRy1 + (int)(cos(aR) * 12.0f) + 4;
  display.drawLine(aRx1, aRy1, aRx2, aRy2, WHITE);
  display.fillCircle(aRx2, aRy2, 2, WHITE);

  // ---- EYES ----
  int eyeH = EYE_H - (int)(danceEyeSquint * (EYE_H * 0.35f));
  eyeH = max(eyeH, 10);
  int eyeLy = EYE_Y + by + (EYE_H - eyeH) / 2 + (ht > 0 ? ht / 4 : 0);
  int eyeRy = EYE_Y + by + (EYE_H - eyeH) / 2 + (ht < 0 ? -ht / 4 : 0);

  if (danceStyle == 3 && beatPulse > 0.6f) {
    // HYPE — X eyes on beat
    int ey = eyeLy + eyeH / 2;
    display.drawLine(EYE_X_L + ht, ey - 6, EYE_X_L + BASE_EYE_W + ht, ey + 6,
                     WHITE);
    display.drawLine(EYE_X_L + ht, ey + 6, EYE_X_L + BASE_EYE_W + ht, ey - 6,
                     WHITE);
    display.drawLine(EYE_X_R + ht, ey - 6, EYE_X_R + BASE_EYE_W + ht, ey + 6,
                     WHITE);
    display.drawLine(EYE_X_R + ht, ey + 6, EYE_X_R + BASE_EYE_W + ht, ey - 6,
                     WHITE);
  } else if (beatPulse > 0.7f) {
    // Strong beat — star shimmer eyes
    display.fillRoundRect(EYE_X_L + ht, eyeLy, BASE_EYE_W, eyeH, EYE_RADIUS,
                          WHITE);
    display.fillRoundRect(EYE_X_R + ht, eyeRy, BASE_EYE_W, eyeH, EYE_RADIUS,
                          WHITE);
    int starR = (int)(3 + beatPulse * 2);
    drawStar(EYE_X_L + BASE_EYE_W / 2 + ht, eyeLy + eyeH / 2, starR);
    drawStar(EYE_X_R + BASE_EYE_W / 2 + ht, eyeRy + eyeH / 2, starR);
  } else if (danceStyle == 1 && beatPulse > 0.3f) {
    // DISCO — heart eyes on beat
    drawHeart(EYE_X_L + BASE_EYE_W / 2 + ht, eyeLy + eyeH / 2, 0.7f);
    drawHeart(EYE_X_R + BASE_EYE_W / 2 + ht, eyeRy + eyeH / 2, 0.7f);
  } else {
    // Normal happy eyes with shine dot
    display.fillRoundRect(EYE_X_L + ht, eyeLy, BASE_EYE_W, eyeH, EYE_RADIUS,
                          WHITE);
    display.fillRoundRect(EYE_X_R + ht, eyeRy, BASE_EYE_W, eyeH, EYE_RADIUS,
                          WHITE);
    display.fillCircle(EYE_X_L + ht + 6, eyeLy + 5, 2, BLACK);
    display.fillCircle(EYE_X_R + ht + 6, eyeRy + 5, 2, BLACK);
  }

  // Eyebrows
  display.fillRect(EYE_X_L + ht + 3, eyeLy - 5 + (ht > 0 ? -1 : 0),
                   BASE_EYE_W - 6, 2, WHITE);
  display.fillRect(EYE_X_R + ht + 3, eyeRy - 5 + (ht < 0 ? -1 : 0),
                   BASE_EYE_W - 6, 2, WHITE);

  // ---- MOUTH ----
  int my = MOUTH_Y + by;
  if (danceStyle == 2 && danceMouthOpen < 0.15f) {
    // WAVE — peaceful closed eyes smile
    display.fillCircle(64 + ht, my + 6, 10, WHITE);
    display.fillCircle(64 + ht, my + 2, 10, BLACK);
  } else if (danceMouthOpen > 0.15f) {
    int mW = 16 + (int)(danceMouthOpen * 14.0f);
    int mH = 6 + (int)(danceMouthOpen * 10.0f);
    display.fillRoundRect(64 - mW / 2 + ht, my + 2, mW, mH, mH / 2, WHITE);
    display.fillRoundRect(64 - mW / 2 + ht + 2, my + 4, mW - 4, mH - 4,
                          (mH - 4) / 2, BLACK);
    if (mH > 10 && danceMouthOpen > 0.5f) {
      int tw = (mW - 8) / 3;
      for (int i = 0; i < 3; i++)
        display.fillRect(64 - mW / 2 + ht + 4 + i * (tw + 1), my + 4, tw, 4,
                         WHITE);
    }
  } else {
    display.fillCircle(64 + ht, my + 6, 9, WHITE);
    display.fillCircle(64 + ht, my + 2, 9, BLACK);
  }

  // ---- BEAT PULSE BORDER ----
  if (beatPulse > 0.6f) {
    display.drawRoundRect(1, 1, 126, 62, 6, WHITE);
    if (beatPulse > 0.9f)
      display.drawRoundRect(3, 3, 122, 58, 4,
                            WHITE); // double border on strong beat
  }
}

// ================= DRAW CALL SCREEN =================
void drawCallScreen() {
  unsigned long now = millis();
  display.fillRect(0, 0, 128, 64, BLACK);

  if (callActive) {
    // ============ ACTIVE CALL — clean minimal design ============
    // Top bar
    display.fillRect(0, 0, 128, 12, WHITE);
    display.setTextColor(BLACK);
    centerText("ON CALL", 2, 1);
    display.setTextColor(WHITE);

    // Source app (if app call)
    if (strlen(callSource) > 0 && strcmp(callSource, "Phone") != 0) {
      display.setTextSize(1);
      int sw = strlen(callSource) * 6;
      display.setCursor((128 - sw) / 2, 16);
      display.print(callSource);
    }

    // Caller name — big centered
    int nameLen = strlen(callerName);
    if (nameLen <= 10) {
      display.setTextSize(2);
      int nw = nameLen * 12;
      display.setCursor((128 - nw) / 2, 26);
      display.print(callerName);
    } else {
      display.setTextSize(1);
      int nw = min(nameLen, 21) * 6;
      display.setCursor((128 - nw) / 2, 30);
      char truncName[22];
      strncpy(truncName, callerName, 21);
      truncName[21] = '\0';
      display.print(truncName);
    }

    // Elapsed timer at bottom
    unsigned long elapsed = (now - callEnd) / 1000;
    char timerStr[8];
    sprintf(timerStr, "%02lu:%02lu", elapsed / 60, elapsed % 60);
    centerText(timerStr, 50, 1);

    // Subtle pulse border
    float pulse = sinf(now * 0.003f) * 0.5f + 0.5f;
    display.drawRoundRect(0, 0, 128, 64, 4, WHITE);
    if (pulse > 0.5f)
      display.drawRoundRect(1, 1, 126, 62, 3, WHITE);

  } else {
    // ============ INCOMING CALL — animated ringing ============
    // Blinking header
    bool blink = (now / 400) % 2 == 0;
    if (blink) {
      display.fillRect(0, 0, 128, 12, WHITE);
      display.setTextColor(BLACK);
      centerText("INCOMING", 2, 1);
      display.setTextColor(WHITE);
    } else {
      display.drawRect(0, 0, 128, 12, WHITE);
      centerText("INCOMING", 2, 1);
    }

    // Source app label
    if (strlen(callSource) > 0) {
      display.setTextSize(1);
      int sw = strlen(callSource) * 6;
      display.setCursor((128 - sw) / 2, 14);
      display.print(callSource);
    }

    // Caller name — centered, size 2 if fits
    int nameLen = strlen(callerName);
    int nameY = 25;
    if (nameLen <= 10) {
      display.setTextSize(2);
      int nw = nameLen * 12;
      display.setCursor((128 - nw) / 2, nameY);
      display.print(callerName);
    } else {
      display.setTextSize(1);
      int nw = min(nameLen, 21) * 6;
      display.setCursor((128 - nw) / 2, nameY + 4);
      char truncName[22];
      strncpy(truncName, callerName, 21);
      truncName[21] = '\0';
      display.print(truncName);
    }

    // Small ringing phone icon on the left
    float ring = sinf(now * 0.02f);
    int jx = (ring > 0.5f) ? (int)(ring * 2) - 1 : 0;
    // Mini phone shape
    display.drawRoundRect(5 + jx, 44, 10, 14, 2, WHITE);
    display.fillRect(7 + jx, 46, 6, 6, WHITE);
    display.fillCircle(10 + jx, 55, 1, WHITE);
    // Mini ripples
    if ((now / 300) % 3 >= 1)
      display.drawCircle(10 + jx, 50, 9, WHITE);
    if ((now / 300) % 3 >= 2)
      display.drawCircle(10 + jx, 50, 13, WHITE);

    // "ringing..." text with animated dots
    display.setTextSize(1);
    int dots = (int)(now / 350) % 4;
    char ringStr[12] = "ringing";
    for (int i = 0; i < dots; i++) {
      ringStr[7 + i] = '.';
      ringStr[8 + i] = '\0';
    }
    display.setCursor(24, 48);
    display.print(ringStr);

    // Countdown bar at very bottom
    unsigned long elapsed2 = now - callRingStart;
    unsigned long left =
        CALL_RING_TIMEOUT > elapsed2 ? CALL_RING_TIMEOUT - elapsed2 : 0;
    int bw = (int)(124.0f * left / CALL_RING_TIMEOUT);
    if (bw > 0)
      display.fillRect(2, 62, bw, 1, WHITE);

    // Outer animated border
    display.drawRoundRect(0, 0, 128, 64, 4, WHITE);
    if (blink)
      display.drawRoundRect(1, 1, 126, 62, 3, WHITE);
  }
}

// ================= WAVEFORM BAR HELPER =================
void drawWaveform(unsigned long now) {
  // Animate 8 bars that pulse on beat
  if (now - lastWaveUpdate > 80) {
    lastWaveUpdate = now;
    for (int i = 0; i < 8; i++) {
      float target =
          4.0f + (float)sin(now * 0.003f + i * 0.8f) * 3.0f + beatPulse * 8.0f;
      waveformBars[i] += (target - waveformBars[i]) * 0.4f;
    }
  }
  for (int i = 0; i < 8; i++) {
    int h = max(2, (int)waveformBars[i]);
    int bx = 4 + i * 15;
    display.fillRect(bx, 60 - h, 11, h, WHITE);
    display.drawRect(bx, 60 - h, 11, h, WHITE);
  }
}

// ================= DISCO BALL HELPER =================
void drawDisco(unsigned long now) {
  // Rotating disco ball in top-right corner
  int dcx = 108, dcy = 10, dr = 8;
  display.drawCircle(dcx, dcy, dr, WHITE);
  // Grid lines on ball
  for (int i = -dr + 2; i <= dr - 2; i += 3) {
    // horizontal lines
    int hw = (int)sqrt((float)(dr * dr - i * i));
    int sOff = (int)(now / 80) % 3;
    display.drawLine(dcx - hw, dcy + i, dcx + hw, dcy + i, WHITE);
  }
  // Spinning light beams from ball
  float angle = now * 0.003f;
  for (int i = 0; i < 4; i++) {
    float a = angle + i * (3.14159f / 2.0f);
    int ex = dcx + (int)(cos(a) * 22);
    int ey = dcy + (int)(sin(a) * 16);
    if (ex > 0 && ex < 128 && ey > 0 && ey < 50)
      display.drawLine(dcx, dcy, ex, ey, WHITE);
  }
  // Sparkles — spawn and age
  if (now - lastSparkleSpawn > 200) {
    lastSparkleSpawn = now;
    for (int i = 0; i < 10; i++) {
      if (!sparkles[i].active) {
        float a2 = (float)random(0, 628) / 100.0f;
        sparkles[i].x = dcx + (float)cos(a2) * 22;
        sparkles[i].y = dcy + (float)sin(a2) * 16;
        sparkles[i].age = 0;
        sparkles[i].active = true;
        break;
      }
    }
  }
  for (int i = 0; i < 10; i++) {
    if (sparkles[i].active) {
      sparkles[i].age += 0.05f;
      if (sparkles[i].age > 1.0f) {
        sparkles[i].active = false;
        continue;
      }
      int sx = (int)sparkles[i].x, sy = (int)sparkles[i].y;
      if (sx > 0 && sx < 128 && sy > 0 && sy < 55) {
        display.drawPixel(sx, sy, WHITE);
        if (sparkles[i].age < 0.5f) {
          display.drawPixel(sx + 1, sy, WHITE);
          display.drawPixel(sx, sy + 1, WHITE);
        }
      }
    }
  }
}

// ================= NOTE EXPLOSION BURST =================
void drawNoteExplosion(unsigned long now) {
  // Burst of notes flying outward from center on strong beat
  if (beatPulse > 0.85f) {
    for (int i = 0; i < 6; i++) {
      float a = i * (3.14159f / 3.0f) + now * 0.001f;
      float dist = (1.0f - beatPulse) * 40.0f;
      int bx = 64 + (int)(cos(a) * dist);
      int by = 25 + (int)(sin(a) * 18);
      if (bx > 2 && bx < 124 && by > 2 && by < 54)
        drawMusicNote(bx, by, i % 3);
    }
  }
}

// ---- NOTIFICATION fullscreen — smartwatch style ----
void drawNotifOverlay() {
  unsigned long now = millis();
  unsigned long elapsed = now - (notifEnd - NOTIF_DURATION);
  float progress = 1.0f - (float)elapsed / NOTIF_DURATION;

  // Slide-in animation — slides down from top
  int slideY =
      (progress > 0.85f) ? (int)((1.0f - progress) * 5.0f / 0.15f * (-64)) : 0;
  slideY = max(slideY, -64);
  slideY = min(slideY, 0);

  // Full black background
  display.fillRect(0, slideY, 128, 64, BLACK);

  // Top accent bar
  display.fillRect(0, slideY, 128, 3, WHITE);

  // App icon area — small pulsing dot
  int dotR = 3 + (int)(sin(now * 0.01f) * 1.5f);
  display.fillCircle(8, slideY + 12, dotR, WHITE);

  // App name — right of dot, small
  display.setTextSize(1);
  display.setCursor(16, slideY + 8);
  display.print(notifApp);

  // Divider
  display.drawLine(0, slideY + 18, 128, slideY + 18, WHITE);

  // Notification body — auto-size and auto-scroll for long text
  int bodyLen1 = strlen(notifLine1);
  int bodyLen2 = strlen(notifLine2);

  // Scrolling state
  static int nScroll1 = 0, nScroll2 = 0;
  static unsigned long nScrollT = 0;
  if (now - nScrollT > 150) {
    nScroll1++;
    nScroll2++;
    nScrollT = now;
  }

  if (bodyLen2 == 0 && bodyLen1 <= 10) {
    // Short single line — big text, centered
    nScroll1 = 0;
    display.setTextSize(2);
    int nw = bodyLen1 * 12;
    display.setCursor((128 - nw) / 2, slideY + 26);
    display.print(notifLine1);
  } else if (bodyLen2 == 0) {
    // Single line — scroll if wider than screen
    display.setTextSize(1);
    int textW = bodyLen1 * 6;
    if (textW <= 126) {
      nScroll1 = 0;
      centerText(notifLine1, slideY + 28, 1);
    } else {
      int totalW = textW + 30;
      if (nScroll1 >= totalW)
        nScroll1 = 0;
      display.setCursor(2 - nScroll1, slideY + 28);
      display.print(notifLine1);
      display.setCursor(2 - nScroll1 + totalW, slideY + 28);
      display.print(notifLine1);
    }
  } else {
    // Two lines — scroll each independently if needed
    display.setTextSize(1);
    int w1 = bodyLen1 * 6, w2 = bodyLen2 * 6;
    if (w1 <= 126) {
      nScroll1 = 0;
      centerText(notifLine1, slideY + 22, 1);
    } else {
      int tw = w1 + 30;
      if (nScroll1 >= tw)
        nScroll1 = 0;
      display.setCursor(2 - nScroll1, slideY + 22);
      display.print(notifLine1);
      display.setCursor(2 - nScroll1 + tw, slideY + 22);
      display.print(notifLine1);
    }
    if (w2 <= 126) {
      nScroll2 = 0;
      centerText(notifLine2, slideY + 34, 1);
    } else {
      int tw = w2 + 30;
      if (nScroll2 >= tw)
        nScroll2 = 0;
      display.setCursor(2 - nScroll2, slideY + 34);
      display.print(notifLine2);
      display.setCursor(2 - nScroll2 + tw, slideY + 34);
      display.print(notifLine2);
    }
  }

  // Bottom progress bar — drains as time passes
  int bw = (int)(128.0f * progress);
  if (bw > 0)
    display.fillRect(0, slideY + 60, bw, 3, WHITE);

  // Corner brackets for style
  display.drawLine(0, slideY, 6, slideY, WHITE);
  display.drawLine(0, slideY, 0, slideY + 6, WHITE);
  display.drawLine(122, slideY, 128, slideY, WHITE);
  display.drawLine(128, slideY, 128, slideY + 6, WHITE);
}

// ---- MUSIC POPUP fullscreen — play/pause card ----
void drawMusicPopup() {
  unsigned long now = millis();
  unsigned long elapsed = now - (musicPopupEnd - MUSIC_POPUP_DURATION);
  float progress = 1.0f - (float)elapsed / MUSIC_POPUP_DURATION;

  // Fade-out effect at end — blink the border
  bool showBorder = (progress > 0.2f) || ((now / 200) % 2 == 0);

  // Clean full background
  display.fillRect(0, 0, 128, 64, BLACK);

  // Animated music bars on left side — 4 bars
  int frame = (now / 120) % 4;
  int bars[] = {8, 14, 10, 16, 12, 6, 14, 10};
  if (!musicPlaying) {
    // Paused — flat bars
    for (int i = 0; i < 4; i++)
      display.fillRect(4 + i * 5, 44 - 4, 3, 8, WHITE);
  } else {
    for (int i = 0; i < 4; i++) {
      int h = bars[(i + frame) % 8];
      display.fillRect(4 + i * 5, 44 - h / 2, 3, h, WHITE);
    }
  }

  // Play/pause icon on right
  if (musicPlaying) {
    // Triangle play icon
    display.fillTriangle(108, 36, 108, 52, 122, 44, WHITE);
  } else {
    // Pause bars
    display.fillRect(108, 36, 5, 16, WHITE);
    display.fillRect(116, 36, 5, 16, WHITE);
  }

  // Divider line
  display.drawLine(0, 28, 128, 28, WHITE);

  // Track name — big, centered
  display.setTextSize(1);
  String track = String(musicTrack);
  if (track.length() == 0)
    track = "Unknown";

  // Scroll if long
  static int popScrollOff = 0;
  static unsigned long popLastScroll = 0;
  int tlen = track.length();
  if (tlen > 21) {
    if (now - popLastScroll > 180) {
      popScrollOff++;
      if (popScrollOff >= tlen * 6)
        popScrollOff = 0;
      popLastScroll = now;
    }
    display.setCursor(2 - popScrollOff, 10);
    display.print(track);
    if (popScrollOff > 0) {
      display.setCursor(2 - popScrollOff + tlen * 6 + 10, 10);
      display.print(track);
    }
  } else {
    centerText(track.c_str(), 10, 1);
  }

  // Artist name — smaller, below divider
  display.setTextSize(1);
  String artist = String(musicArtist);
  if (artist.length() > 21)
    artist = artist.substring(0, 20);
  centerText(artist.c_str(), 32, 1);

  // Outer rounded border with fade
  if (showBorder)
    display.drawRoundRect(0, 0, 128, 64, 4, WHITE);

  // Progress bar at bottom
  int bw = (int)(128.0f * progress);
  if (bw > 0)
    display.fillRect(0, 61, bw, 3, WHITE);
}

// ---- WEATHER redesigned — reference icon style v6 ----
// Inspired by clean outline icon set: outline sun+rays, 3-bump cloud,
// 6-arm snowflake with branches, bold bolt polygon, wavy fog bands.

// ================= WEATHER DISPLAY =================
int getIconType(int code) {
  if (code == 0)
    return 0; // clear / sunny
  if (code <= 2)
    return 1; // partly cloudy
  if (code == 3)
    return 2; // overcast
  if (code <= 48)
    return 3; // fog
  if (code <= 67 || code <= 82)
    return 4; // rain
  if (code <= 77 || code <= 86)
    return 6; // snow
  if (code <= 99)
    return 5; // thunderstorm
  return 2;
}

// ---- THREE-BUMP CLOUD matching reference icon proportions ----
// cx, cy = cloud center. s = integer scale (1 = default size)
// Bumps: left(medium), center(tallest), right(smaller) — clearly distinct
void cloudShape(int cx, int cy, int s) {
  // Three bump circles
  display.fillCircle(cx - 10 * s, cy, 6 * s, WHITE); // left
  display.fillCircle(cx, cy - 7 * s, 9 * s,
                     WHITE); // center (tallest, ref icon)
  display.fillCircle(cx + 12 * s, cy + s, 5 * s, WHITE); // right
  // Flat body connecting them
  int lE = cx - 16 * s, rE = cx + 17 * s;
  int bodyTop = cy - 3 * s, bY = cy + 5 * s;
  display.fillRect(lE, bodyTop, rE - lE, bY - bodyTop, WHITE);
}

// ---- OUTLINE SUN: circle + 8 rays (reference row-2 col-1 icon) ----
void drawSunIcon(int cx, int cy, int R, float rotAngle) {
  // Clean circle outline
  display.drawCircle(cx, cy, R, WHITE);
  // 8 evenly-spaced rotating rays
  for (int i = 0; i < 8; i++) {
    float a = rotAngle + i * 0.7854f;
    int x1 = cx + (int)(cosf(a) * (R + 2));
    int y1 = cy + (int)(sinf(a) * (R + 2));
    int x2 = cx + (int)(cosf(a) * (R + 8));
    int y2 = cy + (int)(sinf(a) * (R + 8));
    if (y1 < 47 && y2 < 47)
      display.drawLine(x1, y1, x2, y2, WHITE);
  }
}

// ---- 6-ARM SNOWFLAKE with branch nubs (reference row-4 col-2 icon) ----
void drawSnowflake(int cx, int cy, int r) {
  for (int i = 0; i < 6; i++) {
    float a = i * 1.0472f; // π/3 per arm
    int ex = cx + (int)(cosf(a) * r);
    int ey = cy + (int)(sinf(a) * r);
    display.drawLine(cx, cy, ex, ey, WHITE);
    // Branch nubs at 55% along each arm
    int bx = cx + (int)(cosf(a) * r * 0.55f);
    int by_ = cy + (int)(sinf(a) * r * 0.55f);
    int bl = max(1, (int)(r * 0.35f));
    float ba = a + 1.5708f; // +π/2
    display.drawLine(bx + (int)(cosf(ba) * bl), by_ + (int)(sinf(ba) * bl),
                     bx - (int)(cosf(ba) * bl), by_ - (int)(sinf(ba) * bl),
                     WHITE);
    // Tiny tip dot
    display.drawPixel(ex, ey, WHITE);
  }
  display.fillCircle(cx, cy, 1, WHITE);
}

// ---- LIGHTNING BOLT: closed polygon (reference row-1 col-5 icon) ----
// x,y = top-left of bounding box, w=width, h=height
void drawBolt(int x, int y, int w, int h) {
  // Upper-left edge (A→B diagonal)
  display.drawLine(x + w * 38 / 100, y, x, y + h / 2, WHITE);
  // Waist horizontal (B→C)
  display.drawLine(x, y + h / 2, x + w * 32 / 100, y + h / 2, WHITE);
  // Lower-left edge (C→D diagonal)
  display.drawLine(x + w * 32 / 100, y + h / 2, x - w * 4 / 100, y + h, WHITE);
  // Lower-right edge (D→E)
  display.drawLine(x - w * 4 / 100, y + h, x + w * 56 / 100, y + h * 52 / 100,
                   WHITE);
  // Waist right (E→F)
  display.drawLine(x + w * 56 / 100, y + h * 52 / 100, x + w * 24 / 100,
                   y + h * 52 / 100, WHITE);
  // Upper-right edge (F→G)
  display.drawLine(x + w * 24 / 100, y + h * 52 / 100, x + w * 62 / 100, y,
                   WHITE);
  // Close top (G→A)
  display.drawLine(x + w * 62 / 100, y, x + w * 38 / 100, y, WHITE);
  // Interior fill — two triangles make the bolt solid
  display.fillTriangle(x + w * 38 / 100, y, x, y + h / 2, x + w * 5 / 10,
                       y + h / 2, WHITE);
  display.fillTriangle(x + w * 1 / 10, y + h / 2, x - w / 25, y + h,
                       x + w * 52 / 100, y + h * 52 / 100, WHITE);
}

// ---- TINY 4-POINT STAR ----
void drawTinyStar(int cx, int cy, int r) {
  display.drawLine(cx - r, cy, cx + r, cy, WHITE);
  display.drawLine(cx, cy - r, cx, cy + r, WHITE);
  display.drawPixel(cx - r + 1, cy - r + 1, WHITE);
  display.drawPixel(cx + r - 1, cy - r + 1, WHITE);
}

// ---- WAVY FOG LINE ----
void drawFogLine(int y0, float t, float amp, float freq, float phaseOff) {
  for (int x = 0; x < 127; x++) {
    int y = y0 + (int)(amp * sinf(x * freq + t + phaseOff));
    if (y >= 0 && y < 36)
      display.drawPixel(x, y, WHITE);
    if (y + 1 >= 0 && y + 1 < 36)
      display.drawPixel(x, y + 1, WHITE);
  }
}

void drawWeatherFace() {
  unsigned long now = millis();
  float t = now * 0.001f;
  int itype = weatherReady ? getIconType(weatherCode) : -1;

  // ---- NO DATA YET — elegant spinner ----
  if (!weatherReady) {
    display.drawCircle(64, 24, 18, WHITE);
    float oa = t * 2.5f;
    display.fillCircle(64 + (int)(cosf(oa) * 18), 24 + (int)(sinf(oa) * 18), 2,
                       WHITE);
    display.fillCircle(64, 24, 2, WHITE);
    display.setTextSize(1);
    centerText("waiting...", 46, 1);
    centerText(weatherCity[0] ? weatherCity : "", 56, 1);
    return;
  }

  // ===========================================================
  // SCENE AREA: y 0–35 (36px)   INFO STRIP: y 37–63 (27px)
  // ===========================================================

  // ===== 0: SUNNY =====
  if (itype == 0) {
    int cx = 64, cy = 15;
    float pulse = 0.88f + 0.12f * sinf(t * 1.9f);
    int R = (int)(8 * pulse);
    display.drawCircle(cx, cy, R + 5, WHITE);
    display.drawCircle(cx, cy, R + 9, WHITE);
    drawSunIcon(cx, cy, R, t * 0.38f);
    drawTinyStar(18, 8, 3);
    drawTinyStar(105, 6, 2);

    // ===== 1: PARTLY CLOUDY =====
  } else if (itype == 1) {
    drawSunIcon(100, 12, 8, t * 0.33f);
    int cloudX = (int)(fmod(t * 9.0f, 170.0f)) - 42;
    cloudShape(cloudX + 48, 27, 1);
    int wispX = (int)(128.0f - fmod(t * 5.0f + 60.0f, 148.0f));
    if (wispX > -25 && wispX < 128) {
      display.fillCircle(wispX + 8, 14, 3, WHITE);
      display.fillCircle(wispX + 14, 11, 4, WHITE);
      display.fillCircle(wispX + 20, 14, 3, WHITE);
      display.fillRect(wispX + 5, 14, 17, 3, WHITE);
    }

    // ===== 2: OVERCAST =====
  } else if (itype == 2) {
    float s1 = fmod(t * 6.5f, 158.0f) - 18;
    float s2 = fmod(t * 10.0f + 55.0f, 158.0f) - 18;
    float s3 = fmod(t * 8.0f + 28.0f, 158.0f) - 18;
    cloudShape((int)s3 + 26, 5, 1);
    cloudShape((int)s2 + 42, 16, 1);
    cloudShape((int)s1 + 52, 27, 1);

    // ===== 3: FOG =====
  } else if (itype == 3) {
    const float freqs[] = {0.07f, 0.06f, 0.08f, 0.065f, 0.075f, 0.07f};
    const float amps[] = {1.5f, 1.8f, 1.6f, 2.0f, 1.4f, 1.7f};
    const float offs[] = {0, 1.8f, 0.9f, 2.7f, 1.4f, 3.2f};
    const float spds[] = {1.2f, 0.8f, 1.5f, 1.0f, 1.3f, 0.7f};
    // 6 bands compressed into y 4–33
    for (int i = 0; i < 6; i++) {
      int fy = 4 + i * 5;
      drawFogLine(fy, t * spds[i], amps[i], freqs[i], offs[i]);
    }
    if ((now / 900) % 2 == 0) {
      display.setTextSize(2);
      int fw = 3 * 12;
      display.fillRect((128 - fw) / 2 - 2, 10, fw + 4, 18,
                       BLACK); // black backing so text is readable
      display.setCursor((128 - fw) / 2, 11);
      display.print("FOG");
    }

    // ===== 4: RAIN =====
  } else if (itype == 4) {
    cloudShape(64, 14, 1);
    for (int i = 0; i < 22; i++) {
      int rx = (i * 17 + 3) % 118 + 5;
      int ry = (int)(now / 48 + i * 13) % 16 + 20;
      if (ry > 19 && ry < 36)
        display.drawLine(rx, ry, rx - 2, ry + 5, WHITE);
    }
    for (int i = 0; i < 4; i++) {
      int sx = (i * 31 + 10) % 110 + 9;
      int ph = (int)(now / 75 + i * 200) % 28;
      if (ph < 16) {
        int r = ph / 4 + 1;
        display.drawCircle(sx, 35, r, WHITE);
      }
    }

    // ===== 5: THUNDERSTORM =====
  } else if (itype == 5) {
    cloudShape(64, 11, 1);
    for (int i = 0; i < 20; i++) {
      int rx = (i * 15 + 5) % 114 + 7;
      int ry = (int)(now / 32 + i * 10) % 18 + 18;
      if (ry > 17 && ry < 36)
        display.drawLine(rx, ry, rx - 3, ry + 6, WHITE);
    }
    int lphase = (int)(now / 190) % 8;
    if (lphase > 1) {
      drawBolt(50, 14, 16, 18);
      if (lphase == 2) {
        display.drawLine(0, 0, 127, 0, WHITE);
        display.drawLine(0, 1, 127, 1, WHITE);
      }
    }

    // ===== 6: SNOW =====
  } else if (itype == 6) {
    cloudShape(64, 12, 1);
    for (int i = 0; i < 12; i++) {
      int sx = (i * 21 + 4) % 114 + 7;
      int speed = 185 + (i % 4) * 38;
      int sy = (int)(now / speed + i * 22) % 16 + 18;
      int r = (i % 3 == 0) ? 4 : 3;
      if (sy > 17 && sy < 35)
        drawSnowflake(sx, sy, r);
    }
    for (int b = 0; b < 5; b++)
      display.fillCircle(b * 26 + 8, 34, 2, WHITE);
    for (int x = 0; x < 128; x += 2)
      display.drawPixel(x, 35, WHITE);
  }

  // =====================================================
  // INFO STRIP — y 37–63  (no hard double divider)
  // Single thin separator line, then bigger readable info
  // =====================================================
  display.drawLine(0, 37, 127, 37, WHITE);

  // --- Temperature SIZE 2 — left ---
  char tb[6];
  if (!isnan(outdoorTemp))
    sprintf(tb, "%d", (int)round(outdoorTemp));
  else
    strcpy(tb, "--");
  display.setTextSize(2);
  display.setCursor(3, 40);
  display.print(tb);
  int numW = strlen(tb) * 12; // SIZE 2 char width

  // Degree + C in SIZE 1 superscript
  display.setTextSize(1);
  display.setCursor(3 + numW + 1, 40);
  display.print("\xf8"
                "C");

  // --- Condition SIZE 1 — right aligned, vertically centered with temp ---
  const char *cond;
  if (weatherCode == 0)
    cond = "Clear";
  else if (weatherCode <= 2)
    cond = "Partly Cloudy";
  else if (weatherCode == 3)
    cond = "Cloudy";
  else if (weatherCode <= 48)
    cond = "Fog";
  else if (weatherCode <= 55)
    cond = "Drizzle";
  else if (weatherCode <= 67)
    cond = "Rain";
  else if (weatherCode <= 77)
    cond = "Snow";
  else if (weatherCode <= 82)
    cond = "Showers";
  else if (weatherCode <= 86)
    cond = "Snow Shower";
  else
    cond = "Storm";

  // Wrap condition to two lines if longer than fits
  int condFitCols =
      (125 - (3 + numW + 14)) / 6; // available chars on right side
  display.setTextSize(1);
  if ((int)strlen(cond) <= condFitCols) {
    int condW = strlen(cond) * 6;
    display.setCursor(125 - condW, 44);
    display.print(cond);
  } else {
    // Two-word split
    char c1[12] = {0}, c2[12] = {0};
    const char *sp = strchr(cond, ' ');
    if (sp) {
      strncpy(c1, cond, sp - cond);
      c1[sp - cond] = '\0';
      strncpy(c2, sp + 1, 11);
    } else {
      strncpy(c1, cond, 11);
    }
    display.setCursor(125 - (int)strlen(c1) * 6, 40);
    display.print(c1);
    if (c2[0]) {
      display.setCursor(125 - (int)strlen(c2) * 6, 49);
      display.print(c2);
    }
  }

  // --- City name SIZE 1 — centered at bottom ---
  display.setTextSize(1);
  if (weatherCity[0]) {
    int cityW = strlen(weatherCity) * 6;
    if (cityW <= 122) {
      display.setCursor((128 - cityW) / 2, 56);
      display.print(weatherCity);
    } else {
      char abbr[10];
      strncpy(abbr, weatherCity, 8);
      abbr[8] = '.';
      abbr[9] = '\0';
      int aw = strlen(abbr) * 6;
      display.setCursor((128 - aw) / 2, 56);
      display.print(abbr);
    }
  }
}

// ---- Unused but kept for compatibility ----
void showWeather() { drawWeatherFace(); }
void drawMusicBar() {} // no longer used

// ================= CLOCK & WEATHER =================
void wrapTextToLines(const char *text, char *line1, char *line2) {
  line1[0] = '\0';
  line2[0] = '\0';
  int len = strlen(text);
  if (len == 0)
    return;
  if (len <= 21) {
    strncpy(line1, text, 31);
    line1[31] = '\0';
    return;
  }
  int split = 21;
  while (split > 0 && text[split] != ' ')
    split--;
  if (split == 0)
    split = 21;
  strncpy(line1, text, split);
  line1[split] = '\0';
  int s2 = (text[split] == ' ') ? split + 1 : split;
  int remaining = len - s2;
  if (remaining > 31)
    remaining = 31;
  strncpy(line2, text + s2, remaining);
  line2[remaining] = '\0';
}
void syncTime() {
// Only sync via NTP if WiFi is available
// Otherwise time comes from Gadgetbridge setTime packet on connect
#ifdef WIFI_H
  configTime(19800, 0, "time.google.com", "time.cloudflare.com",
             "pool.ntp.org");
  struct tm t;
  int r = 0;
  while (!getLocalTime(&t) && r < 20) {
    delay(300);
    r++;
  }
  if (getLocalTime(&t)) {
    timeReady = true;
    Serial.println("NTP time synced");
  }
#endif
}
void showClock() {
  unsigned long now = millis();
  struct tm t;
  if (!getLocalTime(&t)) {
    // Waiting for Gadgetbridge to sync time
    display.drawRoundRect(1, 1, 126, 62, 6, WHITE);
    for (int i = 0; i < 8; i++) {
      float a = i * 0.785f;
      int x = 64 + (int)(cos(a) * 20), y = 32 + (int)(sin(a) * 14);
      int d = (now / 200) % 8;
      if (i == d)
        display.fillCircle(x, y, 3, WHITE);
      else
        display.drawPixel(x, y, WHITE);
    }
    centerText("waiting for time", 36, 1);
    return;
  }

  int hr12 = t.tm_hour % 12;
  if (hr12 == 0)
    hr12 = 12;
  bool pm = t.tm_hour >= 12;
  int mn = t.tm_min;
  int sc = t.tm_sec;

  // Detect whether we have weather info from app
  bool hasWeatherInfo = (weatherReady && !isnan(outdoorTemp)) || weatherCity[0];

  if (!hasWeatherInfo) {
    // ===== FULL-SCREEN CLOCK — big, clean, no weather strip =====

    // ---- TOP BAR: seconds progress (y=0..3, 4px tall) ----
    for (int i = 0; i < 60; i++) {
      int sx = 4 + i * 2;
      if (i < sc)
        display.fillRect(sx, 0, 2, 4, WHITE);
      else if (i % 5 == 0)
        display.drawRect(sx, 0, 2, 4, WHITE);
    }

    // ---- AM/PM PILL — top-left ----
    display.fillRoundRect(2, 4, 20, 10, 3, WHITE);
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(4, 5);
    display.print(pm ? "PM" : "AM");
    display.setTextColor(WHITE);

    // ---- BLE STATUS — top-right indicator ----
    if (bleConnected)
      display.fillCircle(122, 8, 2, WHITE);
    else
      display.drawCircle(122, 8, 2, WHITE);

    // ---- MAIN TIME — SIZE 4, big and centered ----
    char ts[6];
    sprintf(ts, "%d:%02d", hr12, mn);
    display.setTextSize(4);
    int tw = strlen(ts) * 24; // size 4 = 24px per char
    int tx = (128 - tw) / 2;
    int ty = 16; // below AM/PM pill (ends at y=14) with 2px gap
    display.setCursor(tx, ty);
    display.print(ts);

    // Blinking colon
    if (sc % 2 == 0) {
      int colonOff = (hr12 >= 10) ? 2 : 1;
      int colonX = tx + colonOff * 24;
      display.fillRect(colonX, ty, 14, 32, BLACK);
    }

    // ---- SECONDS — right of time, baseline-aligned ----
    char ss[3];
    sprintf(ss, "%02d", sc);
    display.setTextSize(1);
    display.setCursor(tx + tw + 3, ty + 24); // bottom-aligned with size 4
    display.print(ss);

    // ---- DAY + DATE — centered below time ----
    char dayStr[4], dateStr[9];
    strftime(dayStr, 4, "%a", &t);
    strftime(dateStr, 9, "%d %b", &t);
    char fullDate[16];
    sprintf(fullDate, "%s  %s", dayStr, dateStr);
    display.setTextSize(1);
    int dateW = strlen(fullDate) * 6;
    display.setCursor((128 - dateW) / 2, 50); // below time (16+32=48, +2px gap)
    display.print(fullDate);

    // ---- BOTTOM BAR: minutes progress (y=60..63, 4px tall) ----
    for (int i = 0; i < 60; i++) {
      int mx = 4 + i * 2;
      if (i < mn)
        display.fillRect(mx, 60, 2, 4, WHITE);
      else if (i % 15 == 0)
        display.drawRect(mx, 60, 2, 4, WHITE);
    }

  } else {
    // ===== COMPACT CLOCK — original layout with weather strip =====

    // ---- TOP BAR: seconds progress (y=0..3, 4px tall) ----
    for (int i = 0; i < 60; i++) {
      int sx = 4 + i * 2;
      if (i < sc)
        display.fillRect(sx, 0, 2, 4, WHITE);
      else if (i % 5 == 0)
        display.drawRect(sx, 0, 2, 4, WHITE);
    }

    // ---- AM/PM PILL — top-left ----
    display.fillRoundRect(2, 4, 20, 10, 3, WHITE);
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(4, 5);
    display.print(pm ? "PM" : "AM");
    display.setTextColor(WHITE);

    // ---- MAIN TIME — size 3 ----
    char ts[6];
    sprintf(ts, "%d:%02d", hr12, mn);
    display.setTextSize(3);
    int tw = strlen(ts) * 18;
    int tx = (128 - tw) / 2;
    int ty = 16; // below AM/PM pill (ends at y=14) with 2px gap

    // Blinking colon — erase the colon pixel area when off
    display.setCursor(tx, ty);
    display.print(ts);

    if (sc % 2 == 0) {
      int colonOff = (hr12 >= 10) ? 2 : 1;
      int colonX = tx + colonOff * 18;
      display.fillRect(colonX, ty, 10, 24, BLACK);
    }

    // ---- SECONDS — size 1, bottom-aligned right of main time ----
    char ss[3];
    sprintf(ss, "%02d", sc);
    display.setTextSize(1);
    display.setCursor(tx + tw + 3, ty + 16);
    display.print(ss);

    // ---- DAY + DATE — below time ----
    char dayStr[4];
    strftime(dayStr, 4, "%a", &t);
    char dateStr[9];
    strftime(dateStr, 9, "%d %b", &t);

    display.setTextSize(1);
    display.setCursor(4, 42); // below time (16+24=40, +2px gap)
    display.print(dayStr);
    int dateW = strlen(dateStr) * 6;
    display.setCursor((128 - dateW) / 2, 42);
    display.print(dateStr);

    // ---- DIVIDER ----
    display.drawLine(0, 51, 127, 51, WHITE);

    // ---- BOTTOM STRIP: temp left, city right ----
    display.setTextSize(1);
    if (weatherReady && !isnan(outdoorTemp)) {
      char wb[8];
      sprintf(wb,
              "%d\xf8"
              "C",
              (int)round(outdoorTemp));
      display.setCursor(3, 52);
      display.print(wb);
    }
    // City name right-aligned (blank if no city from app)
    if (weatherCity[0]) {
      int cityW2 = strlen(weatherCity) * 6;
      if (cityW2 <= 80) {
        display.setCursor(125 - cityW2, 52);
        display.print(weatherCity);
      } else {
        char abbr[7];
        strncpy(abbr, weatherCity, 5);
        abbr[5] = '.';
        abbr[6] = '\0';
        int aw = strlen(abbr) * 6;
        display.setCursor(125 - aw, 52);
        display.print(abbr);
      }
    }

    // ---- BLE STATUS — top-right indicator ----
    if (bleConnected)
      display.fillCircle(122, 8, 2, WHITE);
    else
      display.drawCircle(122, 8, 2, WHITE);

    // ---- BOTTOM BAR: minutes progress (y=60..63, 4px tall) ----
    for (int i = 0; i < 60; i++) {
      int mx = 4 + i * 2;
      if (i < mn)
        display.fillRect(mx, 60, 2, 4, WHITE);
      else if (i % 15 == 0)
        display.drawRect(mx, 60, 2, 4, WHITE);
    }
  }
}
void fetchWeather() {
  // Weather comes from Gadgetbridge — this stub kept for compatibility
  // Enable WiFi includes at top to restore HTTP fallback
}

void drawMainCloud(int x, int y) {
  display.fillCircle(x + 12, y + 10, 8, WHITE);
  display.fillCircle(x + 24, y + 7, 10, WHITE);
  display.fillCircle(x + 36, y + 10, 8, WHITE);
  display.fillCircle(x + 18, y + 14, 9, WHITE);
  display.fillCircle(x + 30, y + 14, 9, WHITE);
}
void drawSmallCloud(int x, int y) {
  display.fillCircle(x + 6, y + 6, 4, WHITE);
  display.fillCircle(x + 12, y + 4, 5, WHITE);
  display.fillCircle(x + 18, y + 6, 4, WHITE);
}
void animateClouds() {
  if (millis() - cloudAnimTimer > 30) {
    mainCloudX += 0.5;
    smallCloud1X += 0.5;
    smallCloud2X += 0.5;
    if (mainCloudX > 140)
      mainCloudX = -60;
    if (smallCloud1X > 140)
      smallCloud1X = -40;
    if (smallCloud2X > 140)
      smallCloud2X = -50;
    cloudAnimTimer = millis();
  }
  drawMainCloud((int)mainCloudX, 20);
  drawSmallCloud((int)smallCloud1X, 16);
  drawSmallCloud((int)smallCloud2X, 30);
}

// ================= SPEAKER ICON =================
// Draws a tiny speaker icon at (x,y). If muted=true, draws an X over it.
// Icon is ~12x10 pixels. (x,y) is top-left of the speaker body.
void drawSpeakerIcon(int x, int y, bool muted) {
  // Speaker body (small rectangle)
  display.fillRect(x, y + 2, 4, 6, WHITE);
  // Speaker cone (triangle)
  display.fillTriangle(x + 4, y, x + 4, y + 9, x + 9, y + 5, WHITE);
  if (!muted) {
    // Sound waves (arcs approximated with pixels)
    display.drawPixel(x + 11, y + 3, WHITE);
    display.drawPixel(x + 11, y + 7, WHITE);
    display.drawPixel(x + 12, y + 5, WHITE);
  } else {
    // Mute X over the speaker
    display.drawLine(x + 10, y + 1, x + 14, y + 9, WHITE);
    display.drawLine(x + 14, y + 1, x + 10, y + 9, WHITE);
  }
}
