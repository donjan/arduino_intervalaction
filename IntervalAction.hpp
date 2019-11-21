/*
 * IntervalAction.hpp - protothreading class
 *
 * Copyright 2019 Donjan Rodic <donjan@dyx.ch>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INTERVALACTION_HPP
#define INTERVALACTION_HPP

#define ARDUINO_ONLY_CODE_SECTION  // Arduino specifics, remove for portability

#include <stdint.h>

/**
 * Protothreading class inspired by the Arduino TimedAction class.
 * Can access class members and has an ad-hoc execution mechanism which leaves
 * the code where it can be seen.
 * For usage within class methods, just capture 'this' and use members directly.
 *
 * Simple Arduino usage example:
 *   void loop() {
 *      static IntervalAction<micros> ia_work(3'000'000);  // every 3 seconds
 *      ia_work([](){
 *        Serial.println("hi");
 *        ia_work.set_interval(1'000'000);  // we changed our mind to 1 second
 *      });
 *   }
 */
template<uint32_t (*timer_function)(void)>
class IntervalAction {

  public:

  IntervalAction(const uint32_t iv) : interval(iv), t_prev(timer_function()) {}

  void set_interval(const uint32_t iv) { interval = iv; }
  uint32_t get_interval() const { return interval; }
  uint32_t get_prev() const { return t_prev; }

  template<typename T> void operator()(const T&& func) {
    if(timer_function() - t_prev >= interval) {
      t_prev = timer_function();
      func();
    }
  }

  private:

  uint32_t interval;
  uint32_t t_prev;

};

/**
 * IntervalAction taking method pointers.
 * Awkward instantiation and seems to have SRAM issues.
 *
 * Class obj;
 * IntervalActionMEMFN<Class, &Class::method> ia_work(obj, 1'000'000);
 */
template<typename T, uint32_t(T::*timer_function)() const>
class IntervalActionMEMFN {

  public:

  IntervalActionMEMFN(const T& cclass, const uint32_t iv) :
      interval(iv),
      t_prev((cclass.*timer_function)()),
      clock_(cclass) {}

  void set_interval(const uint32_t iv) { interval = iv; }
  uint32_t get_interval() const { return interval; }
  uint32_t get_prev() const { return t_prev; }

  template<typename U> void operator()(const U&& func) {
    if((clock_.*timer_function)() - t_prev >= interval) {
      t_prev = (clock_.*timer_function)();
      func();
    }
  }

  private:

  uint32_t interval;
  uint32_t t_prev;
  const T& clock_;

};

/**
 * IntervalAction taking lambdas.
 * Speed close to hardcoded function.
 * Awkward instantiation and seems to have SRAM issues.
 *
 * constexpr auto lambda = []() -> uint32_t { return micros(); };
 * IntervalActionTPL<decltype(lambda)> ia_work(lambda, 1'000'000);
 */
template<typename FUNC>
class IntervalActionTPL {

  public:

  IntervalActionTPL(const FUNC& tf, const uint32_t iv) :
      timer_function(tf),
      interval(iv),
      t_prev(timer_function()) {}

  void set_interval(const uint32_t iv) { interval = iv; }
  uint32_t get_interval() const { return interval; }
  uint32_t get_prev() const { return t_prev; }

  template<typename T> void operator()(const T&& func) {
    if(timer_function() - t_prev >= interval) {
      t_prev = timer_function();
      func();
    }
  }

  private:

  const FUNC& timer_function;
  uint32_t interval;
  uint32_t t_prev;

};

////////////////////////////////////////////////////////////////////////////////
// Arduino Section

#ifdef ARDUINO_ONLY_CODE_SECTION

#include <tool/clock.hpp>  // https://github.com/mskoenz/arduino_crash_course
#include <Arduino.h>

/**
 * IntervalAction based on ustd::tool::clock.
 * Hardcoded, but significantly faster than a free function wrapper. (no
 * pointer -> inlining)
 *
 * IntervalActionUstdClock ia_work(1'000'000);
 */
class IntervalActionUstdClock {

  public:

  IntervalActionUstdClock(const uint32_t iv) :
      interval(iv),
      t_prev(tool::clock.micros()) {}

  void set_interval(const uint32_t iv) { interval = iv; }
  //~ void set_interval(const uint32_t iv) { interval = iv; active = true; }
  uint32_t get_interval() const { return interval; }
  uint32_t get_prev() const { return t_prev; }
  //~ void deactivate() { active = false; }

  template<typename T> void operator()(const T&& func) {
    if(tool::clock.micros() - t_prev >= interval) {
    //~ if(active && tool::clock.micros() - t_prev >= interval) {
      t_prev = tool::clock.micros();
      func();
    }
  }

  private:

  uint32_t interval;
  uint32_t t_prev;
  //~ bool active = true;  // too much SRAM usage...

};

/**
 * Quality of life: print iterations per second.
 */
#define IA_PRINT_ITER_PER_SEC() {                                              \
  static IntervalActionUstdClock _ia_iter_per_sec(1'000'000);                  \
  static uint16_t _cnt_iter_per_sec = 0;                                       \
  ++_cnt_iter_per_sec;                                                         \
  _ia_iter_per_sec([](){                                                       \
    Serial.print("iter/sec: ");Serial.print(_cnt_iter_per_sec);                \
    Serial.print(" (");                                                        \
    Serial.print(_ia_iter_per_sec.get_interval() / _cnt_iter_per_sec);         \
    Serial.println(" us average loop time)");                                  \
    _cnt_iter_per_sec = 0;                                                     \
  });                                                                          \
}                                                                             //

/**
 * Quality of life: print SRAM usage.
 */
#define IA_PRINT_SRAM_USAGE() {                                                \
  static IntervalAction<micros> ia_sram_report(3'000'000);                     \
  ia_sram_report([](){                                                         \
    diag::ram_report();                                                        \
  });                                                                          \
}                                                                             //

/**
 * Quality of life: print load in scope marked by provided times.
 * This doesn't account for itself (maybe at least 10% wrong final value).
 */
#define IA_PRINT_LOAD(time_start, time_end) {                                  \
  static uint32_t _duty_done = 0;                                              \
  _duty_done += time_end - time_start;                                         \
  static IntervalActionUstdClock _ia_duty_cycle_report(1'000'000);             \
  _ia_duty_cycle_report([](){                                                  \
    Serial.print("Load: ");                                                    \
    Serial.print(100*(float(_duty_done) / 1'000'000));                         \
    Serial.println("%");                                                       \
    _duty_done = 0;                                                            \
  });                                                                          \
}                                                                             //

#endif  // ARDUINO_ONLY_CODE_SECTION

#endif  // INTERVALACTION_HPP
