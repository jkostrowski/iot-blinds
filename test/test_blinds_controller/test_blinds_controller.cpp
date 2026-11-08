#include <unity.h>

#include <BlindsController.h>

void test_open_command_sets_negative_move() {
  BlindsController controller(8, 80);
  controller.setPosition(3);

  CommandType result = controller.applyCommand("OPEN");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Open), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(-3, controller.pendingMove());
}

void test_close_command_sets_positive_move() {
  BlindsController controller(8, 80);
  controller.setPosition(1);

  CommandType result = controller.applyCommand("CLOSE");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Close), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(7, controller.pendingMove());
}

void test_close50_command_halves_target() {
  BlindsController controller(8, 80);
  controller.setPosition(2);

  CommandType result = controller.applyCommand("CLOSE50");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Close50), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(2, controller.pendingMove());
}

void test_close110_command_applies_offset() {
  BlindsController controller(8, 80);
  controller.setPosition(5);

  CommandType result = controller.applyCommand("CLOSE110");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Close110), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(5, controller.pendingMove());
}

void test_negative_close_command() {
  BlindsController controller(8, 80);
  controller.setPosition(-2);

  CommandType result = controller.applyCommand("-CLOSE");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::CloseNeg), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(-6, controller.pendingMove());
}

void test_toggle_command_closes_from_open() {
  BlindsController controller(8, 80);
  controller.setPosition(0);

  CommandType result = controller.applyCommand("TOGGLE");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Toggle), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(8, controller.pendingMove());
}

void test_toggle_command_opens_from_closed() {
  BlindsController controller(8, 80);
  controller.setPosition(8);

  CommandType result = controller.applyCommand("TOGGLE");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Toggle), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(-8, controller.pendingMove());
}

void test_unknown_command_is_ignored() {
  BlindsController controller(8, 80);
  controller.setPosition(0);

  CommandType result = controller.applyCommand("NOPE");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Unknown), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(0, controller.pendingMove());
}

void test_commands_ignored_while_moving() {
  BlindsController controller(8, 80);
  controller.setPosition(0);
  controller.applyCommand("CLOSE");
  controller.processStepper();

  int before = controller.pendingMove();
  CommandType result = controller.applyCommand("OPEN");

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::IgnoredMoving), static_cast<int>(result));
  TEST_ASSERT_EQUAL_INT(before, controller.pendingMove());
}

void test_stepper_progression_updates_position_and_state() {
  BlindsController controller(8, 80);
  controller.setPosition(0);
  controller.applyCommand("CLOSE");

  for (int i = 0; i < 8; i++) {
    StepEvent event = controller.processStepper();
    TEST_ASSERT_TRUE(event.triggered);
    TEST_ASSERT_TRUE(event.forward);
    TEST_ASSERT_EQUAL_INT(80, event.steps);
  }

  TEST_ASSERT_EQUAL_INT(0, controller.pendingMove());
  TEST_ASSERT_EQUAL_INT(8, controller.position());
  TEST_ASSERT_TRUE(controller.isMoving());

  StepEvent final_event = controller.processStepper();
  TEST_ASSERT_FALSE(final_event.triggered);
  TEST_ASSERT_FALSE(controller.isMoving());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_open_command_sets_negative_move);
  RUN_TEST(test_close_command_sets_positive_move);
  RUN_TEST(test_close50_command_halves_target);
  RUN_TEST(test_close110_command_applies_offset);
  RUN_TEST(test_negative_close_command);
  RUN_TEST(test_toggle_command_closes_from_open);
  RUN_TEST(test_toggle_command_opens_from_closed);
  RUN_TEST(test_unknown_command_is_ignored);
  RUN_TEST(test_commands_ignored_while_moving);
  RUN_TEST(test_stepper_progression_updates_position_and_state);
  return UNITY_END();
}
