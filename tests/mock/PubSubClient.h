#pragma once
#include "Arduino.h"
struct PubSubClient {
  bool con = false; void (*cb)(char*, byte*, unsigned int) = nullptr;
  PubSubClient(WiFiClient&) {}
  void setServer(const char*, int) {} void setBufferSize(int) {} void setSocketTimeout(int) {}
  void setCallback(void (*f)(char*, byte*, unsigned int)) { cb = f; }
  bool connected() { if (!sim::mqttOk) con = false; return con; }
  bool connect(const char*, const char*, const char*, const char*, int, bool, const char*) { con = sim::mqttOk; return con; }
  void subscribe(const char*) {} void loop() {}
  bool publish(const char* t, const char* m, bool) { sim::publicados.push_back({t, m}); return true; }
  void injetar(const char* m, const char* topico = "sbp-gs2026/totem/sbp-0001/cmd") { char t[80]; strncpy(t, topico, 79); t[79] = 0; cb(t, (byte*)m, strlen(m)); }
};
