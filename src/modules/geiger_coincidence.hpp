#pragma once

#include <Arduino.h>

struct GeigerStats {
  uint32_t ch1_count = 0;
  uint32_t ch2_count = 0;
  uint32_t coinc_count = 0;
  uint32_t ch1_uncorr_count = 0;
  uint32_t ch2_uncorr_count = 0;
  uint32_t ch1_bounce_rejects = 0;
  uint32_t ch2_bounce_rejects = 0;
  uint32_t last_coinc_dt_us = 0;
  uint32_t coinc_window_us = 0;
  uint32_t debounce_us = 0;
};

constexpr size_t kGeigerSeriesBins = 120;

struct GeigerSeriesPoint {
  uint32_t t_ms = 0;
  uint32_t corr = 0;
  uint32_t uncorr_ch1 = 0;
  uint32_t uncorr_ch2 = 0;
};

struct GeigerSeriesSnapshot {
  uint32_t bin_ms = 0;
  uint8_t count = 0;
  GeigerSeriesPoint points[kGeigerSeriesBins];
};

class GeigerCoincidence {
 public:
  void begin(uint32_t coincidence_window_us, uint32_t debounce_us);
  void setCoincidenceWindowUs(uint32_t window_us);
  void setDebounceUs(uint32_t debounce_us);
  GeigerStats snapshot() const;
  GeigerSeriesSnapshot seriesSnapshot() const;
  void poll();

  void onPulse1();
  void onPulse2();

 private:
  static constexpr uint32_t kSeriesBinMs = 1000;
  void advanceSeriesToMs(uint32_t now_ms);
  void expirePending(uint32_t now_us);
  void recordCorrelatedAtUs(uint32_t event_us);
  void recordUncorrelatedCh1AtUs(uint32_t event_us);
  void recordUncorrelatedCh2AtUs(uint32_t event_us);

  volatile uint32_t ch1_count_ = 0;
  volatile uint32_t ch2_count_ = 0;
  volatile uint32_t coinc_count_ = 0;
  volatile uint32_t ch1_uncorr_count_ = 0;
  volatile uint32_t ch2_uncorr_count_ = 0;
  volatile uint32_t ch1_bounce_rejects_ = 0;
  volatile uint32_t ch2_bounce_rejects_ = 0;
  volatile uint32_t last_ch1_accepted_us_ = 0;
  volatile uint32_t last_ch2_accepted_us_ = 0;
  volatile bool ch1_pending_ = false;
  volatile bool ch2_pending_ = false;
  volatile uint32_t last_ch1_us_ = 0;
  volatile uint32_t last_ch2_us_ = 0;
  volatile uint32_t last_coinc_dt_us_ = 0;
  volatile uint32_t coinc_window_us_ = 0;
  volatile uint32_t debounce_us_ = 0;

  volatile uint8_t series_head_ = 0;
  volatile uint8_t series_count_ = 0;
  volatile uint32_t series_head_start_ms_ = 0;
  volatile uint32_t series_corr_[kGeigerSeriesBins] = {};
  volatile uint32_t series_uncorr_ch1_[kGeigerSeriesBins] = {};
  volatile uint32_t series_uncorr_ch2_[kGeigerSeriesBins] = {};
};

extern GeigerCoincidence geiger;
