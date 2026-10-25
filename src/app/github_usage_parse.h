#pragma once

#include <ArduinoJson.h>

#include "github_usage.h"

// Sums AI credit usage items. Works with an Arduino Stream or, for host tests, a C string.
template <typename TInput>
DeserializationError parseCreditUsage(TInput &&input, UsageResult &result)
{
  JsonDocument filter;
  filter["timePeriod"]["year"] = true;
  filter["timePeriod"]["month"] = true;
  filter["usageItems"][0]["grossQuantity"] = true;
  filter["usageItems"][0]["netQuantity"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter));
  if (err)
    return err;

  JsonArrayConst items = doc["usageItems"].as<JsonArrayConst>();
  if (items.isNull())
    return DeserializationError::InvalidInput;

  result.grossCredits = 0;
  result.netCredits = 0;
  result.itemCount = 0;
  for (JsonObjectConst item : items)
  {
    result.grossCredits += item["grossQuantity"] | 0.0;
    result.netCredits += item["netQuantity"] | 0.0;
    ++result.itemCount;
  }
  result.year = doc["timePeriod"]["year"] | 0;
  result.month = doc["timePeriod"]["month"] | 0;
  return DeserializationError::Ok;
}
