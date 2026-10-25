#include "usage_math.h"

#include <math.h>
#include <stdio.h>

float usagePercent(double used, double limit)
{
  if (limit <= 0 || used <= 0)
    return 0.0f;
  return static_cast<float>(used * 100.0 / limit);
}

Mood moodForPercent(float percent)
{
  if (percent >= 100.0f) return Mood::BurntOut;
  if (percent >= 90.0f) return Mood::Panic;
  if (percent >= 75.0f) return Mood::Worried;
  if (percent >= 50.0f) return Mood::Focused;
  if (percent >= 25.0f) return Mood::Cruising;
  return Mood::Happy;
}

const char *moodName(Mood mood)
{
  switch (mood)
  {
  case Mood::Happy: return "Happy";
  case Mood::Cruising: return "Cruising";
  case Mood::Focused: return "Focused";
  case Mood::Worried: return "Worried";
  case Mood::Panic: return "Panic!";
  case Mood::BurntOut: return "Burnt out";
  }
  return "?";
}

// Howard Hinnant's days_from_civil: days since 1970-01-01 for a proleptic Gregorian date.
int64_t daysFromCivil(int year, int month, int day)
{
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const int64_t yoe = year - era * 400;
  const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

time_t utcMonthStart(int year, int month)
{
  return static_cast<time_t>(daysFromCivil(year, month, 1) * 86400);
}

bool monthInfoUtc(time_t now, MonthInfo &out)
{
  // Anything before 2024 means SNTP has not set the clock yet.
  if (now < 1704067200)
    return false;

  struct tm utc;
  gmtime_r(&now, &utc);
  out.year = utc.tm_year + 1900;
  out.month = utc.tm_mon + 1;

  const int nextYear = out.month == 12 ? out.year + 1 : out.year;
  const int nextMonth = out.month == 12 ? 1 : out.month + 1;
  const time_t start = utcMonthStart(out.year, out.month);
  const time_t end = utcMonthStart(nextYear, nextMonth);

  out.nextResetUtc = end;
  out.secondsToReset = static_cast<int64_t>(end - now);
  out.elapsedFraction = static_cast<double>(now - start) / static_cast<double>(end - start);
  return true;
}

PaceInfo computePace(double used, double limit, double elapsedFraction)
{
  PaceInfo info{Pace::Unknown, 0.0f, 0.0f, false, 0.0};
  if (elapsedFraction <= 0.0 || elapsedFraction > 1.0)
    return info;

  info.expectedPercent = static_cast<float>(elapsedFraction * 100.0);
  if (elapsedFraction < PROJECTION_MIN_ELAPSED)
    return info;

  info.projectionValid = true;
  info.projected = used / elapsedFraction;
  if (limit <= 0)
    return info;

  info.deltaPercent = usagePercent(used, limit) - info.expectedPercent;
  if (info.deltaPercent > PACE_TOLERANCE_PERCENT)
    info.pace = Pace::Fast;
  else if (info.deltaPercent < -PACE_TOLERANCE_PERCENT)
    info.pace = Pace::Under;
  else
    info.pace = Pace::OnTrack;
  return info;
}

const char *paceLabel(Pace pace)
{
  switch (pace)
  {
  case Pace::Under: return "Under pace";
  case Pace::OnTrack: return "On track";
  case Pace::Fast: return "Burning fast";
  default: return "Pace: --";
  }
}

void formatThousands(double value, char *out, size_t len)
{
  if (len == 0)
    return;
  const unsigned long long whole = value > 0 ? static_cast<unsigned long long>(llround(value)) : 0ULL;

  char digits[24];
  const int count = snprintf(digits, sizeof(digits), "%llu", whole);
  size_t pos = 0;
  for (int i = 0; i < count && pos + 1 < len; ++i)
  {
    if (i > 0 && (count - i) % 3 == 0)
    {
      out[pos++] = ',';
      if (pos + 1 >= len)
        break;
    }
    out[pos++] = digits[i];
  }
  out[pos] = '\0';
}

void formatCompact(double value, char *out, size_t len)
{
  if (value < 0)
    value = 0;
  if (value >= 999950.0)
    snprintf(out, len, "%.1fM", value / 1000000.0);
  else if (value >= 99950.0)
    snprintf(out, len, "%.0fk", value / 1000.0);
  else if (value >= 999.5)
    snprintf(out, len, "%.1fk", value / 1000.0);
  else
    snprintf(out, len, "%.0f", value);
}

void formatDuration(int64_t seconds, char *out, size_t len)
{
  if (seconds < 0)
    seconds = 0;
  const long days = static_cast<long>(seconds / 86400);
  const long hours = static_cast<long>((seconds % 86400) / 3600);
  const long minutes = static_cast<long>((seconds % 3600) / 60);

  if (days >= 2)
    snprintf(out, len, "%ld days", days);
  else if (days == 1)
    snprintf(out, len, "1 day %ldh", hours);
  else if (hours > 0)
    snprintf(out, len, "%ldh %ldm", hours, minutes);
  else
    snprintf(out, len, "%ldm", minutes);
}
