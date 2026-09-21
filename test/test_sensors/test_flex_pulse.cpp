#include "../test_utils.h"
#include "globals.h"
#include "sensors.h"
#include "src/pins/boardInputPin.h"

extern boardInputPin_t flex_pin;
extern volatile uint32_t flexStartTime;
extern volatile uint32_t flexLastValidRising;
extern volatile uint32_t flexLastRising;

static void flex_set_high(void)
{
  flex_pin._pin.setPinHigh();
}

static void flex_set_low(void)
{
  flex_pin._pin.setPinLow();
}

static void delay_us(uint32_t us)
{
  while (us > 10000UL)
  {
    delayMicroseconds(10000);
    us -= 10000UL;
  }
  if (us > 0UL)
  {
    delayMicroseconds((unsigned int)us);
  }
}

static void reset_flex_test_state(void)
{
  pinNumbers.pinFlex = 2U;
  configPage2.flexEnabled = true;
  configPage4.FILTER_FLEX = 0U;
  currentStatus.ethanolPct = 0U;
  flexCounter = 0U;
  flexPulseWidth = 0UL;
  flex_set_high();
  initialiseFlexSensor(configPage2, currentStatus, 2U);
  delay_us(1000);
}

static void send_flex_pulse(uint32_t low_us, uint32_t high_us)
{
  flex_set_low();
  flexPulse();
  delay_us(low_us);
  flex_set_high();
  flexPulse();
  if (high_us > 0UL)
  {
    delay_us(high_us);
  }
}

static void test_flex_pulse_valid_single_pulse(void)
{
  reset_flex_test_state();

  send_flex_pulse(2000, 0);

  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);
  TEST_ASSERT_UINT32_WITHIN(100UL, 2000UL, flexPulseWidth);
}

static void test_flex_pulse_rejection_pulse_too_short(void)
{
  reset_flex_test_state();

  // 1. High frequency noise glitch: 50us (far below FLEX_MIN_PULSE_WIDTH 800us)
  send_flex_pulse(50, 0);

  TEST_ASSERT_EQUAL_UINT8(0U, flexCounter);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexPulseWidth);

  // 2. Just below threshold: 750us
  delay_us(6000); // allow period to pass
  send_flex_pulse(750, 0);

  TEST_ASSERT_EQUAL_UINT8(0U, flexCounter);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexPulseWidth);
}

static void test_flex_pulse_rejection_pulse_too_long(void)
{
  reset_flex_test_state();

  // Pulse of 7000us exceeds FLEX_MAX_PULSE_WIDTH (6000us)
  send_flex_pulse(7000, 0);

  TEST_ASSERT_EQUAL_UINT8(0U, flexCounter);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexPulseWidth);
}

static void test_flex_pulse_rejection_period_too_short(void)
{
  reset_flex_test_state();

  // First pulse: valid 1500us
  send_flex_pulse(1500, 1500);

  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);

  // Second pulse sent too soon: only 1500us after first rising edge (total period ~3000us < 5000us)
  send_flex_pulse(1500, 0);

  // Second pulse must be rejected by period check
  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);
}

static void test_flex_pulse_boundaries_min_and_max_pulse_width(void)
{
  reset_flex_test_state();

  // Near minimum valid pulse: 900us
  send_flex_pulse(900, 6000);

  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);
  TEST_ASSERT_UINT32_WITHIN(100UL, 900UL, flexPulseWidth);

  // Near maximum valid pulse: 5500us (covers 125C max temperature and sensor error pulse widths)
  send_flex_pulse(5500, 0);

  TEST_ASSERT_EQUAL_UINT8(2U, flexCounter);
  TEST_ASSERT_UINT32_WITHIN(150UL, 5500UL, flexPulseWidth);
}

