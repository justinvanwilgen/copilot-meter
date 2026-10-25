#include <Arduino.h>
#include <ESPmDNS.h>
#include <OneButton.h>
#include <WiFi.h>
#include <atomic>
#include <time.h>

#include "app/config.h"
#include "app/github_usage.h"
#include "app/usage_math.h"
#include "app/wifi_setup.h"
#include "board.h"
#include "drivers/display.h"
#include "drivers/rgb_led.h"
#include "drivers/touch.h"
#include "ui/dashboard.h"
#include "ui/setup_screens.h"
#include "ui/theme.h"

enum class AppState : uint8_t
{
  Connecting,
  AccessPoint,
  NeedsSetup,
  Running
};

static constexpr const char *HOSTNAME = "copilot-meter";
static constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
static constexpr uint16_t AP_RETRY_SECONDS = 180;
static constexpr uint16_t AP_MANUAL_SECONDS = 300;
static constexpr uint32_t REFRESH_COOLDOWN_MS = 30000;
static constexpr uint32_t ERROR_RETRY_MS = 2 * 60 * 1000UL;
static constexpr uint32_t MONTH_RESET_GRACE_MS = 90 * 1000UL;
static constexpr uint32_t UI_REFRESH_MS = 1000;
static constexpr uint32_t TOUCH_POLL_MS = 25;
static constexpr uint32_t DEMO_STEP_MS = 4000;
static constexpr uint32_t FETCH_STACK = 12288;
static constexpr float DEMO_PERCENTS[] = {12, 37, 62, 82, 95, 100};
static constexpr uint8_t BRIGHTNESS_LEVELS[] = {255, 120, 30};

static OneButton bootButton(PIN_BUTTON_BOOT);
static AppConfig config;
static CachedUsage cache;
static AppState state = AppState::Connecting;
static uint32_t stateSince = 0;
static bool timeSynced = false;
static bool mdnsStarted = false;

// Latest usage reading
static bool hasUsage = false;
static double usedCredits = 0;
static bool usageFromCache = false;
static time_t lastSuccessAt = 0;
static uint32_t lastSuccessMs = 0;
static bool lastFetchFailed = false;
static FetchStatus lastStatus = FetchStatus::Ok;

// Background fetch
struct FetchJob
{
  char username[USERNAME_LEN];
  char token[TOKEN_LEN];
};
static std::atomic<bool> fetchBusy{false};
static std::atomic<bool> fetchDone{false};
static UsageResult fetchResult;
static bool fetchPending = false;
static uint32_t nextFetchAt = 0;
static uint32_t lastFetchStartedAt = 0;
static bool refetchQueued = false; // settings changed while a fetch was in flight

// UI / input
static uint32_t lastUiAt = 0;
static bool uiDirty = false;
static uint32_t lastTouchPollAt = 0;
static bool touchHeld = false;
static uint32_t lastTickAt = 0;
static int32_t lastCountdown = -2;
static char transientText[40];
static uint32_t transientUntil = 0;
static bool demoMode = false;
static float forcedPercent = -1;
static uint8_t demoIndex = 0;
static uint32_t demoStepAt = 0;
static uint8_t brightnessIndex = 0;
static uint32_t lastHeapLogAt = 0;
static char serialLine[256];
static uint16_t serialLen = 0;
static uint32_t apCloseAt = 0;

static bool due(uint32_t at)
{
  return static_cast<int32_t>(millis() - at) >= 0;
}

static void showTransient(const char *text)
{
  strlcpy(transientText, text, sizeof(transientText));
  transientUntil = millis() + 3000;
  uiDirty = true;
}

static uint32_t refreshIntervalMs()
{
  return static_cast<uint32_t>(config.refreshMinutes) * 60UL * 1000UL;
}

