#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

static constexpr const char *NAMESPACE = "copilotmeter";

// Preferences logs an error for every missing string key, so check first.
static void readString(Preferences &prefs, const char *key, char *dst, size_t len, const char *fallback)
{
  if (prefs.isKey(key))
    strlcpy(dst, prefs.getString(key, fallback).c_str(), len);
  else
    strlcpy(dst, fallback, len);
}

void configLoad(AppConfig &cfg)
{
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  readString(prefs, "user", cfg.username, sizeof(cfg.username), "");
  readString(prefs, "token", cfg.token, sizeof(cfg.token), "");
  cfg.monthlyLimit = prefs.getUInt("limit", DEFAULT_MONTHLY_LIMIT);
  cfg.refreshMinutes = prefs.getUShort("refresh", DEFAULT_REFRESH_MINUTES);
  readString(prefs, "tz", cfg.tz, sizeof(cfg.tz), DEFAULT_TZ);
  cfg.ledEnabled = prefs.getBool("led", true);
  prefs.end();

  if (cfg.monthlyLimit == 0)
    cfg.monthlyLimit = DEFAULT_MONTHLY_LIMIT;
  cfg.refreshMinutes = constrain(cfg.refreshMinutes, MIN_REFRESH_MINUTES, MAX_REFRESH_MINUTES);
  if (cfg.tz[0] == '\0')
    strlcpy(cfg.tz, DEFAULT_TZ, sizeof(cfg.tz));
}

void configSave(const AppConfig &cfg)
{
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.putString("user", cfg.username);
  prefs.putString("token", cfg.token);
  prefs.putUInt("limit", cfg.monthlyLimit);
  prefs.putUShort("refresh", cfg.refreshMinutes);
  prefs.putString("tz", cfg.tz);
  prefs.putBool("led", cfg.ledEnabled);
  prefs.end();
}

bool configIsComplete(const AppConfig &cfg)
{
  return cfg.username[0] != '\0' && cfg.token[0] != '\0';
}

void configErase()
{
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.clear();
  prefs.end();
}

void cacheLoad(CachedUsage &cache)
{
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  cache.valid = prefs.isKey("c_used");
  cache.used = cache.valid ? prefs.getDouble("c_used", 0) : 0;
  cache.fetchedAt = prefs.getLong64("c_at", 0);
  const int32_t ym = prefs.getInt("c_ym", 0);
  prefs.end();
  cache.year = ym / 100;
  cache.month = ym % 100;
}

void cacheSave(const CachedUsage &cache)
{
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  prefs.putDouble("c_used", cache.used);
  prefs.putLong64("c_at", cache.fetchedAt);
  prefs.putInt("c_ym", cache.year * 100 + cache.month);
  prefs.end();
}

void tokenHint(const char *token, char *out, size_t len)
{
  const size_t n = strlen(token);
  if (n == 0)
    snprintf(out, len, "not set");
  else if (n <= 8)
    snprintf(out, len, "saved");
  else
    snprintf(out, len, "saved ...%s", token + n - 4);
}

static void trimInto(const char *src, char *dst, size_t len)
{
  while (*src && isspace(static_cast<unsigned char>(*src)))
    ++src;
  strlcpy(dst, src, len);
  size_t n = strlen(dst);
  while (n > 0 && isspace(static_cast<unsigned char>(dst[n - 1])))
    dst[--n] = '\0';
}

// Keeps only allowed characters; returns false if anything was dropped.
static bool filterChars(char *s, bool (*allowed)(char))
{
  bool clean = true;
  char *out = s;
  for (char *in = s; *in; ++in)
  {
    if (allowed(*in))
      *out++ = *in;
    else
      clean = false;
  }
  *out = '\0';
  return clean;
}

static bool usernameChar(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '-'; }
static bool tokenChar(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; }
static bool tzChar(char c) { return isalnum(static_cast<unsigned char>(c)) || strchr("+-,.:/<>", c) != nullptr; }

bool configSetUsername(AppConfig &cfg, const char *value)
{
  char buf[USERNAME_LEN];
  trimInto(value, buf, sizeof(buf));
  const char *name = buf[0] == '@' ? buf + 1 : buf;
  char clean[USERNAME_LEN];
  strlcpy(clean, name, sizeof(clean));
  filterChars(clean, usernameChar);
  strlcpy(cfg.username, clean, sizeof(cfg.username));
  return true;
}

bool configSetToken(AppConfig &cfg, const char *value)
{
  char buf[TOKEN_LEN];
  trimInto(value, buf, sizeof(buf));
  if (buf[0] == '\0' || !filterChars(buf, tokenChar))
    return false;
  strlcpy(cfg.token, buf, sizeof(cfg.token));
  return true;
}

bool configSetLimit(AppConfig &cfg, const char *value)
{
  const unsigned long limit = strtoul(value, nullptr, 10);
  if (limit == 0)
    return false;
  cfg.monthlyLimit = limit > MAX_MONTHLY_LIMIT ? MAX_MONTHLY_LIMIT : limit;
  return true;
}

bool configSetRefresh(AppConfig &cfg, const char *value)
{
  const unsigned long minutes = strtoul(value, nullptr, 10);
  if (minutes == 0)
    return false;
  cfg.refreshMinutes = constrain(minutes, MIN_REFRESH_MINUTES, MAX_REFRESH_MINUTES);
  return true;
}

bool configSetTz(AppConfig &cfg, const char *value)
{
  char buf[TZ_LEN];
  trimInto(value, buf, sizeof(buf));
  filterChars(buf, tzChar);
  strlcpy(cfg.tz, buf[0] ? buf : DEFAULT_TZ, sizeof(cfg.tz));
  return true;
}

void configApPassword(char *out, size_t len)
{
  static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
  Preferences prefs;
  prefs.begin(NAMESPACE, false);
  char pass[12] = "";
  if (prefs.isKey("ap_pass"))
    strlcpy(pass, prefs.getString("ap_pass").c_str(), sizeof(pass));
  if (strlen(pass) < 8)
  {
    for (uint8_t i = 0; i < 8; ++i)
      pass[i] = ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
    pass[8] = '\0';
    prefs.putString("ap_pass", pass);
  }
  prefs.end();
  strlcpy(out, pass, len);
}
