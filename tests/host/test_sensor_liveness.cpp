#include <assert.h>
#include <stdint.h>

#include "../../firmware/waveshare-hub/include/sensor_liveness.h"

using namespace espplants_sensor_liveness;

int main() {
  Tracker tracker{};

  assert(timeoutMs(tracker) == 2UL * 60UL * 60UL * 1000UL);
  assert(!shouldBeStale(tracker, false, 0, 9999999));

  const uint32_t first = 1000;
  noteReport(tracker, 0, first);
  assert(tracker.learnedSamples == 0);
  assert(!tracker.stale);

  // Burst packets are not treated as the normal sleep/report cadence.
  noteReport(tracker, first, first + 5000);
  assert(tracker.learnedSamples == 0);

  // A five-minute reporting interval learns a conservative 15-minute timeout.
  const uint32_t fiveMinutes = 5UL * 60UL * 1000UL;
  noteReport(tracker, first + 5000, first + 5000 + fiveMinutes);
  assert(tracker.learnedCadenceMs == fiveMinutes);
  assert(tracker.learnedSamples == 1);
  // Do not trust a single early/event-driven interval. Stay conservative until
  // several separate report cycles have been observed.
  assert(timeoutMs(tracker) == 2UL * 60UL * 60UL * 1000UL);

  const uint32_t secondSeen = first + 5000 + fiveMinutes;
  const uint32_t tenMinutes = 10UL * 60UL * 1000UL;
  noteReport(tracker, secondSeen, secondSeen + tenMinutes);
  assert(tracker.learnedCadenceMs == tenMinutes);
  assert(tracker.learnedSamples == 2);
  assert(timeoutMs(tracker) == 2UL * 60UL * 60UL * 1000UL);

  // A third separate interval activates the learned cadence.
  const uint32_t thirdSeen = secondSeen + tenMinutes;
  noteReport(tracker, thirdSeen, thirdSeen + fiveMinutes);
  assert(tracker.learnedSamples == 3);
  assert(timeoutMs(tracker) == 30UL * 60UL * 1000UL);
  const uint32_t lastSeen = thirdSeen + fiveMinutes;
  assert(!shouldBeStale(tracker, true, lastSeen,
                        lastSeen + 30UL * 60UL * 1000UL - 1));
  assert(shouldBeStale(tracker, true, lastSeen,
                       lastSeen + 30UL * 60UL * 1000UL));

  // One-hour cadence is allowed and produces the hard three-hour ceiling.
  const uint32_t oneHour = 60UL * 60UL * 1000UL;
  noteReport(tracker, lastSeen, lastSeen + oneHour);
  assert(tracker.learnedCadenceMs == oneHour);
  assert(timeoutMs(tracker) == 3UL * 60UL * 60UL * 1000UL);

  // Recovery after a stale period is immediate, but the outage itself is not
  // learned as the sensor's new normal cadence.
  tracker.stale = true;
  const uint32_t learnedBeforeRecovery = tracker.learnedCadenceMs;
  const uint8_t samplesBeforeRecovery = tracker.learnedSamples;
  noteReport(tracker, 1000, 1000 + 12UL * 60UL * 60UL * 1000UL);
  assert(!tracker.stale);
  assert(tracker.learnedCadenceMs == learnedBeforeRecovery);
  assert(tracker.learnedSamples == samplesBeforeRecovery);

  return 0;
}
