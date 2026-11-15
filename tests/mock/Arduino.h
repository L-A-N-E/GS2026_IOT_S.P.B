// Mock minimo do Arduino para compilar e simular o firmware no PC (so para testes)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <ctime>
#include <climits>
#include <string>
#include <vector>
typedef uint8_t byte;
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
namespace sim {
  inline uint32_t agoraMs = 0; inline int pinos[40]; inline int adc = 0; inline int toneFreq = 0;
  inline bool wifiOk = true, mqttOk = true, ntpOk = false; inline struct tm ntp;
  inline std::vector<std::pair<std::string,std::string>> publicados; inline std::vector<std::string> serial;
}
inline uint32_t millis() { return sim::agoraMs; }
inline void delay(uint32_t ms) { sim::agoraMs += ms; }
inline void pinMode(int p, int m) { if (m == INPUT_PULLUP) sim::pinos[p] = HIGH; }
inline int digitalRead(int p) { return sim::pinos[p]; }
inline void digitalWrite(int p, int v) { sim::pinos[p] = v; }
inline int analogRead(int) { return sim::adc; }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
inline void tone(int, int f) { sim::toneFreq = f; }
inline void noTone(int) { sim::toneFreq = 0; }
inline long random(long m) { return rand() % m; }
inline void configTime(long, int, const char*, const char*) {}
inline bool getLocalTime(struct tm* t, uint32_t) { if (!sim::ntpOk) return false; *t = sim::ntp; return true; }
struct SerialMock {
  void begin(int) {}
  void println(const char* s) { sim::serial.push_back(s); }
  void printf(const char* f, ...) { char b[600]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); sim::serial.push_back(b); }
};
inline SerialMock Serial;
