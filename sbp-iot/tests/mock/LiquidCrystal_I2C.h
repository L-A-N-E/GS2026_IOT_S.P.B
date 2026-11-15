#pragma once
#include "Arduino.h"
struct LiquidCrystal_I2C {
  char tela[4][21]; int c = 0, r = 0; bool luz = false;
  LiquidCrystal_I2C(int, int, int) { for (auto& l : tela) { memset(l, ' ', 20); l[20] = 0; } }
  void init() {} void backlight() { luz = true; } void noBacklight() { luz = false; }
  void setCursor(int cc, int rr) { c = cc; r = rr; }
  void print(const char* s) { for (; *s && c < 20; s++) tela[r][c++] = *s; }
};
