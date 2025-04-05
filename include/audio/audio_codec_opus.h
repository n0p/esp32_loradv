#ifndef AUDIO_CODEC_OPUS_H
#define AUDIO_CODEC_OPUS_H

#include <opus.h>
#include "audio/audio_codec.h"

namespace LoraDv {

class AudioCodecOpus : public AudioCodec {

public:
  AudioCodecOpus();

  virtual bool start(std::shared_ptr<const Config> config) override;
  virtual void stop() override;

  virtual int encode(uint8_t *encodedOut, int16_t *pcmIn) override;
  virtual int decode(int16_t *pcmOut, uint8_t *encodedIn, uint16_t encodedSize) override;

  virtual bool isFixedFrameSize() const override { return false; }

  virtual int getFrameSize() const override { return encodedFrameBufferSize_; }
  virtual int getPcmFrameSize() const override { return pcmFrameSize_; };
  virtual int getPcmFrameBufferSize() const override { return pcmFrameBufferSize_; };

private:
  const int CfgComplexity = 0;
  const int CfgEncodedFrameBufferSize = 1024;

  OpusEncoder *opusEncoder_;
  OpusDecoder *opusDecoder_;

  int pcmFrameSize_;
  int pcmFrameBufferSize_;
  int encodedFrameBufferSize_;
};

}

#endif // AUDIO_CODEC_OPUS_H