// Make sure a fetch happens shortly after the monthly reset (00:00 UTC on the 1st).
static void clampToMonthReset()
{
  MonthInfo mi;
  if (!fetchPending || !timeSynced || !monthInfoUtc(time(nullptr), mi))
    return;
  const uint64_t untilReset = static_cast<uint64_t>(mi.secondsToReset) * 1000ULL + MONTH_RESET_GRACE_MS;
  const uint32_t untilNext = static_cast<int32_t>(nextFetchAt - millis()) > 0 ? nextFetchAt - millis() : 0;
  if (untilReset < untilNext)
    nextFetchAt = millis() + static_cast<uint32_t>(untilReset);
}

static void scheduleFetchIn(uint32_t ms)
{
  fetchPending = true;
  nextFetchAt = millis() + ms;
  clampToMonthReset();
}

static void fetchTask(void *arg)
{
  FetchJob *job = static_cast<FetchJob *>(arg);
  fetchResult = fetchCopilotCredits(job->username, job->token);
  memset(job->token, 0, sizeof(job->token));
  delete job;
  fetchDone = true;
  vTaskDelete(nullptr);
}

static bool startFetch(const char *reason)
{
  if (fetchBusy || !configIsComplete(config) || WiFi.status() != WL_CONNECTED)
    return false;

  FetchJob *job = new FetchJob;
  strlcpy(job->username, config.username, sizeof(job->username));
  strlcpy(job->token, config.token, sizeof(job->token));
  fetchBusy = true;
  fetchDone = false;
  fetchPending = false;
  lastFetchStartedAt = millis();
  uiDirty = true;
  Serial.printf("[app] refreshing (%s), heap %u\n", reason, ESP.getFreeHeap());

  // Core 0 keeps the TLS handshake off the UI loop so the mascot keeps animating.
  if (xTaskCreatePinnedToCore(fetchTask, "fetch", FETCH_STACK, job, 1, nullptr, 0) != pdPASS)
  {
    delete job;
    fetchBusy = false;
    Serial.println("[app] could not start fetch task");
    scheduleFetchIn(ERROR_RETRY_MS);
    return false;
  }
  return true;
}

static void requestRefresh(const char *source)
{
  if (state != AppState::Running || !configIsComplete(config))
    return;
  if (fetchBusy)
  {
    showTransient("Already refreshing");
    return;
  }
  const uint32_t since = millis() - lastFetchStartedAt;
  if (lastFetchStartedAt != 0 && since < REFRESH_COOLDOWN_MS)
  {
    char text[40];
    snprintf(text, sizeof(text), "Wait %lus", static_cast<unsigned long>((REFRESH_COOLDOWN_MS - since) / 1000 + 1));
    showTransient(text);
    return;
  }
  startFetch(source);
}

static void currentYearMonth(int &year, int &month)
{
  MonthInfo mi;
  if (timeSynced && monthInfoUtc(time(nullptr), mi))
  {
    year = mi.year;
    month = mi.month;
  }
  else
  {
    year = 0;
    month = 0;
  }
}

static void handleFetchResult()
{
  if (!fetchDone)
    return;
  fetchDone = false;
  const UsageResult r = fetchResult;
  fetchBusy = false;
  lastStatus = r.status;
  uiDirty = true;

  if (r.status == FetchStatus::Ok)
  {
    const bool changed = !hasUsage || usageFromCache || r.grossCredits != usedCredits;
    hasUsage = true;
    usedCredits = r.grossCredits;
    usageFromCache = false;
    lastFetchFailed = false;
    lastSuccessMs = millis();
    lastSuccessAt = timeSynced ? time(nullptr) : 0;

    int year = r.year;
    int month = r.month;
    if (year == 0 || month == 0)
      currentYearMonth(year, month);
    if (changed || cache.year != year || cache.month != month)
    {
      cache = {true, usedCredits, static_cast<int64_t>(lastSuccessAt), year, month};
      cacheSave(cache);
    }
    scheduleFetchIn(refreshIntervalMs());
  }
  else
  {
    lastFetchFailed = true;
    Serial.printf("[app] refresh failed: %s (HTTP %d)\n", fetchStatusText(r.status), r.httpCode);
    const bool transient = r.status == FetchStatus::ConnectFailed || r.status == FetchStatus::NoWifi ||
                           r.status == FetchStatus::HttpError || r.status == FetchStatus::ParseError;
    scheduleFetchIn(transient ? min(ERROR_RETRY_MS, refreshIntervalMs()) : refreshIntervalMs());
  }
  if (refetchQueued)
  {
    refetchQueued = false;
    scheduleFetchIn(0);
  }
}

