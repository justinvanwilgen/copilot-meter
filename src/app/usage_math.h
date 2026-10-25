#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Pure helpers (no Arduino dependencies) so they can be unit tested on the host.

enum class Mood : uint8_t
{
  Happy,    // 0 - <25 %
  Cruising, // 25 - <50 %
  Focused,  // 50 - <75 %
  Worried,  // 75 - <90 %
  Panic,    // 90 - <100 %
  BurntOut  // >= 100 %
};
constexpr uint8_t MOOD_COUNT = 6;

enum class Pace : uint8_t
{
  Unknown, // clock not set or too early in the month to judge
  Under,
  OnTrack,
  Fast
};

struct PaceInfo
{
  Pace pace;
  float expectedPercent; // where usage "should" be if spread evenly over the month
  float deltaPercent;    // used % minus expected %
  bool projectionValid;
  double projected;      // credits at month end at the current rate
};

struct MonthInfo
{
  int year;
  int month;              // 1-12
  double elapsedFraction; // 0..1 through the current UTC month
  int64_t secondsToReset; // until 00:00 UTC on the 1st of next month
  time_t nextResetUtc;
};

constexpr float PACE_TOLERANCE_PERCENT = 5.0f;
constexpr double PROJECTION_MIN_ELAPSED = 0.02;

float usagePercent(double used, double limit);
Mood moodForPercent(float percent);
const char *moodName(Mood mood);

int64_t daysFromCivil(int year, int month, int day);
time_t utcMonthStart(int year, int month);
bool monthInfoUtc(time_t now, MonthInfo &out);

PaceInfo computePace(double used, double limit, double elapsedFraction);
const char *paceLabel(Pace pace);

// "12,345" (rounded to the nearest whole credit).
void formatThousands(double value, char *out, size_t len);
// "950", "18.4k", "1.2M".
void formatCompact(double value, char *out, size_t len);
// "6 days", "1 day 5h", "5h 12m", "12m".
void formatDuration(int64_t seconds, char *out, size_t len);
