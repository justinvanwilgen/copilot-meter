#include "wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

namespace wifisetup
{
static WiFiManager wm;
static AppConfig *config = nullptr;
static Events pending{};

static char apSsid[24];
static char apPass[12];
static bool apActive = false;
static uint32_t apTimeoutMs = 0;
static uint32_t apIdleSince = 0;

// WiFiManagerParameter keeps pointers to these, so they must outlive the portal.
static char tokenAttrs[160];
static char ledAttrs[96];

static const char HEAD_HTML[] =
  "<style>input[type=checkbox]{width:auto;margin:0 8px 0 0;vertical-align:middle}"
  ".hint{color:#8b949e;font-size:.9em}</style>";
static const char INTRO_HTML[] =
  "<h3>Copilot meter</h3><p class='hint'>Create a <b>fine-grained</b> personal access token at "
  "<a href='https://github.com/settings/personal-access-tokens/new' target='_blank'>"
  "github.com/settings/personal-access-tokens/new</a> with account permission <b>Plan: Read-only</b> "
  "and nothing else. The token is never shown again here.</p>"
  "<p class='hint'>Only changing these settings? Leave the SSID field empty to keep the current WiFi.</p>";

static WiFiManagerParameter pIntro(INTRO_HTML);
static WiFiManagerParameter pUser("gh_user", "GitHub username", "", USERNAME_LEN - 1,
                                  "autocapitalize='none' autocomplete='off' spellcheck='false'");
static WiFiManagerParameter pToken("gh_token", "Token (leave blank to keep the saved one)", "", TOKEN_LEN - 1,
                                   tokenAttrs);
static WiFiManagerParameter pLimit("limit", "Monthly AI credit allowance (Max = 20000)", "", 10,
                                   "type='number' min='1' step='1'");
static WiFiManagerParameter pRefresh("refresh", "Refresh every N minutes (5-1440)", "", 4,
                                     "type='number' min='5' max='1440'");
static WiFiManagerParameter pTz("tz", "Timezone, POSIX TZ (SAST-2, CET-1CEST,M3.5.0,M10.5.0/3)", "", TZ_LEN - 1,
                                "autocapitalize='none' spellcheck='false'");
static WiFiManagerParameter pLed("led", "RGB LED shows mood", "1", 1, ledAttrs, WFM_LABEL_AFTER);

static const char *AP_MENU[] = {"wifi", "info", "sep", "restart"};
// No "wifi" entry on the LAN: changing networks goes through the AP portal (hold BOOT).
static const char *LAN_MENU[] = {"param", "info", "sep", "restart"};

static void refreshFormValues()
{
  char number[12];
  pUser.setValue(config->username, USERNAME_LEN - 1);
  pToken.setValue("", TOKEN_LEN - 1);
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(config->monthlyLimit));
  pLimit.setValue(number, 10);
  snprintf(number, sizeof(number), "%u", config->refreshMinutes);
  pRefresh.setValue(number, 4);
  pTz.setValue(config->tz, TZ_LEN - 1);
  pLed.setValue("1", 1);

  char hint[32];
  tokenHint(config->token, hint, sizeof(hint));
  snprintf(tokenAttrs, sizeof(tokenAttrs),
           "type='password' autocomplete='new-password' spellcheck='false' placeholder='%s'", hint);
  snprintf(ledAttrs, sizeof(ledAttrs), "type='checkbox'%s", config->ledEnabled ? " checked" : "");
}

static void onSaveParams()
{
  configSetUsername(*config, pUser.getValue());
  const bool tokenEntered = pToken.getValue()[0] != '\0';
  if (tokenEntered && !configSetToken(*config, pToken.getValue()))
    Serial.println("[wifi] token rejected: unexpected characters");
  configSetLimit(*config, pLimit.getValue());
  configSetRefresh(*config, pRefresh.getValue());
  configSetTz(*config, pTz.getValue());
  config->ledEnabled = strcmp(pLed.getValue(), "1") == 0;

  configSave(*config);
  refreshFormValues();
  pending.settingsSaved = true;
  Serial.printf("[wifi] settings saved: user=%s token=%s limit=%lu refresh=%u tz=%s led=%d\n", config->username,
                config->token[0] ? "set" : "missing", static_cast<unsigned long>(config->monthlyLimit),
                config->refreshMinutes, config->tz, config->ledEnabled);
}

