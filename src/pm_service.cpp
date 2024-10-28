#include "pm_service.h"

namespace LoraDv {

  std::shared_ptr<const Config> config_;
  std::shared_ptr<Adafruit_SSD1306> display_;

  Timer<1> lightSleepTimer_;
  Timer<1>::Task lightSleepTimerTask_;

  bool isExitFromSleep_;

PmService::PmService() 
  : config_(nullptr)
  , display_(nullptr)
  , lightSleepTimerTask_(0)
  , isExitFromSleep_(false)
{
}   

void PmService::setup(std::shared_ptr<const Config> config, std::shared_ptr<Adafruit_SSD1306> display)
{
  config_ = config;
  display_ = display;
  lightSleepReset();
}

void PmService::lightSleepReset() 
{
  LOG_DEBUG("Reset light sleep");
  if (lightSleepTimerTask_ != 0) {
    lightSleepTimer_.cancel(lightSleepTimerTask_);
  }
  lightSleepTimerTask_ = lightSleepTimer_.in(config_->PmSleepAfterMs, lightSleepEnterTimer, this);
}

bool PmService::lightSleepEnterTimer(void *param) 
{
  static_cast<PmService*>(param)->lightSleepEnter();
  return false;
}

void PmService::lightSleepEnter(void) 
{
  LOG_INFO("Entering light sleep");
  display_->clearDisplay();
  display_->display();

  esp_sleep_wakeup_cause_t wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  while (true) {
    wakeupCause = lightSleepWait(config_->PmLightSleepDurationMs_ * 1000UL);
    if (wakeupCause != ESP_SLEEP_WAKEUP_TIMER) break;
    delay(config_->PmLightSleepAwakeMs_);
  }

  LOG_INFO("Exiting light sleep");
  isExitFromSleep_ = true;
}

esp_sleep_wakeup_cause_t PmService::lightSleepWait(uint64_t sleepTimeUs) const
{
  esp_sleep_enable_ext0_wakeup((gpio_num_t)config_->PttBtnPin_, LOW);
#ifdef USE_SX126X
  uint64_t bitMask = (uint64_t)(1 << config_->LoraPinA_) | (uint64_t)(1 << config_->LoraPinB_);
#else
  uint64_t bitMask = (uint64_t)(1 << config_->LoraPinA_);
#endif
  esp_sleep_enable_ext1_wakeup(bitMask, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(sleepTimeUs);
  esp_light_sleep_start();
  return esp_sleep_get_wakeup_cause();
}

bool PmService::loop()
{
  lightSleepTimer_.tick();
  bool isExitFromSleep = isExitFromSleep_;
  isExitFromSleep_ = false;
  if (isExitFromSleep)
    lightSleepReset();
  return isExitFromSleep;
}

} // LoraDv