#include "sink.hpp"

#include "journal.hpp"
#include "services.hpp"

#include <cstdio>
#include <utility>

namespace cam::reaper {

Sink::Sink(std::string fileName, int channels, int sampleRate, std::unique_ptr<Take> take)
    : fileName_(std::move(fileName)), channels_(channels), sampleRate_(sampleRate),
      take_(std::move(take)) {
  journal("sink create file={} nch={} srate={}", fileName_, channels_, sampleRate_);
}

Sink::~Sink() {
  const std::int64_t samples = take_->samples();
  take_.reset(); // дописывает места до конца записанного и закрывает файл
  journal("sink destroy file={} samples={} hook drops={}", fileName_, samples,
          services().clock().dropped());
}

void Sink::GetOutputInfoString(char *buf, int buflen) {
  if (buf && buflen > 0)
    (void)std::snprintf(buf, static_cast<size_t>(buflen), "Video (camera)");
}

const char *Sink::GetFileName() { return fileName_.c_str(); }

int Sink::GetNumChannels() { return channels_; }

double Sink::GetLength() {
  return sampleRate_ > 0 ? static_cast<double>(take_->samples()) / sampleRate_ : 0.0;
}

INT64 Sink::GetFileSize() { return 0; }

void Sink::WriteMIDI(MIDI_eventlist * /*events*/, int /*len*/, double /*samplerate*/) {}

void Sink::WriteDoubles(ReaSample ** /*samples*/, int len, int /*nch*/, int /*offset*/,
                        int /*spacing*/) {
  // Как если бы из звукового потока: только числа в кольцо (design.md D8).
  take_->onBlock({.firstSample = nextSample_, .length = len, .position = blockPosition_});
  nextSample_ += len;
}

void Sink::GetPeakInfo(PCM_source_peaktransfer_t *block) {
  if (block)
    block->peaks_out = 0;
}

int Sink::Extended(int call, void *parm1, void * /*parm2*/, void * /*parm3*/) {
  if (call == PCM_SINK_EXT_SETCURBLOCKTIME && parm1)
    blockPosition_ = *static_cast<double *>(parm1);

  return 0;
}

} // namespace cam::reaper
