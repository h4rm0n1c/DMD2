/*
  Timer management code for the DMD library, includes implementation
  for AVR-based Arduino compatible (Eleven, Uno, etc.) and for Arduino Due

 Copyright (C) 2014 Freetronics, Inc. (info <at> freetronics <dot> com)

 Updated by Angus Gratton, based on DMD by Marc Alexander.

---

 This program is free software: you can redistribute it and/or modify it under the terms
 of the version 3 GNU General Public License as published by the Free Software Foundation.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with this program.
 If not, see <http://www.gnu.org/licenses/>.
*/
#include "DMD2.h"
#ifdef ESP8266
#include <Schedule.h>
#endif

/*
  Uncomment the following line if you don't want DMD library to touch
  any timer functionality (ie if you want to use TimerOne library or
  similar, or other libraries that use timers.

  With timers enabled, on AVR-based Arduinos this code will register a
  Timer1 overflow handler (without disrupting any built-in library
  functions.) On ARM-based Arduino Due it will use use Timer7.

*/

//#define NO_TIMERS

#ifndef DMD2_ESP8266_REFRESH_US
#define DMD2_ESP8266_REFRESH_US 1000 // Default to lower refresh pressure for Wi-Fi/WDT stability.
#endif

#ifndef DMD2_ESP8266_SCAN_DIVIDER
#define DMD2_ESP8266_SCAN_DIVIDER 2 // Scan once every N timer ticks to reduce ISR load.
#endif

#ifndef DMD2_ESP8266_MAX_SCANS_PER_SERVICE
#define DMD2_ESP8266_MAX_SCANS_PER_SERVICE 2 // Prevent burst starvation in loop context.
#endif

#ifndef DMD2_ESP8266_AUTO_SERVICE_HOOK
#define DMD2_ESP8266_AUTO_SERVICE_HOOK 1 // Schedule task-context refresh service automatically.
#endif

#ifndef DMD2_ESP8266_ADAPTIVE_INTERVAL
#define DMD2_ESP8266_ADAPTIVE_INTERVAL 1 // Set to 0 to keep deterministic fixed refresh interval.
#endif

#ifndef DMD2_ESP8266_REFRESH_MAX_US
#define DMD2_ESP8266_REFRESH_MAX_US 4000 // Adaptive upper bound for heavily loaded redraw paths.
#endif

#define ESP8266_TIMER0_TICKS microsecondsToClockCycles(DMD2_ESP8266_REFRESH_US)
#define ESP8266_TIMER0_MAX_TICKS microsecondsToClockCycles(DMD2_ESP8266_REFRESH_MAX_US)

#ifdef NO_TIMERS

// Timer-free stub code which gets compiled in only if NO_TIMERS is set
void BaseDMD::begin() {
  beginNoTimer();
}

void BaseDMD::end() {
}

void BaseDMD::serviceAll() {
}

#else // Use timers

// Forward declarations for tracking currently running DMDs
static void register_running_dmd(BaseDMD *dmd);
static bool unregister_running_dmd(BaseDMD *dmd);
static void inline scan_running_dmds();

#ifdef ESP8266
// ISR path must remain IRAM-safe and minimal latency on ESP8266.
static void ICACHE_RAM_ATTR esp8266_ISR_wrapper();
static volatile uint8_t esp8266_isr_divider = 0; // ISR-only divider counter.
static volatile uint8_t esp8266_scan_pending = 0; // ISR increments, task context consumes.
static volatile uint32_t esp8266_timer_interval_ticks = ESP8266_TIMER0_TICKS; // ISR reads for next arm.
static volatile uint8_t esp8266_timer_active = 0; // Task context toggles lifecycle state.
#if DMD2_ESP8266_AUTO_SERVICE_HOOK
static volatile uint8_t esp8266_service_queued = 0;
static void esp8266_serviceAll_callback();
#endif
#endif

#ifdef __AVR__

/* This AVR timer ISR uses the standard /64 timing used by Timer1 in the Arduino core,
   so none of those registers (or normal PWM timing) is changed. We do skip 50% of ISRs
   as 50% timer overflows is approximately every 4ms, which is fine for flicker-free
   updating.
*/
ISR(TIMER1_OVF_vect)
{
  static uint8_t skip_isrs = 0;
  skip_isrs = (skip_isrs + 1) % 2;
  if(skip_isrs)
    return;
  scan_running_dmds();
}

