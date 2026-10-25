#pragma once

#include <stdint.h>

enum class FetchStatus : uint8_t
{
  Ok,
  NotConfigured,
  NoWifi,
  ConnectFailed, // DNS/TCP/TLS failure or timeout
  Unauthorized,  // 401: token wrong/expired
  Forbidden,     // 403: token lacks "Plan: read"
  NotFound,      // 404: wrong username or no personal Copilot billing
  RateLimited,
  HttpError,
  ParseError
};

struct UsageResult
{
  FetchStatus status;
  int httpCode;
  double grossCredits; // all AI credits consumed this month
  double netCredits;   // portion billed beyond the included allowance
  uint16_t itemCount;
  int year;
  int month;
};

// Blocking HTTPS call (~1-3 s); run it off the UI loop.
UsageResult fetchCopilotCredits(const char *username, const char *token);
const char *fetchStatusText(FetchStatus status);
