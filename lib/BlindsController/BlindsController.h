#pragma once

#include <stddef.h>

enum class CommandType {
  None = 0,
  Open,
  Close,
  Close50,
  Close110,
  CloseNeg,
  Toggle,
  IgnoredMoving,
  Unknown
};

struct StepEvent {
  bool triggered;
  bool forward;
  int steps;
};

class BlindsController {
public:
  BlindsController(int steps_to_close, int step_size);

  CommandType applyCommand(const char *payload);
  StepEvent processStepper();

  void setPosition(int position);
  void calibratePosition();

  int position() const;
  int pendingMove() const;
  bool isMoving() const;

private:
  int steps_to_close_;
  int step_size_;
  int position_;
  int move_;
  bool moving_;
};
