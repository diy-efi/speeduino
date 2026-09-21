#include "../test_utils.h"
#include "globals.h"
#include "sensors.h"
#include "setup_oneMsInterval.h"

static void test_flex_low_freq_yields_zero_pct(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;        // No filter, immediate
  flexCounter = 10U;                    // Below flexFreqLow
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 100U;

  // During hold (< 5 seconds), reading is maintained
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(100U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexCounter);

  // After 5-second hold expires (6th second of error), reading drops to 0%
  run_n_intervals(5000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexCounter);
}

static void test_flex_in_band_yields_pct(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 75U;                    // 25% ethanol
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 0U;

  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(25U, currentStatus.ethanolPct);
}

static void test_flex_over_range_error_yields_zero_pct(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 200U;                   // > flexFreqHigh+1 and >= flexFreqHigh+20 -> error
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 50U;

  // During hold (< 5 seconds), reading is maintained
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(50U, currentStatus.ethanolPct);

  // After 5-second hold expires (6th second of error), reading drops to 0%
  run_n_intervals(5000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexCounter);
}

static void test_flex_saturation_band_yields_100_pct(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 160U;                   // Saturation band (between 152 and 169 Hz) -> 100% ethanol
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 0U;

  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(100U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexCounter);
}

static void test_flex_1_pulse_buffer_yields_100_pct(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 151U;                   // 1 pulse buffer above flexFreqHigh (150Hz) -> capped at 100%
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 0U;

  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(100U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexCounter);
}

static void test_flex_error_at_170_yields_zero_pct(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 170U;                   // Exactly 20Hz above flexFreqHigh -> error condition
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 50U;

  // During hold (< 5 seconds), reading is maintained
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(50U, currentStatus.ethanolPct);

  // After 5-second hold expires (6th second of error), reading drops to 0%
  run_n_intervals(5000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT32(0UL, flexCounter);
}

static void test_flex_off_by_one_correction(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 51U;                    // tempEthPct = 1 -> off-by-one corrected to 0
  flexPulseWidth = 1000UL;
  currentStatus.ethanolPct = 75U;

  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
}

static void test_flex_clamps_pulsewidth_low(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  flexCounter = 100U;                   // 50% ethanol
  flexPulseWidth = 0UL;                  // Below 1000us -> clamp to 1000

  run_n_intervals(1000);
  // With pw clamped at 1000us: tempX100 = (4224*1000)/1024 - 8125 = 4125 - 8125 = -4000
  // fuelTemp = -4000/100 = -40C
  TEST_ASSERT_EQUAL_INT16(-40, currentStatus.fuelTemp);
}

static void test_flex_hold_exact_5_second_duration(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  currentStatus.ethanolPct = 85U; // Baseline 85% ethanol

  // Disconnect sensor / 0 Hz error: verify each second individually
  for (uint8_t sec = 1U; sec <= 5U; ++sec)
  {
    flexCounter = 0U;
    run_n_intervals(1000);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(85U, currentStatus.ethanolPct, "Reading must hold at 85% during 5-second window");
    TEST_ASSERT_EQUAL_UINT8(sec, flexErrorSeconds);
  }

  // 6th consecutive bad second: hold must expire and reading drops to 0%
  flexCounter = 0U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(6U, flexErrorSeconds);

  // 7th consecutive bad second: stays 0%, error counter does not overflow
  flexCounter = 0U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(6U, flexErrorSeconds);
}

static void test_flex_hold_recovery_clears_fault_counter(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  currentStatus.ethanolPct = 70U;

  // Transient crosstalk burst (190Hz) for 3 seconds
  for (uint8_t i = 0U; i < 3U; ++i)
  {
    flexCounter = 190U;
    run_n_intervals(1000);
    TEST_ASSERT_EQUAL_UINT8(70U, currentStatus.ethanolPct);
  }
  TEST_ASSERT_EQUAL_UINT8(3U, flexErrorSeconds);

  // Valid signal (70% ethanol = 120Hz) returns at 4th second
  flexCounter = 120U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(70U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(0U, flexErrorSeconds); // Counter must be reset to 0

  // A new 2-second error occurs: timer starts fresh from 0
  flexCounter = 190U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(70U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(1U, flexErrorSeconds);

  flexCounter = 190U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(70U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(2U, flexErrorSeconds);
}

static void test_flex_hold_recovery_to_different_valid_value(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;

  // Start with 50% ethanol (100Hz)
  flexCounter = 100U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(50U, currentStatus.ethanolPct);

  // Glitch for 2 seconds: held at 50%
  flexCounter = 0U;
  run_n_intervals(2000);
  TEST_ASSERT_EQUAL_UINT8(50U, currentStatus.ethanolPct);

  // New fuel content read: 80% ethanol (130Hz)
  flexCounter = 130U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(80U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(0U, flexErrorSeconds);
}

static void test_flex_hold_maintains_value_under_smoothing_filter(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 128U; // 50% filter
  currentStatus.ethanolPct = 60U;

  // Transient glitch (0Hz) for 4 seconds
  for (uint8_t i = 0U; i < 4U; ++i)
  {
    flexCounter = 0U;
    run_n_intervals(1000);
    // Because the hold provides the current reading to the filter, it must not decay
    TEST_ASSERT_EQUAL_UINT8(60U, currentStatus.ethanolPct);
  }
}

static void test_flex_hold_initial_startup_no_sensor(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  currentStatus.ethanolPct = 0U; // Startup default

  // With no pulses at all across 7 seconds, reading must remain 0% throughout
  for (uint8_t i = 0U; i < 7U; ++i)
  {
    flexCounter = 0U;
    run_n_intervals(1000);
    TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);
  }
}

static void test_flex_hold_full_timeout_then_reconnection(void)
{
  setup_oneMsInterval();
  configPage2.flexEnabled = true;
  configPage2.flexFreqLow = 50U;
  configPage2.flexFreqHigh = 150U;
  configPage4.FILTER_FLEX = 0U;
  currentStatus.ethanolPct = 60U;

  // Error persists for 6 seconds -> drops to 0%
  flexCounter = 0U;
  run_n_intervals(6000);
  TEST_ASSERT_EQUAL_UINT8(0U, currentStatus.ethanolPct);

  // Sensor is reconnected: 60% ethanol (110Hz)
  flexCounter = 110U;
  run_n_intervals(1000);
  TEST_ASSERT_EQUAL_UINT8(60U, currentStatus.ethanolPct);
  TEST_ASSERT_EQUAL_UINT8(0U, flexErrorSeconds);
}

void testFlex(void)
{
  SET_UNITY_FILENAME()
  {
    RUN_TEST(test_flex_low_freq_yields_zero_pct);
    RUN_TEST(test_flex_in_band_yields_pct);
    RUN_TEST(test_flex_over_range_error_yields_zero_pct);
    RUN_TEST(test_flex_saturation_band_yields_100_pct);
    RUN_TEST(test_flex_1_pulse_buffer_yields_100_pct);
    RUN_TEST(test_flex_error_at_170_yields_zero_pct);
    RUN_TEST(test_flex_off_by_one_correction);
    RUN_TEST(test_flex_clamps_pulsewidth_low);
    RUN_TEST(test_flex_hold_exact_5_second_duration);
    RUN_TEST(test_flex_hold_recovery_clears_fault_counter);
    RUN_TEST(test_flex_hold_recovery_to_different_valid_value);
    RUN_TEST(test_flex_hold_maintains_value_under_smoothing_filter);
    RUN_TEST(test_flex_hold_initial_startup_no_sensor);
    RUN_TEST(test_flex_hold_full_timeout_then_reconnection);
  }
}