static void test_flex_pulse_glitch_high_does_not_corrupt_pulse_width(void)
{
  reset_flex_test_state();

  // Legitimate pulse starts: pin goes LOW
  flex_set_low();
  flexPulse();
  delay_us(500);

  // A brief positive glitch occurs: pin pops HIGH for 20us (<800us)
  flex_set_high();
  flexPulse();
  delay_us(20);

  // Glitch ends: pin goes back LOW. Because high time was only 20us (<500us),
  // flexStartTime must NOT be reset!
  flex_set_low();
  flexPulse();
  delay_us(1500);

  // Legitimate rising edge: total low duration was ~2020us
  flex_set_high();
  flexPulse();

  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);
  // Pulse width should reflect the full pulse (~2020us), not the truncated ~1500us
  TEST_ASSERT_UINT32_WITHIN(100UL, 2020UL, flexPulseWidth);
}

static void test_flex_pulse_noise_burst_rejected(void)
{
  reset_flex_test_state();

  // Simulate a 100kHz noise burst (10us cycle: 5us low, 5us high) repeated 20 times
  for (uint8_t i = 0U; i < 20U; ++i)
  {
    send_flex_pulse(5, 5);
  }

  // Not a single pulse should be accepted
  TEST_ASSERT_EQUAL_UINT8(0U, flexCounter);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexPulseWidth);
}

static void test_flex_pulse_normal_sequence_50hz(void)
{
  reset_flex_test_state();

  // 50Hz: 20000us period, 1500us low, 18500us high
  for (uint8_t i = 0U; i < 3U; ++i)
  {
    send_flex_pulse(1500, 18500);
  }

  TEST_ASSERT_EQUAL_UINT8(3U, flexCounter);
  TEST_ASSERT_UINT32_WITHIN(100UL, 1500UL, flexPulseWidth);
}

static void test_flex_pulse_normal_sequence_150hz(void)
{
  reset_flex_test_state();

  // 150Hz: 6667us period, 3500us low, 3167us high
  for (uint8_t i = 0U; i < 3U; ++i)
  {
    send_flex_pulse(3500, 3167);
  }

  TEST_ASSERT_EQUAL_UINT8(3U, flexCounter);
  TEST_ASSERT_UINT32_WITHIN(100UL, 3500UL, flexPulseWidth);
}

static void test_flex_pulse_filter_flex_smoothing(void)
{
  reset_flex_test_state();
  configPage4.FILTER_FLEX = 128U; // 50% smoothing
  flexPulseWidth = 1000UL;

  // Send a pulse of 3000us
  send_flex_pulse(3000, 0);

  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);
  // (3000 * 128 + 1000 * 128) / 256 = 2000
  TEST_ASSERT_UINT32_WITHIN(100UL, 2000UL, flexPulseWidth);
}

static void test_initialiseFlexSensor_resets_timestamps(void)
{
  reset_flex_test_state();
  currentStatus.ethanolPct = 50U;
  initialiseFlexSensor(configPage2, currentStatus, 2U);
  delay_us(1000);

  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);

  // After init, first pulse should be accepted without false period rejection
  send_flex_pulse(2000, 0);

  TEST_ASSERT_EQUAL_UINT8(1U, flexCounter);
  TEST_ASSERT_UINT32_WITHIN(100UL, 2000UL, flexPulseWidth);
}

void test_flex_pulse(void)
{
  SET_UNITY_FILENAME()
  {
    RUN_TEST(test_flex_pulse_valid_single_pulse);
    RUN_TEST(test_flex_pulse_rejection_pulse_too_short);
    RUN_TEST(test_flex_pulse_rejection_pulse_too_long);
    RUN_TEST(test_flex_pulse_rejection_period_too_short);
    RUN_TEST(test_flex_pulse_boundaries_min_and_max_pulse_width);
    RUN_TEST(test_flex_pulse_glitch_high_does_not_corrupt_pulse_width);
    RUN_TEST(test_flex_pulse_noise_burst_rejected);
    RUN_TEST(test_flex_pulse_normal_sequence_50hz);
    RUN_TEST(test_flex_pulse_normal_sequence_150hz);
    RUN_TEST(test_flex_pulse_filter_flex_smoothing);
    RUN_TEST(test_initialiseFlexSensor_resets_timestamps);
  }
}
