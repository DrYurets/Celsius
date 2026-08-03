#ifndef CELSIUS_SYNC_PROGRESS_H
#define CELSIUS_SYNC_PROGRESS_H

#include <stdint.h>

enum SyncStepStatus : uint8_t {
  SYNC_STEP_WAIT = 0,
  SYNC_STEP_RUN,
  SYNC_STEP_OK,
  SYNC_STEP_FAIL,
  SYNC_STEP_SKIP
};

struct SyncProgressState {
  SyncStepStatus wifi;
  SyncStepStatus ntp;
  SyncStepStatus ota;
  SyncStepStatus weather;
  bool finished;
  bool overallOk;
};

#endif
