#pragma once

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

#include "take.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace cam::reaper {

/// Экземпляр формата «видео с камеры»: REAPER создаёт его на каждую
/// дорожку-камеру при каждой записи и передаёт ему поток дорожки (design.md D1).
///
/// Звук в файл не пишется: сэмплы только считаются — это время файла
/// (design.md D4), — а позиция блока из SETCURBLOCKTIME связывает блок с
/// аудиохуком (design.md D5). Всё остальное делает дубль в своём потоке.
class Sink : public PCM_sink {
public:
  Sink(std::string fileName, int channels, int sampleRate, std::unique_ptr<Take> take);
  ~Sink() override;

  Sink(const Sink &) = delete;
  Sink &operator=(const Sink &) = delete;
  Sink(Sink &&) = delete;
  Sink &operator=(Sink &&) = delete;

  void GetOutputInfoString(char *buf, int buflen) override;
  const char *GetFileName() override;
  int GetNumChannels() override;
  double GetLength() override;
  INT64 GetFileSize() override;

  void WriteMIDI(MIDI_eventlist *events, int len, double samplerate) override;
  void WriteDoubles(ReaSample **samples, int len, int nch, int offset, int spacing) override;

  void GetPeakInfo(PCM_source_peaktransfer_t *block) override;

  int Extended(int call, void *parm1, void *parm2, void *parm3) override;

private:
  std::string fileName_;
  int channels_;
  int sampleRate_;
  std::unique_ptr<Take> take_;

  // WriteDoubles и SETCURBLOCKTIME перед ним хост зовёт по очереди, пусть и
  // из разных рабочих потоков, — счётчик и позиция ни с кем не делятся.
  std::int64_t nextSample_ = 0;
  double blockPosition_ = 0.0;
};

} // namespace cam::reaper