void BaseDMD::begin()
{
  beginNoTimer(); // Do any generic setup

  char oldSREG = SREG;
  cli();
  register_running_dmd(this);
  TIMSK1 = _BV(TOIE1); // set overflow interrupt
  SREG = oldSREG;
}

void BaseDMD::end()
{
  char oldSREG = SREG;
  cli();
  bool still_running = unregister_running_dmd(this);
  if(!still_running)
    TIMSK1 &= ~_BV(TOIE1); // disable timer interrupt, no more DMDs are running
  SREG = oldSREG;
  // One final (manual) scan to turn off all LEDs
  clearScreen();
  scanDisplay();
}

#elif defined (__arm__) // __ARM__, Due assumed for now

/* ARM timer callback (ISR context), checks timer status then scans all running DMDs */
void TC7_Handler(){
  TC_GetStatus(TC2, 1);
  scan_running_dmds();
}

void BaseDMD::begin()
{
  beginNoTimer(); // Do any generic setup

  NVIC_DisableIRQ(TC7_IRQn);
  register_running_dmd(this);
  pmc_set_writeprotect(false);
  pmc_enable_periph_clk(TC7_IRQn);
  // Timer 7 is TC2, channel 1
  TC_Configure(TC2, 1, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_TCCLKS_TIMER_CLOCK4); // counter up, /128 divisor
  TC_SetRC(TC2, 1, 2500); // approx 4ms at /128 divisor
  TC2->TC_CHANNEL[1].TC_IER=TC_IER_CPCS;

  NVIC_ClearPendingIRQ(TC7_IRQn);
  NVIC_EnableIRQ(TC7_IRQn);
  TC_Start(TC2, 1);
}

void BaseDMD::end()
{
  NVIC_DisableIRQ(TC7_IRQn);
  bool still_running = unregister_running_dmd(this);
  if(still_running)
    NVIC_EnableIRQ(TC7_IRQn); // Still some DMDs running
  else
    TC_Stop(TC2, 1);
  clearScreen();
  scanDisplay();
}

#elif defined (ESP8266)

void BaseDMD::begin()
{
  beginNoTimer();
  timer0_detachInterrupt();

  noInterrupts();
  register_running_dmd(this);
  interrupts();

  esp8266_isr_divider = 0;
  esp8266_scan_pending = 0;
  esp8266_timer_interval_ticks = ESP8266_TIMER0_TICKS;
  esp8266_timer_active = 1;
#if DMD2_ESP8266_AUTO_SERVICE_HOOK
  esp8266_service_queued = 0;
#endif
  timer0_isr_init();
  timer0_attachInterrupt(esp8266_ISR_wrapper);
  timer0_write(ESP.getCycleCount() + esp8266_timer_interval_ticks);
#if DMD2_ESP8266_AUTO_SERVICE_HOOK
  if(!esp8266_service_queued) {
    esp8266_service_queued = 1;
    schedule_function(esp8266_serviceAll_callback);
  }
#endif
}

void BaseDMD::end()
{
  noInterrupts();
  bool still_running = unregister_running_dmd(this);
  interrupts();
  if(!still_running)
  {
    timer0_detachInterrupt(); // timer0 disables itself when the CPU cycle count reaches its own value, hence ESP.getCycleCount()
    esp8266_timer_active = 0;
#if DMD2_ESP8266_AUTO_SERVICE_HOOK
    esp8266_service_queued = 0;
#endif
  }
  clearScreen();
  scanDisplay();
}

#endif // ifdef __AVR__

/* Following functions are static non-architecture-specific functions
   to manage a global array that tracks all known DMD instances.

   This is so a global C code interrupt can call all of the DMDs on
   timer interrupt.

   The array is grown with realloc. If a DMD is destroyed then it
   shrink the array, it just NULLs out the entry (presuming there
   isn't a lot of dynamic growth/shrinkage of DMDs within a sketch!)
*/
static volatile BaseDMD **running_dmds = 0;
static volatile int running_dmd_len = 0;

// Add a running_dmd to the list (caller must have disabled interrupts)
static void register_running_dmd(BaseDMD *dmd)
{
  int empty = -1;
  for(int i = 0; i < running_dmd_len; i++) {
    if(running_dmds[i] == dmd)
      return; // Already running and registered
    if(!running_dmds[i])
      empty = i; // Found an unused slot in the array
  }

  if(empty == -1) { // Grow array to fit new entry
    running_dmd_len++;
    BaseDMD **resized = (BaseDMD **)realloc(running_dmds, sizeof(BaseDMD *)*running_dmd_len);
    if(!resized) {
      // Allocation failed, bail out

      running_dmd_len--;
      return;
    }
    empty = running_dmd_len-1;
    running_dmds = (volatile BaseDMD **)resized;
  }
  running_dmds[empty] = dmd;
}

