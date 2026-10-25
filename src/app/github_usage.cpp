#include "github_usage.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "github_ca.h"
#include "github_usage_parse.h"

static constexpr const char *API_HOST = "https://api.github.com";
static constexpr const char *API_VERSION = "2026-03-10";
static constexpr const char *USER_AGENT = "CopilotMeter-CYD/1.0";
static constexpr uint16_t HTTP_TIMEOUT_MS = 15000;

static FetchStatus statusForHttpCode(int code, const String &rateRemaining)
{
  switch (code)
  {
  case 401: return FetchStatus::Unauthorized;
  case 404: return FetchStatus::NotFound;
  case 429: return FetchStatus::RateLimited;
  case 403: return rateRemaining == "0" ? FetchStatus::RateLimited : FetchStatus::Forbidden;
  default: return FetchStatus::HttpError;
  }
}

UsageResult fetchCopilotCredits(const char *username, const char *token)
{
  UsageResult result{FetchStatus::HttpError, 0, 0, 0, 0, 0, 0};
  if (!username || !username[0] || !token || !token[0])
  {
    result.status = FetchStatus::NotConfigured;
    return result;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    result.status = FetchStatus::NoWifi;
    return result;
  }

  WiFiClientSecure client;
  client.setCACert(GITHUB_ROOT_CAS);
  client.setHandshakeTimeout(HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  // HTTP/1.0 avoids chunked transfer encoding so ArduinoJson can parse straight from the stream.
  http.useHTTP10(true);
  http.setReuse(false);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent(USER_AGENT);

  String url = String(API_HOST) + "/users/" + username + "/settings/billing/ai_credit/usage";
  if (!http.begin(client, url))
  {
    result.status = FetchStatus::ConnectFailed;
    return result;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", API_VERSION);
  http.addHeader("Authorization", String("Bearer ") + token);
  const char *headerKeys[] = {"X-RateLimit-Remaining", "X-Accepted-GitHub-Permissions"};
  http.collectHeaders(headerKeys, 2);

  const uint32_t started = millis();
  const int code = http.GET();
  result.httpCode = code;

  if (code <= 0)
  {
    char tlsError[96] = "";
    client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[github] request failed: %s %s\n", HTTPClient::errorToString(code).c_str(), tlsError);
    result.status = FetchStatus::ConnectFailed;
    http.end();
    return result;
  }

  if (code != HTTP_CODE_OK)
  {
    const String body = http.getString();
    Serial.printf("[github] HTTP %d (needs: %s): %.200s\n", code,
                  http.header("X-Accepted-GitHub-Permissions").c_str(), body.c_str());
    result.status = statusForHttpCode(code, http.header("X-RateLimit-Remaining"));
    http.end();
    return result;
  }

  const DeserializationError err = parseCreditUsage(http.getStream(), result);
  http.end();
  if (err)
  {
    Serial.printf("[github] JSON error: %s\n", err.c_str());
    result.status = FetchStatus::ParseError;
    return result;
  }
  result.status = FetchStatus::Ok;

  Serial.printf("[github] %u items, gross %.2f, net %.2f credits (%d-%02d) in %lu ms\n",
                result.itemCount, result.grossCredits, result.netCredits, result.year, result.month,
                static_cast<unsigned long>(millis() - started));
  return result;
}

const char *fetchStatusText(FetchStatus status)
{
  switch (status)
  {
  case FetchStatus::Ok: return "OK";
  case FetchStatus::NotConfigured: return "Add username + token";
  case FetchStatus::NoWifi: return "No WiFi";
  case FetchStatus::ConnectFailed: return "Can't reach GitHub";
  case FetchStatus::Unauthorized: return "Token invalid (401)";
  case FetchStatus::Forbidden: return "Token needs Plan:read";
  case FetchStatus::NotFound: return "User/billing not found";
  case FetchStatus::RateLimited: return "Rate limited";
  case FetchStatus::HttpError: return "GitHub error";
  case FetchStatus::ParseError: return "Bad response";
  }
  return "?";
}
