#include "hal/radio_task.h"

namespace LoraDv {

volatile bool RadioTask::isIsrEnabled_ = true;
TaskHandle_t RadioTask::loraTaskHandle_;

RadioTask::RadioTask(std::shared_ptr<const Config> config)
  : config_(config)
  , radioModule_(nullptr)
  , audioTask_(nullptr)
  , cipher_(new ChaCha())
  , isImplicitMode_(false)
  , isIsrInstalled_(false)
  , isRunning_(false)
  , shouldUpdateScreen_(false)
  , lastRssi_(0)
{
}

void RadioTask::start(std::shared_ptr<AudioTask> audioTask)
{
  audioTask_ = audioTask;
  cipher_->setKey(config_->AudioPrivacyKey_, sizeof(config_->AudioPrivacyKey_));
  xTaskCreatePinnedToCore(&task, "RadioTask", CfgRadioTaskStack, this, CfgTaskPriority, &loraTaskHandle_, CfgCoreId);
}

void RadioTask::setupRig(long loraFreq, long bw, int sf, int cr, int pwr, int sync, int crcBytes)
{
  LOG_INFO("Initializing LoRa");
  LOG_INFO("Frequency:", loraFreq, "Hz");
  LOG_INFO("Bandwidth:", bw, "Hz");
  LOG_INFO("Spreading:", sf);
  LOG_INFO("Coding rate:", cr);
  LOG_INFO("Power:", pwr, "dBm");
  LOG_INFO("Sync:", "0x" + String(sync, HEX));
  LOG_INFO("CRC:", crcBytes);
  LOG_INFO("Speed:", Utils::loraGetSpeed(sf, cr, bw), "bps");
  LOG_INFO("Min level:", Utils::loraGetSnrLimit(sf, bw));
  radioModule_ = std::make_shared<MODULE_NAME>(new Module(config_->LoraPinSs_, config_->LoraPinA_, config_->LoraPinRst_, config_->LoraPinB_));
  int state = radioModule_->begin((float)loraFreq / 1e6, (float)bw / 1e3, sf, cr, sync, pwr);
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Radio start error:", state);
  }
  radioModule_->setCRC(crcBytes);
  radioModule_->setPreambleLength(config_->LoraPreambleLen_);
#ifdef USE_SX126X
    #pragma message("Using SX126X")
    LOG_INFO("Using SX126X module");
    radioModule_->setRfSwitchPins(config_->LoraPinSwitchRx_, config_->LoraPinSwitchTx_);
    if (isIsrInstalled_) radioModule_->clearDio1Action();
    radioModule_->setDio1Action(onRigIsrRxPacket);
    isIsrInstalled_ = true;
#else
    #pragma message("Using SX127X")
    LOG_INFO("Using SX127X module");
    if (isIsrInstalled_) radioModule_->clearDio0Action();
    radioModule_->setDio0Action(onRigIsrRxPacket, RISING);
    isIsrInstalled_ = true;
#endif
  radioModule_->explicitHeader();
  LOG_INFO("LoRa initialized");
}

void RadioTask::setupRigFsk(long freq, float bitRate, float freqDev, float rxBw, int pwr, byte shaping)
{
  LOG_INFO("Initializing FSK");
  LOG_INFO("Frequency:", freq, "Hz");
  LOG_INFO("Bit rate:", bitRate, "kbps");
  LOG_INFO("Deviation:", freqDev, "kHz");
  LOG_INFO("Bandwidth:", rxBw, "kHz");
  LOG_INFO("Power:", pwr, "dBm");
  LOG_INFO("Shaping:", shaping);
  radioModule_ = make_shared<MODULE_NAME>(new Module(config_->LoraPinSs_, config_->LoraPinA_, config_->LoraPinRst_, config_->LoraPinB_));
  int state = radioModule_->beginFSK((float)freq / 1e6, bitRate, freqDev, rxBw, pwr);
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Radio start error:", state);
  }
  radioModule_->disableAddressFiltering();
  radioModule_->setDataShaping(shaping);