void syncForm()
{
  if (config)
    refreshFormValues();
}

void begin(AppConfig &cfg)
{
  config = &cfg;

  const uint64_t mac = ESP.getEfuseMac();
  snprintf(apSsid, sizeof(apSsid), "CopilotMeter-%02X%02X", static_cast<uint8_t>(mac >> 32),
           static_cast<uint8_t>(mac >> 40));

  refreshFormValues();
  wm.addParameter(&pIntro);
  wm.addParameter(&pUser);
  wm.addParameter(&pToken);
  wm.addParameter(&pLimit);
  wm.addParameter(&pRefresh);
  wm.addParameter(&pTz);
  wm.addParameter(&pLed);

  // WiFiManager debug output would print submitted form values, including the token.
  wm.setDebugOutput(false);
  wm.setTitle("Copilot Meter");
  wm.setDarkMode(true);
  wm.setCustomHeadElement(HEAD_HTML);
  wm.setHostname("copilot-meter");
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0);
  wm.setConnectTimeout(20);
  wm.setSaveConnectTimeout(20);
  wm.setShowInfoUpdate(false);
  wm.setShowInfoErase(false);
  wm.setWiFiAutoReconnect(true);
  wm.setSaveParamsCallback(onSaveParams);
}

bool hasSavedCredentials()
{
  return wm.getWiFiIsSaved();
}

String savedSsid()
{
  return wm.getWiFiSSID(true);
}

void eraseCredentials()
{
  wm.resetSettings();
}

void startAccessPoint(uint16_t timeoutSeconds)
{
  if (apActive)
    return;
  stopLanPortal();
  configApPassword(apPass, sizeof(apPass));

  // Settings live on the WiFi page here so first-time setup is a single form.
  wm.setParamsPage(false);
  wm.setMenu(AP_MENU, sizeof(AP_MENU) / sizeof(AP_MENU[0]));
  refreshFormValues();

  wm.startConfigPortal(apSsid, apPass);
  apActive = true;
  apTimeoutMs = static_cast<uint32_t>(timeoutSeconds) * 1000;
  apIdleSince = millis();
  Serial.printf("[wifi] access point %s started (timeout %us)\n", apSsid, timeoutSeconds);
}

void stopAccessPoint()
{
  if (!apActive)
    return;
  wm.stopConfigPortal();
  apActive = false;
}

bool accessPointActive()
{
  return apActive;
}

const char *apName()
{
  return apSsid;
}

const char *apPassword()
{
  return apPass;
}

uint32_t accessPointSecondsLeft()
{
  if (!apActive || apTimeoutMs == 0)
    return UINT32_MAX;
  const uint32_t idle = millis() - apIdleSince;
  return idle >= apTimeoutMs ? 0 : (apTimeoutMs - idle) / 1000;
}

void startLanPortal()
{
  if (apActive || wm.getWebPortalActive())
    return;
  wm.setMenu(LAN_MENU, sizeof(LAN_MENU) / sizeof(LAN_MENU[0]));
  refreshFormValues();
  wm.startWebPortal();
  Serial.printf("[wifi] settings page at http://%s/param\n", WiFi.localIP().toString().c_str());
}

void stopLanPortal()
{
  if (wm.getWebPortalActive())
    wm.stopWebPortal();
}

bool lanPortalActive()
{
  return wm.getWebPortalActive();
}

Events process()
{
  if (apActive)
  {
    if (WiFi.softAPgetStationNum() > 0)
      apIdleSince = millis();
    if (wm.process())
    {
      // WiFiManager already closes the AP after a successful join; make sure regardless.
      if (wm.getConfigPortalActive())
        wm.stopConfigPortal();
      apActive = false;
      pending.connected = true;
      Serial.printf("[wifi] joined %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
    else if (apTimeoutMs > 0 && millis() - apIdleSince >= apTimeoutMs)
    {
      stopAccessPoint();
      pending.portalTimedOut = true;
      Serial.println("[wifi] access point timed out");
    }
  }
  else if (wm.getWebPortalActive())
  {
    wm.process();
  }

  const Events out = pending;
  pending = Events{};
  return out;
}
} // namespace wifisetup
