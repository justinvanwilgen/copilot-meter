#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint32_t DEFAULT_MONTHLY_LIMIT = 20000; // Copilot Max
constexpr uint32_t MAX_MONTHLY_LIMIT = 10000000;
constexpr uint16_t DEFAULT_REFRESH_MINUTES = 30;
constexpr uint16_t MIN_REFRESH_MINUTES = 5;
constexpr uint16_t MAX_REFRESH_MINUTES = 1440;
constexpr const char *DEFAULT_TZ = "SAST-2";

constexpr size_t USERNAME_LEN = 40;
constexpr size_t TOKEN_LEN = 200;
constexpr size_t TZ_LEN = 64;

struct AppConfig
{
  char username[USERNAME_LEN];
  char token[TOKEN_LEN];
  uint32_t monthlyLimit;
  uint16_t refreshMinutes;
  char tz[TZ_LEN];
  bool ledEnabled;
};

// Last successful reading, persisted so the screen has something to show after a reboot.
struct CachedUsage
{
  bool valid;
  double used;
  int64_t fetchedAt; // unix seconds (UTC)
  int year;
  int month;
};

void configLoad(AppConfig &cfg);
void configSave(const AppConfig &cfg);
bool configIsComplete(const AppConfig &cfg);
void configErase();

void cacheLoad(CachedUsage &cache);
void cacheSave(const CachedUsage &cache);

// "…abcd" style hint so the token is never shown in full.
void tokenHint(const char *token, char *out, size_t len);

// Validated setters shared by the web form and the serial console. Values are trimmed and
// filtered; blank or invalid input leaves the current value unchanged (returns false).
bool configSetUsername(AppConfig &cfg, const char *value);
bool configSetToken(AppConfig &cfg, const char *value);
bool configSetLimit(AppConfig &cfg, const char *value);
bool configSetRefresh(AppConfig &cfg, const char *value);
bool configSetTz(AppConfig &cfg, const char *value);

// Per-device password for the setup hotspot, generated once and kept in NVS.
void configApPassword(char *out, size_t len);