#ifdef USE_SX126X
    #pragma message("Using SX126X")
    LOG_INFO("Using SX126X module");
    radioModule_->setRfSwitchPins(config_->LoraPinSwitchRx_, config_->LoraPinSwitchTx_);
    if (isIsrInstalled_) radioModule_->clearDio1Action();
    radioModule_->setDio1Action(onRigIsrRxPacket);
    isIsrInstalled_ = true;
#else
    #pragma message("Using SX127X")
    LOG_INFO("Using SX127X module");
    if (isIsrInstalled_) radioModule_->clearDio0Action();
    radioModule_->setDio0Action(onRigIsrRxPacket, RISING);
    isIsrInstalled_ = true;
#endif
  LOG_INFO("FSK initialized");
}

void RadioTask::setFreq(long loraFreq) const 
{
  radioModule_->setFrequency((float)loraFreq / (float)1e6);
}

bool RadioTask::hasData() const 
{
  return radioRxQueue_.index.size() > 0;
}

bool RadioTask::readPacketSize(byte &packetSize)
{
  if (!hasData()) return false;
  packetSize = radioRxQueue_.index.shift();
  return true;
}

bool RadioTask::readNextByte(byte &b)
{
  if (radioRxQueue_.data.size() == 0) return false;
  b = radioRxQueue_.data.shift();
  return true;
}

bool RadioTask::writePacketSize(byte packetSize)
{
  return radioTxQueue_.index.push(packetSize);
}

bool RadioTask::writeNextByte(byte b) 
{
  return radioTxQueue_.data.push(b);
}

IRAM_ATTR void RadioTask::onRigIsrRxPacket() 
{
  if (!isIsrEnabled_) return;
  BaseType_t xHigherPriorityTaskWoken;
  xTaskNotifyFromISR(loraTaskHandle_, CfgRadioRxBit, eSetBits, &xHigherPriorityTaskWoken);
}

void RadioTask::task(void *param)
{
  reinterpret_cast<RadioTask*>(param)->rigTask();
}

void RadioTask::startTransmit() const
{
  xTaskNotify(loraTaskHandle_, CfgRadioTxStartBit, eSetBits);
}

void RadioTask::startReceive() const
{
  xTaskNotify(loraTaskHandle_, CfgRadioRxStartBit, eSetBits);
}

void RadioTask::transmit() const
{
  xTaskNotify(loraTaskHandle_, CfgRadioTxBit, eSetBits);
}

void RadioTask::rigTask() 
{
  LOG_INFO("Radio task started");
  isRunning_ = true;

  if (config_->ModType == CFG_MOD_TYPE_LORA) {
    setupRig(config_->LoraFreqRx, config_->LoraBw, config_->LoraSf, 
      config_->LoraCodingRate, config_->LoraPower, config_->LoraSync_, config_->LoraCrc_);
  } else {
    setupRigFsk(config_->LoraFreqRx, config_->FskBitRate, config_->FskFreqDev,
      config_->FskRxBw, config_->LoraPower, config_->FskShaping);
  }

  int32_t seed = radioModule_->random(0x7FFFFFFF);
  LOG_INFO("Random seed:", String(seed, HEX));
  randomSeed(seed);

  rigTaskStartReceive();

  byte *packetBuf = new byte[CfgRadioPacketBufLen];
  byte *tmpBuf = new byte[CfgRadioPacketBufLen + sizeof(iv_)];

  while (isRunning_) {
    uint32_t cmdBits = 0;
    xTaskNotifyWaitIndexed(0, 0x00, ULONG_MAX, &cmdBits, portMAX_DELAY);

    LOG_DEBUG("Radio task bits", cmdBits);
    if (cmdBits & CfgRadioRxBit) {
      rigTaskReceive(packetBuf, tmpBuf);
    }
    else if (cmdBits & CfgRadioTxBit) {
      rigTaskTransmit(packetBuf, tmpBuf);
    } 
    if (cmdBits & CfgRadioRxStartBit) {
      rigTaskStartReceive();
    }
    else if (cmdBits & CfgRadioTxStartBit) {
      rigTaskStartTransmit();
    }
  } 

  delete tmpBuf;
  delete packetBuf;
  LOG_INFO("Radio task stopped");
  vTaskDelete(NULL);
}