// Null out a running_dmd from the list (caller must have disabled interrupts)
static bool unregister_running_dmd(BaseDMD *dmd)
{
  bool still_running = false;
  for(int i = 0; i < running_dmd_len; i++) {
    if(running_dmds[i] == dmd)
      running_dmds[i] = NULL;
    else if (running_dmds[i])
      still_running = true;
  }
  return still_running;
}

// ESP8266 ISR Wrapper
#ifdef ESP8266
// Keep ISR limited to counter updates and timer re-arm only.
static void inline ICACHE_RAM_ATTR esp8266_ISR_wrapper()
{
  esp8266_isr_divider++;
  if(esp8266_isr_divider >= DMD2_ESP8266_SCAN_DIVIDER) {
    esp8266_isr_divider = 0;
    if(esp8266_scan_pending < 0xFF)
      esp8266_scan_pending++;
  }
  timer0_write(ESP.getCycleCount() + esp8266_timer_interval_ticks);
}
#endif


#ifdef ESP8266
#if DMD2_ESP8266_AUTO_SERVICE_HOOK
static void esp8266_serviceAll_callback()
{
  noInterrupts();
  esp8266_service_queued = 0;
  interrupts();

  BaseDMD::serviceAll();

  noInterrupts();
  bool keep_running = (esp8266_timer_active != 0);
  bool can_queue = !esp8266_service_queued;
  if(keep_running && can_queue)
    esp8266_service_queued = 1;
  interrupts();

  if(keep_running && can_queue)
    schedule_function(esp8266_serviceAll_callback);
}
#endif

static void inline update_esp8266_refresh_interval(uint32_t scan_cycles)
{
#if DMD2_ESP8266_ADAPTIVE_INTERVAL
  uint32_t next_ticks = scan_cycles + (scan_cycles >> 2); // ~25% timing headroom above observed scan cost.
  if(next_ticks < ESP8266_TIMER0_TICKS)
    next_ticks = ESP8266_TIMER0_TICKS;
  if(next_ticks > ESP8266_TIMER0_MAX_TICKS)
    next_ticks = ESP8266_TIMER0_MAX_TICKS;

  uint32_t current_ticks = esp8266_timer_interval_ticks;
  esp8266_timer_interval_ticks = ((current_ticks * 3) + next_ticks) >> 2; // smooth interval changes.
#else
  (void)scan_cycles;
  esp8266_timer_interval_ticks = ESP8266_TIMER0_TICKS;
#endif
}
#endif


void BaseDMD::serviceAll()
{
#ifdef ESP8266
  uint8_t pending = 0;

  noInterrupts();
  pending = esp8266_scan_pending;
  if(pending > DMD2_ESP8266_MAX_SCANS_PER_SERVICE)
    pending = DMD2_ESP8266_MAX_SCANS_PER_SERVICE;
  esp8266_scan_pending -= pending;
  interrupts();

  if(!pending) {
#if DMD2_ESP8266_ADAPTIVE_INTERVAL
    // Ease back toward floor when workload calms down.
    uint32_t current_ticks = esp8266_timer_interval_ticks;
    if(current_ticks > ESP8266_TIMER0_TICKS) {
      uint32_t delta = (current_ticks - ESP8266_TIMER0_TICKS) >> 3;
      if(!delta)
        delta = 1;
      esp8266_timer_interval_ticks = current_ticks - delta;
    }
#endif
    return;
  }

  while(pending--) {
    uint32_t scan_start = ESP.getCycleCount();
    scan_running_dmds();
    uint32_t scan_cycles = ESP.getCycleCount() - scan_start;
    update_esp8266_refresh_interval(scan_cycles);
  }
#else
  // Non-ESP8266 targets either scan from ISR or by manual scanDisplay().
#endif
}

// This method is called from timer ISR to scan all the DMD instances present in the running sketch
static void inline __attribute__((always_inline)) scan_running_dmds()
{
  for(int i = 0; i < running_dmd_len; i++) {
    BaseDMD *next = (BaseDMD*)running_dmds[i];
    if(next) {
      next->scanDisplay();
    }
  }
}

#endif // ifdef NO_TIMERS
