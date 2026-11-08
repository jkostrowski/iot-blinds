#include "BlindsController.h"

#include <string.h>

BlindsController::BlindsController(int steps_to_close, int step_size)
    : steps_to_close_(steps_to_close),
      step_size_(step_size),
      position_(0),
      move_(0),
      moving_(false) {}

CommandType BlindsController::applyCommand(const char *payload) {
  if (payload == nullptr) {
    return CommandType::Unknown;
  }

  if (moving_) {
    return CommandType::IgnoredMoving;
  }

  if (strcmp(payload, "OPEN") == 0) {
    move_ = 0 - position_;
    return CommandType::Open;
  }
  if (strcmp(payload, "CLOSE") == 0) {
    move_ = steps_to_close_ - position_;
    return CommandType::Close;
  }
  if (strcmp(payload, "CLOSE50") == 0) {
    move_ = (steps_to_close_ / 2) - position_;
    return CommandType::Close50;
  }
  if (strcmp(payload, "CLOSE110") == 0) {
    move_ = steps_to_close_ - position_ + 2;
    return CommandType::Close110;
  }
  if (strcmp(payload, "-CLOSE") == 0) {
    move_ = -steps_to_close_ - position_;
    return CommandType::CloseNeg;
  }
  if (strcmp(payload, "TOGGLE") == 0) {
    int target = (position_ != steps_to_close_) ? steps_to_close_ : 0;
    move_ = target - position_;
    return CommandType::Toggle;
  }

  return CommandType::Unknown;
}

StepEvent BlindsController::processStepper() {
  StepEvent event{false, false, 0};

  if (move_ > 0) {
    event = {true, true, step_size_};
    move_--;
    if (position_ < steps_to_close_) {
      position_++;
    }
    moving_ = true;
    return event;
  }

  if (move_ < 0) {
    event = {true, false, step_size_};
    move_++;
    if (position_ > -steps_to_close_) {
      position_--;
    }
    moving_ = true;
    return event;
  }

  if (move_ == 0 && moving_) {
    moving_ = false;
  }

  return event;
}

void BlindsController::setPosition(int position) {
  position_ = position;
}

void BlindsController::calibratePosition() {
  position_ = -steps_to_close_;
  move_ = 2 * steps_to_close_;
}

int BlindsController::position() const {
  return position_;
}

int BlindsController::pendingMove() const {
  return move_;
}

bool BlindsController::isMoving() const {
  return moving_;
}