bool RadioTask::loop() 
{
  bool shouldUpdateScreen = shouldUpdateScreen_;
  shouldUpdateScreen_ = false;
  return shouldUpdateScreen;
}

void RadioTask::rigTaskStartReceive() 
{
  LOG_INFO("Start receive");
  if (isHalfDuplex()) setFreq(config_->LoraFreqRx);
  int loraRadioState = radioModule_->startReceive();
  if (loraRadioState != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Start receive error: ", loraRadioState);
  }
  vTaskDelay(1);
  isIsrEnabled_ = true;
}

void RadioTask::rigTaskStartTransmit() 
{
  LOG_INFO("Start transmit");
  isIsrEnabled_ = false;
  if (isHalfDuplex()) setFreq(config_->LoraFreqTx);
}

void RadioTask::rigTaskReceive(byte *packetBuf, byte *tmpBuf) 
{
  int packetSize = radioModule_->getPacketLength();
  if (packetSize > 8 && packetSize <= CfgRadioPacketBufLen) {
    // receive packet
    int state = radioModule_->readData(packetBuf, packetSize);
    if (state == RADIOLIB_ERR_NONE) {
      byte *receiveBuf = packetBuf;
      // if privacy enabled
      if (config_->AudioEnPriv){
        // read iv and decrypt packet
        cipher_->setIV(packetBuf, sizeof(iv_));
        packetSize -= sizeof(iv_);
        cipher_->decrypt(tmpBuf, packetBuf + sizeof(iv_), packetSize);
        receiveBuf = tmpBuf;
      }
      // send packet to the queue
      LOG_DEBUG("Received packet, size", packetSize);
      for (int i = 0; i < packetSize; i++) {
        radioRxQueue_.data.push(receiveBuf[i]);
      }
      radioRxQueue_.index.push(packetSize);
      audioTask_->play();
    } else {
      LOG_ERROR("Read data error: ", state);
    }
    lastRssi_ = radioModule_->getRSSI();
    // probably not needed, still in receive
    state = radioModule_->startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      LOG_ERROR("Start receive error: ", state);
    }
  } else {
    LOG_ERROR("Wrong packet size: ", packetSize);
  }
}

void RadioTask::rigTaskTransmit(byte *packetBuf, byte *tmpBuf) 
{
  while (radioTxQueue_.index.size() > 0) {
    // fetch packet size and packet from the queue
    int txBytesCnt = radioTxQueue_.index.shift();
    if (txBytesCnt > CfgRadioPacketBufLen) {
      LOG_ERROR("Packet size is too large, not transmitting");
      for (int i = 0; i < txBytesCnt; i++) {
        radioTxQueue_.data.shift();
      }
      vTaskDelay(1);
      continue;
    }
    for (int i = 0; i < txBytesCnt; i++) {
        packetBuf[i] = radioTxQueue_.data.shift();
    }
    byte *sendBuf = packetBuf;
    // if privacy enabled
    if (config_->AudioEnPriv) {
      generateIv(tmpBuf);
      // encrypt
      cipher_->setIV(iv_, sizeof(iv_));
      cipher_->encrypt(tmpBuf + sizeof(iv_), packetBuf, txBytesCnt);
      txBytesCnt += sizeof(iv_);
      sendBuf = tmpBuf;
    }
    // transmit
    int loraRadioState = radioModule_->transmit(sendBuf, txBytesCnt);
    if (loraRadioState != RADIOLIB_ERR_NONE) {
        LOG_ERROR("Radio transmit failed:", loraRadioState, txBytesCnt);
    } else {
      LOG_DEBUG("Transmitted packet", txBytesCnt);
    }
    vTaskDelay(1);
  }
}

void RadioTask::generateIv(byte *tmpBuf) 
{
  for (int i = 0; i < sizeof(iv_); i++) {
    iv_[i] = random(255);
    tmpBuf[i] = iv_[i];
  }
}

} // LoraDv