static void formatUpdated(char *out, size_t len)
{
  if (forcedPercent >= 0)
  {
    snprintf(out, len, demoMode ? "Demo mode" : "Test value");
    return;
  }
  if (!hasUsage)
  {
    snprintf(out, len, "Waiting for data");
    return;
  }
  const char *label = usageFromCache ? "Cached" : "Updated";
  if (lastSuccessAt < 1704067200)
  {
    snprintf(out, len, "%s", label);
    return;
  }
  struct tm then;
  struct tm today;
  const time_t now = time(nullptr);
  localtime_r(&lastSuccessAt, &then);
  localtime_r(&now, &today);
  char when[16];
  if (then.tm_yday == today.tm_yday && then.tm_year == today.tm_year)
    strftime(when, sizeof(when), "%H:%M", &then);
  else
    strftime(when, sizeof(when), "%d %b %H:%M", &then);
  snprintf(out, len, "%s %s", label, when);
}

static void refreshDashboard()
{
  if (!dashboardVisible())
    return;

  DashboardData d{};
  d.limit = config.monthlyLimit;
  d.hasUsage = hasUsage;
  d.used = usedCredits;
  if (forcedPercent >= 0)
  {
    d.hasUsage = true;
    d.used = d.limit * forcedPercent / 100.0;
  }
  d.timeValid = timeSynced && monthInfoUtc(time(nullptr), d.month);
  d.wifiConnected = WiFi.status() == WL_CONNECTED;
  d.rssi = d.wifiConnected ? WiFi.RSSI() : 0;
  formatUpdated(d.updated, sizeof(d.updated));

  const bool stale = hasUsage && !usageFromCache && lastSuccessMs != 0 &&
                     millis() - lastSuccessMs > 2 * refreshIntervalMs() + 60000;
  if (forcedPercent >= 0)
  {
    snprintf(d.status, sizeof(d.status), "%s %.0f%%", demoMode ? "DEMO" : "TEST", forcedPercent);
    d.statusColor = theme::YELLOW;
  }
  else if (fetchBusy)
  {
    strlcpy(d.status, "Refreshing...", sizeof(d.status));
    d.statusColor = theme::LINK;
  }
  else if (!due(transientUntil))
  {
    strlcpy(d.status, transientText, sizeof(d.status));
    d.statusColor = theme::YELLOW;
  }
  else if (!d.wifiConnected)
  {
    strlcpy(d.status, "WiFi lost, retrying", sizeof(d.status));
    d.statusColor = theme::RED;
  }
  else if (lastFetchFailed)
  {
    strlcpy(d.status, fetchStatusText(lastStatus), sizeof(d.status));
    d.statusColor = hasUsage ? theme::ORANGE : theme::RED;
  }
  else if (stale)
  {
    strlcpy(d.status, "Data is stale", sizeof(d.status));
    d.statusColor = theme::ORANGE;
  }
  else
  {
    strlcpy(d.status, WiFi.localIP().toString().c_str(), sizeof(d.status));
    d.statusColor = theme::MUTED;
  }
  dashboardUpdate(d);
}

