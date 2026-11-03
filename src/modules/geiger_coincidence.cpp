#include "modules/geiger_coincidence.hpp"

#include "pins.hpp"

GeigerCoincidence geiger;

namespace {

void isrPulse1() { geiger.onPulse1(); }
void isrPulse2() { geiger.onPulse2(); }

uint32_t absDiff(uint32_t a, uint32_t b) {
  return (a > b) ? (a - b) : (b - a);
}

}  // namespace

void GeigerCoincidence::begin(uint32_t coincidence_window_us, uint32_t debounce_us) {
  coinc_window_us_ = coincidence_window_us;
  debounce_us_ = debounce_us;
  series_head_ = 0;
  series_count_ = 1;
  series_head_start_ms_ = millis();

  pinMode(pins::GEIGER_1, INPUT_PULLUP);
  pinMode(pins::GEIGER_2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(pins::GEIGER_1), isrPulse1, FALLING);
  attachInterrupt(digitalPinToInterrupt(pins::GEIGER_2), isrPulse2, FALLING);
}

void GeigerCoincidence::setCoincidenceWindowUs(uint32_t window_us) {
  noInterrupts();
  coinc_window_us_ = window_us;
  interrupts();
}

void GeigerCoincidence::setDebounceUs(uint32_t debounce_us) {
  noInterrupts();
  debounce_us_ = debounce_us;
  interrupts();
}

GeigerStats GeigerCoincidence::snapshot() const {
  GeigerStats stats;
  noInterrupts();
  const uint32_t now_us = micros();
  const_cast<GeigerCoincidence*>(this)->expirePending(now_us);
  stats.ch1_count = ch1_count_;
  stats.ch2_count = ch2_count_;
  stats.coinc_count = coinc_count_;
  stats.ch1_uncorr_count = ch1_uncorr_count_;
  stats.ch2_uncorr_count = ch2_uncorr_count_;
  stats.ch1_bounce_rejects = ch1_bounce_rejects_;
  stats.ch2_bounce_rejects = ch2_bounce_rejects_;
  stats.last_coinc_dt_us = last_coinc_dt_us_;
  stats.coinc_window_us = coinc_window_us_;
  stats.debounce_us = debounce_us_;
  interrupts();
  return stats;
}

GeigerSeriesSnapshot GeigerCoincidence::seriesSnapshot() const {
  GeigerSeriesSnapshot out;
  noInterrupts();
  const uint32_t now_us = micros();
  auto* self = const_cast<GeigerCoincidence*>(this);
  self->expirePending(now_us);

  out.bin_ms = kSeriesBinMs;
  out.count = series_count_;
  if (out.count > 0) {
    const uint8_t oldest_idx = static_cast<uint8_t>((series_head_ + kGeigerSeriesBins - (series_count_ - 1)) %
                                                    kGeigerSeriesBins);
    for (uint8_t i = 0; i < out.count; ++i) {
      const uint8_t idx = static_cast<uint8_t>((oldest_idx + i) % kGeigerSeriesBins);
      const uint32_t age_bins = static_cast<uint32_t>(out.count - 1 - i);
      out.points[i].t_ms = series_head_start_ms_ - age_bins * kSeriesBinMs;
      out.points[i].corr = series_corr_[idx];
      out.points[i].uncorr_ch1 = series_uncorr_ch1_[idx];
      out.points[i].uncorr_ch2 = series_uncorr_ch2_[idx];
    }
  }
  interrupts();
  return out;
}

void GeigerCoincidence::poll() {
  noInterrupts();
  expirePending(micros());
  interrupts();
}

void GeigerCoincidence::onPulse1() {
  const uint32_t now_us = micros();
  if (now_us - last_ch1_accepted_us_ < debounce_us_) {
    ch1_bounce_rejects_++;
    return;
  }
  last_ch1_accepted_us_ = now_us;
  ch1_count_++;
  expirePending(now_us);

  if (ch2_pending_) {
    const uint32_t dt = absDiff(now_us, last_ch2_us_);
    if (dt <= coinc_window_us_) {
      coinc_count_++;
      last_coinc_dt_us_ = dt;
      recordCorrelatedAtUs(now_us);
      ch2_pending_ = false;
      ch1_pending_ = false;
      return;
    }
  }
  if (ch1_pending_) {
    recordUncorrelatedCh1AtUs(last_ch1_us_);
    ch1_pending_ = false;
  }
  last_ch1_us_ = now_us;
  ch1_pending_ = true;
}

void GeigerCoincidence::onPulse2() {
  const uint32_t now_us = micros();
  if (now_us - last_ch2_accepted_us_ < debounce_us_) {
    ch2_bounce_rejects_++;
    return;
  }
  last_ch2_accepted_us_ = now_us;
  ch2_count_++;
  expirePending(now_us);

  if (ch1_pending_) {
    const uint32_t dt = absDiff(now_us, last_ch1_us_);
    if (dt <= coinc_window_us_) {
      coinc_count_++;
      last_coinc_dt_us_ = dt;
      recordCorrelatedAtUs(now_us);
      ch1_pending_ = false;
      ch2_pending_ = false;
      return;
    }
  }
  if (ch2_pending_) {
    recordUncorrelatedCh2AtUs(last_ch2_us_);
    ch2_pending_ = false;
  }
  last_ch2_us_ = now_us;
  ch2_pending_ = true;
}

void GeigerCoincidence::advanceSeriesToMs(uint32_t now_ms) {
  if (series_count_ == 0) {
    series_head_ = 0;
    series_count_ = 1;
    series_head_start_ms_ = now_ms;
    return;
  }

  while (now_ms - series_head_start_ms_ >= kSeriesBinMs) {
    series_head_start_ms_ += kSeriesBinMs;
    series_head_ = static_cast<uint8_t>((series_head_ + 1) % kGeigerSeriesBins);
    series_corr_[series_head_] = 0;
    series_uncorr_ch1_[series_head_] = 0;
    series_uncorr_ch2_[series_head_] = 0;
    if (series_count_ < kGeigerSeriesBins) {
      series_count_++;
    }
  }
}

void GeigerCoincidence::expirePending(uint32_t now_us) {
  if (ch1_pending_ && (now_us - last_ch1_us_ > coinc_window_us_)) {
    recordUncorrelatedCh1AtUs(last_ch1_us_);
    ch1_pending_ = false;
  }
  if (ch2_pending_ && (now_us - last_ch2_us_ > coinc_window_us_)) {
    recordUncorrelatedCh2AtUs(last_ch2_us_);
    ch2_pending_ = false;
  }
}

void GeigerCoincidence::recordCorrelatedAtUs(uint32_t event_us) {
  const uint32_t now_ms = event_us / 1000u;
  advanceSeriesToMs(now_ms);
  series_corr_[series_head_]++;
}

void GeigerCoincidence::recordUncorrelatedCh1AtUs(uint32_t event_us) {
  ch1_uncorr_count_++;
  const uint32_t now_ms = event_us / 1000u;
  advanceSeriesToMs(now_ms);
  series_uncorr_ch1_[series_head_]++;
}

void GeigerCoincidence::recordUncorrelatedCh2AtUs(uint32_t event_us) {
  ch2_uncorr_count_++;
  const uint32_t now_ms = event_us / 1000u;
  advanceSeriesToMs(now_ms);
  series_uncorr_ch2_[series_head_]++;
}