static void updateLed(uint32_t now)
{
  bool r = false;
  bool g = false;
  bool b = false;
  if (config.ledEnabled)
  {
    switch (state)
    {
    case AppState::AccessPoint: b = true; break;
    case AppState::Connecting: b = (now / 250) % 2; break;
    case AppState::NeedsSetup: g = b = true; break;
    case AppState::Running:
      if (!hasUsage && forcedPercent < 0)
        break;
      switch (dashboardMood())
      {
      case Mood::Happy:
      case Mood::Cruising: g = true; break;
      case Mood::Focused:
      case Mood::Worried: r = g = true; break;
      case Mood::Panic: r = true; break;
      case Mood::BurntOut: r = (now / 500) % 2; break;
      }
      break;
    }
  }
  static uint8_t last = 0xFF;
  const uint8_t bits = r | (g << 1) | (b << 2);
  if (bits != last)
  {
    rgbLedSet(r, g, b);
    last = bits;
  }
}

static void enterRunning()
{
  state = AppState::Running;
  stateSince = millis();
  dashboardShow();
  refreshDashboard();
  if (fetchBusy)
    refetchQueued = true;
  else
    scheduleFetchIn(0);
}

static void enterNeedsSetup()
{
  state = AppState::NeedsSetup;
  stateSince = millis();
  dashboardHide();
  screenFinishSetup(WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// Connected *and* has an address; status can read connected briefly while the IP is still 0.0.0.0.
static bool wifiReady()
{
  return WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0;
}

static void beginConnect()
{
  state = AppState::Connecting;
  stateSince = millis();
  lastTickAt = 0;
  dashboardHide();
  const String ssid = wifisetup::savedSsid();
  screenConnecting(ssid.c_str());
  if (WiFi.isConnected())
  {
    // e.g. the hotspot just closed while the station link stayed up
    Serial.printf("[app] still connected to %s\n", WiFi.SSID().c_str());
    return;
  }
  Serial.printf("[app] connecting to %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
}

static void openAccessPoint(uint16_t timeoutSeconds, const char *reason)
{
  state = AppState::AccessPoint;
  stateSince = millis();
  lastCountdown = -2;
  apCloseAt = 0;
  dashboardHide();
  wifisetup::startAccessPoint(timeoutSeconds);
  screenAccessPoint(wifisetup::apName(), wifisetup::apPassword(), reason);
}

static void onConnected()
{
  Serial.printf("[app] WiFi connected: %s, IP %s, RSSI %d\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  configTzTime(config.tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  if (!mdnsStarted && MDNS.begin(HOSTNAME))
  {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
  }
  wifisetup::startLanPortal();
  if (configIsComplete(config))
    enterRunning();
  else
    enterNeedsSetup();
}

static void onSettingsSaved()
{
  setenv("TZ", config.tz, 1);
  tzset();
  lastFetchFailed = false;
  if (state == AppState::NeedsSetup && configIsComplete(config))
  {
    enterRunning();
  }
  else if (state == AppState::Running)
  {
    if (!configIsComplete(config))
    {
      enterNeedsSetup();
      return;
    }
    if (fetchBusy)
      refetchQueued = true;
    else if (!startFetch("settings saved"))
      scheduleFetchIn(0);
    refreshDashboard();
  }
}

static void togglePortal()
{
  if (state == AppState::AccessPoint)
  {
    wifisetup::stopAccessPoint();
    if (wifisetup::hasSavedCredentials())
      beginConnect();
    else
      openAccessPoint(0, "No WiFi saved yet");
    return;
  }
  openAccessPoint(wifisetup::hasSavedCredentials() ? AP_MANUAL_SECONDS : 0, "Setup hotspot opened");
}

static void cycleBrightness()
{
  brightnessIndex = (brightnessIndex + 1) % sizeof(BRIGHTNESS_LEVELS);
  displaySetBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
}

static void printStatus()
{
  char hint[32];
  tokenHint(config.token, hint, sizeof(hint));
  Serial.printf("state=%u wifi=%s ip=%s rssi=%d time=%s\n", static_cast<unsigned>(state),
                WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "down", WiFi.localIP().toString().c_str(),
                WiFi.RSSI(), timeSynced ? "synced" : "unsynced");
  Serial.printf("user=%s token=%s limit=%lu refresh=%umin tz=%s led=%d\n", config.username, hint,
                static_cast<unsigned long>(config.monthlyLimit), config.refreshMinutes, config.tz, config.ledEnabled);
  Serial.printf("usage=%s %.2f (%.1f%%, %s) last=%s busy=%d next in %lds\n", hasUsage ? "yes" : "no", usedCredits,
                usagePercent(usedCredits, config.monthlyLimit), usageFromCache ? "cached" : "live",
                fetchStatusText(lastStatus), fetchBusy.load(),
                fetchPending ? static_cast<long>(static_cast<int32_t>(nextFetchAt - millis()) / 1000) : -1L);
  Serial.printf("heap=%u min=%u maxBlock=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
}

static void sendScreenshot()
{
  // Raw RGB565 rows read back from the ILI9341, framed so a host script can capture them.
  const int16_t w = tft.width();
  const int16_t h = tft.height();
  uint16_t row[320];
  Serial.printf("\nSCREENSHOT %d %d\n", w, h);
  for (int16_t y = 0; y < h; ++y)
  {
    tft.readRect(0, y, w, 1, row);
    Serial.write(reinterpret_cast<const uint8_t *>(row), w * sizeof(uint16_t));
  }
  Serial.println("\nSCREENSHOT END");
}

static void handleSetCommand(char *args)
{
  char *value = strchr(args, ' ');
  if (!value)
  {
    Serial.println("usage: set user|token|limit|refresh|tz|led <value>   (user/token \"-\" clears)");
    return;
  }
  *value++ = '\0';
  bool ok = true;
  const bool clear = strcmp(value, "-") == 0;
  if (clear && strcmp(args, "user") == 0)
    config.username[0] = '\0';
  else if (clear && strcmp(args, "token") == 0)
    config.token[0] = '\0';
  else if (strcmp(args, "user") == 0)
    ok = configSetUsername(config, value);
  else if (strcmp(args, "token") == 0)
    ok = configSetToken(config, value);
  else if (strcmp(args, "limit") == 0)
    ok = configSetLimit(config, value);
  else if (strcmp(args, "refresh") == 0)
    ok = configSetRefresh(config, value);
  else if (strcmp(args, "tz") == 0)
    ok = configSetTz(config, value);
  else if (strcmp(args, "led") == 0)
    config.ledEnabled = strcmp(value, "on") == 0 || strcmp(value, "1") == 0;
  else
    ok = false;

  if (!ok)
  {
    Serial.printf("invalid value for '%s'\n", args);
    return;
  }
  configSave(config);
  wifisetup::syncForm();
  Serial.printf("saved %s\n", args);
  onSettingsSaved();
}

static void httpSelfTest()
{
  WiFiClient client;
  const IPAddress ip = WiFi.localIP();
  if (!client.connect(ip, 80, 3000))
  {
    Serial.printf("[test] cannot connect to own web server at %s:80\n", ip.toString().c_str());
    return;
  }
  client.print("GET /param HTTP/1.0\r\nHost: copilot-meter\r\n\r\n");
  String response;
  const uint32_t start = millis();
  while (millis() - start < 5000 && (client.connected() || client.available()))
  {
    while (client.available())
      response += static_cast<char>(client.read());
    delay(10);
  }
  client.stop();
  const int eol = response.indexOf('\r');
  Serial.printf("[test] GET http://%s/param -> %s, %u bytes, settings form %s\n", ip.toString().c_str(),
                eol > 0 ? response.substring(0, eol).c_str() : "(no response)", response.length(),
                response.indexOf("gh_token") >= 0 ? "present" : "MISSING");
  Serial.println("[test] if this works but other devices time out, the WiFi isolates clients: use the hotspot");
}

static void httpSelfTestTask(void *)
{
  // Runs off the loop so the loop can keep serving the request.
  httpSelfTest();
  vTaskDelete(nullptr);
}

static void handleSerialCommand(char *line)
{
  if (strcmp(line, "refresh") == 0)
  {
    if (!startFetch("serial"))
      Serial.println("cannot refresh now (busy, offline or not configured)");
  }
  else if (strcmp(line, "status") == 0)
    printStatus();
  else if (strcmp(line, "screenshot") == 0)
    sendScreenshot();
  else if (strcmp(line, "httptest") == 0)
  {
    if (WiFi.status() != WL_CONNECTED || !wifisetup::lanPortalActive())
      Serial.println("[test] settings page is not running (needs WiFi)");
    else
      xTaskCreatePinnedToCore(httpSelfTestTask, "httptest", 6144, nullptr, 1, nullptr, 0);
  }
  else if (strcmp(line, "demo") == 0)
  {
    demoMode = !demoMode;
    demoIndex = 0;
    demoStepAt = millis();
    forcedPercent = demoMode ? DEMO_PERCENTS[0] : -1;
    Serial.printf("demo %s\n", demoMode ? "on" : "off");
    refreshDashboard();
  }
  else if (strncmp(line, "pct", 3) == 0)
  {
    demoMode = false;
    const char *arg = line + 3;
    while (*arg == ' ')
      ++arg;
    forcedPercent = (*arg == '\0' || strcmp(arg, "off") == 0) ? -1 : atof(arg);
    Serial.printf("forced percent: %.1f\n", forcedPercent);
    refreshDashboard();
  }
  else if (strncmp(line, "set ", 4) == 0)
    handleSetCommand(line + 4);
  else if (strcmp(line, "portal") == 0)
    togglePortal();
  else if (strcmp(line, "wipe") == 0)
  {
    Serial.println("erasing settings and WiFi, restarting");
    configErase();
    wifisetup::eraseCredentials();
    delay(200);
    ESP.restart();
  }
  else if (strcmp(line, "reboot") == 0)
    ESP.restart();
  else
    Serial.println("commands: refresh | status | demo | pct <n>|off | set user|token|limit|refresh|tz|led <value> |"
                   " portal | screenshot | httptest | wipe | reboot");
}

static void pollSerial()
{
  while (Serial.available())
  {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n')
    {
      if (serialLen > 0)
      {
        serialLine[serialLen] = '\0';
        handleSerialCommand(serialLine);
        serialLen = 0;
      }
    }
    else if (serialLen < sizeof(serialLine) - 1)
      serialLine[serialLen++] = c;
  }
}

// Returns true once per new touch.
static bool pollTap(uint32_t now)
{
  if (now - lastTouchPollAt < TOUCH_POLL_MS)
    return false;
  lastTouchPollAt = now;
  TouchPoint p;
  const bool pressed = touchRead(p);
  const bool tapped = pressed && !touchHeld;
  touchHeld = pressed;
  return tapped;
}

static void checkTimeSync()
{
  if (timeSynced || time(nullptr) < 1704067200)
    return;
  timeSynced = true;
  int year;
  int month;
  currentYearMonth(year, month);
  Serial.printf("[app] time synced, UTC month %d-%02d\n", year, month);
  if (usageFromCache && (cache.year != year || cache.month != month))
  {
    hasUsage = false;
    usageFromCache = false;
  }
  // A fetch can finish before SNTP does; date it now that the clock is right.
  if (lastSuccessMs != 0 && lastSuccessAt < 1704067200)
  {
    lastSuccessAt = time(nullptr) - static_cast<time_t>((millis() - lastSuccessMs) / 1000);
    cache.fetchedAt = lastSuccessAt;
    if (cache.valid)
      cacheSave(cache);
  }
  uiDirty = true;
  clampToMonthReset();
}

void setup()
{
  Serial.begin(115200);
  rgbLedInit();
  displayInit();
  touchInit();
  // Allocate the sprite before WiFi so it gets a contiguous block.
  dashboardBegin();

  configLoad(config);
  cacheLoad(cache);
  if (cache.valid)
  {
    hasUsage = true;
    usedCredits = cache.used;
    usageFromCache = true;
    lastSuccessAt = static_cast<time_t>(cache.fetchedAt);
  }
  setenv("TZ", config.tz, 1);
  tzset();

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  wifisetup::begin(config);

  bootButton.setPressMs(3000);
  bootButton.attachClick([]() { requestRefresh("button"); });
  bootButton.attachDoubleClick(cycleBrightness);
  bootButton.attachLongPressStart(togglePortal);

  Serial.printf("[app] Copilot meter starting, heap %u\n", ESP.getFreeHeap());
  if (wifisetup::hasSavedCredentials())
    beginConnect();
  else
    openAccessPoint(0, "No WiFi saved yet");
}

void loop()
{
  const uint32_t now = millis();
  bootButton.tick();
  pollSerial();

  const wifisetup::Events ev = wifisetup::process();
  if (ev.settingsSaved)
    onSettingsSaved();

  switch (state)
  {
  case AppState::Connecting:
    if (wifiReady())
      onConnected();
    else if (now - stateSince > CONNECT_TIMEOUT_MS)
    {
      char reason[64];
      snprintf(reason, sizeof(reason), "Couldn't join \"%s\"", wifisetup::savedSsid().c_str());
      openAccessPoint(AP_RETRY_SECONDS, reason);
    }
    else if (now - lastTickAt >= 250)
    {
      lastTickAt = now;
      screenConnectingTick(now - stateSince);
    }
    break;

  case AppState::AccessPoint:
    if (ev.connected)
      beginConnect();
    else if (ev.portalTimedOut)
      beginConnect();
    else if (ev.settingsSaved && wifisetup::hasSavedCredentials())
    {
      // Settings-only save (SSID left blank): give the browser a moment, then rejoin the saved WiFi.
      apCloseAt = (now + 3000) | 1;
      screenMessage("WiFi setup", "Settings saved", "Reconnecting to WiFi...");
    }
    else if (apCloseAt != 0 && due(apCloseAt))
    {
      apCloseAt = 0;
      wifisetup::stopAccessPoint();
      beginConnect();
    }
    else if (apCloseAt == 0)
    {
      const uint32_t left = wifisetup::accessPointSecondsLeft();
      const int32_t shown = (left == UINT32_MAX || WiFi.softAPgetStationNum() > 0) ? -1 : static_cast<int32_t>(left);
      if (shown != lastCountdown)
      {
        lastCountdown = shown;
        screenAccessPointCountdown(shown);
      }
    }
    break;

  case AppState::NeedsSetup:
    // Guest networks often isolate clients, so offer the hotspot with a tap.
    if (pollTap(now))
      togglePortal();
    break;

  case AppState::Running:
    if (pollTap(now))
      requestRefresh("tap");
    if (fetchPending && due(nextFetchAt) && WiFi.status() == WL_CONNECTED)
      startFetch("scheduled");
    break;
  }

  checkTimeSync();
  handleFetchResult();

  if (demoMode && now - demoStepAt >= DEMO_STEP_MS)
  {
    demoStepAt = now;
    demoIndex = (demoIndex + 1) % (sizeof(DEMO_PERCENTS) / sizeof(DEMO_PERCENTS[0]));
    forcedPercent = DEMO_PERCENTS[demoIndex];
    uiDirty = true;
  }

  if (uiDirty || now - lastUiAt >= UI_REFRESH_MS)
  {
    uiDirty = false;
    lastUiAt = now;
    refreshDashboard();
  }
  dashboardTick(now);
  updateLed(now);

  if (now - lastHeapLogAt >= 60000)
  {
    lastHeapLogAt = now;
    Serial.printf("[app] heap %u (min %u, max block %u)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  ESP.getMaxAllocHeap());
  }
  delay(1);
}
