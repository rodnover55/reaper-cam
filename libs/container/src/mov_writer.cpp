// Фрагментированный QuickTime с кодеком Photo-JPEG (design.md D7).
//
// Раскладка коробок — по QuickTime File Format и ISO/IEC 14496-12 (фрагменты).
// В начале файла — индекс без кадров, дальше фрагменты moof + mdat по
// секунде. После каждого целого фрагмента в индексе обновляются
// длительности: хост берёт длину файла из индекса и фрагменты не
// пересчитывает, так что файл, оборванный аварией, читается до последнего
// целого фрагмента и с верной длиной (findings.md, вопрос 6).
//
// Дорожка одна, частота постоянная: шкала времени дорожки — числитель
// частоты, длительность кадра — её знаменатель, так что время кадра точное и
// для 30000/1001.

#include "mov_writer.hpp"

#include "bytes.hpp"

#include <cmath>
#include <exception>
#include <utility>
#include <vector>

namespace cam::container::detail {
namespace {

constexpr std::uint32_t kMovieTimescale = 1000;

/// Кадров во фрагменте: около секунды. Короче — меньше теряется при аварии,
/// длиннее — меньше служебных коробок (design.md, Open Questions).
int framesPerFragment(const VideoFormat &format) {
  const int frames =
      (format.rateNumerator + format.rateDenominator - 1) / format.rateDenominator;
  return frames > 0 ? frames : 1;
}

void matrix(Bytes &b) {
  b.u32(0x00010000);
  b.u32(0);
  b.u32(0);
  b.u32(0);
  b.u32(0x00010000);
  b.u32(0);
  b.u32(0);
  b.u32(0);
  b.u32(0x40000000);
}

/// Строка с длиной в первом байте, как принято в QuickTime.
void pascalString(Bytes &b, std::string_view text) {
  b.u8(static_cast<std::uint8_t>(text.size()));
  b.text(text);
}

void fileType(Bytes &b) {
  const Box ftyp(b, "ftyp");
  b.fourcc("qt  ");
  b.u32(0x00000200);
  b.fourcc("qt  ");
}

/// Описание кадров: Photo-JPEG, по одному полю на кадр.
void jpegSampleEntry(Bytes &b, const VideoFormat &format) {
  const Box entry(b, "jpeg");
  b.zeros(6); // зарезервировано
  b.u16(1);   // ссылка на данные
  b.u16(0);   // версия
  b.u16(0);   // ревизия
  b.u32(0);   // производитель
  b.u32(0);   // временное качество
  b.u32(512); // пространственное качество: codecNormalQuality
  b.u16(static_cast<std::uint16_t>(format.width));
  b.u16(static_cast<std::uint16_t>(format.height));
  b.u32(0x00480000); // 72 точки на дюйм
  b.u32(0x00480000);
  b.u32(0); // размер данных
  b.u16(1); // кадров в образце

  // Имя компрессора — ровно 32 байта: длина, буквы, нули.
  const std::string_view name = "Photo - JPEG";
  pascalString(b, name);
  b.zeros(31 - name.size());

  b.u16(24);     // глубина цвета
  b.u16(0xFFFF); // своей палитры нет
}

/// Пустая таблица образцов: кадры описывают фрагменты.
void emptyTable(Bytes &b, std::string_view type, int zeroFields) {
  const Box box(b, type);
  b.u32(0); // версия и флаги
  for (int i = 0; i < zeroFields; ++i)
    b.u32(0);
}

} // namespace

MovWriter::MovWriter(std::unique_ptr<Output> output, const VideoFormat &format)
    : output_(std::move(output)), format_(format),
      framesPerFragment_(framesPerFragment(format)) {
  Bytes header;
  fileType(header);

  moovAt_ = header.size();
  movie(header);

  output_->append(header.data());
  output_->flush();
}

MovWriter::~MovWriter() {
  // Закрытие при уничтожении экземпляра формата: ошибке здесь некуда уйти, а
  // целые фрагменты на диске уже читаются и без хвоста.
  try {
    close();
  } catch (const std::exception &) { // NOLINT(bugprone-empty-catch)
  }
}

void MovWriter::append(std::span<const std::uint8_t> jpeg) {
  if (finished_)
    return;

  pending_.emplace_back(jpeg.begin(), jpeg.end());
  ++frames_;

  if (static_cast<int>(pending_.size()) >= framesPerFragment_)
    writeFragment();
}

void MovWriter::finish() { close(); }

void MovWriter::close() {
  if (finished_)
    return;

  finished_ = true;
  writeFragment();
  output_->flush();
}

std::uint32_t MovWriter::movieDurationOf(std::uint64_t frames) const {
  const double seconds =
      static_cast<double>(frames) * format_.rateDenominator / format_.rateNumerator;
  return static_cast<std::uint32_t>(std::llround(seconds * kMovieTimescale));
}

void MovWriter::movie(Bytes &b) {
  const std::size_t start = b.size();

  const Box moov(b, "moov");
  {
    const Box mvhd(b, "mvhd");
    b.u32(0); // версия и флаги
    b.u32(0); // создан
    b.u32(0); // изменён
    b.u32(kMovieTimescale);
    fields_.mvhd = b.size() - start;
    b.u32(0);          // длительность — после каждого фрагмента
    b.u32(0x00010000); // скорость 1.0
    b.u16(0x0100);     // громкость 1.0
    b.zeros(10);
    matrix(b);
    b.zeros(24); // предпросмотр, постер, выделение, текущее время
    b.u32(2);    // следующий номер дорожки
  }
  {
    const Box trak(b, "trak");
    {
      const Box tkhd(b, "tkhd");
      b.u32(0x00000003); // дорожка включена и участвует в фильме
      b.u32(0);
      b.u32(0);
      b.u32(1); // номер дорожки
      b.u32(0);
      fields_.tkhd = b.size() - start;
      b.u32(0);
      b.zeros(8);
      b.u16(0); // слой
      b.u16(0); // группа
      b.u16(0); // громкость: у видео нет
      b.u16(0);
      matrix(b);
      b.u32(static_cast<std::uint32_t>(format_.width) << 16U);
      b.u32(static_cast<std::uint32_t>(format_.height) << 16U);
    }
    {
      const Box mdia(b, "mdia");
      {
        const Box mdhd(b, "mdhd");
        b.u32(0);
        b.u32(0);
        b.u32(0);
        b.u32(static_cast<std::uint32_t>(format_.rateNumerator));
        fields_.mdhd = b.size() - start;
        b.u32(0);
        b.u16(0x55C4); // язык «und»
        b.u16(0);
      }
      {
        const Box hdlr(b, "hdlr");
        b.u32(0);
        b.fourcc("mhlr");
        b.fourcc("vide");
        b.u32(0);
        b.u32(0);
        b.u32(0);
        pascalString(b, "VideoHandler");
      }
      {
        const Box minf(b, "minf");
        {
          const Box vmhd(b, "vmhd");
          b.u32(0x00000001);
          b.u16(0x0040); // ditherCopy
          b.u16(0x8000);
          b.u16(0x8000);
          b.u16(0x8000);
        }
        {
          const Box hdlr(b, "hdlr");
          b.u32(0);
          b.fourcc("dhlr");
          b.fourcc("alis");
          b.u32(0);
          b.u32(0);
          b.u32(0);
          pascalString(b, "DataHandler");
        }
        {
          const Box dinf(b, "dinf");
          const Box dref(b, "dref");
          b.u32(0);
          b.u32(1);
          const Box alis(b, "alis");
          b.u32(0x00000001); // данные в этом же файле
        }
        {
          const Box stbl(b, "stbl");
          {
            const Box stsd(b, "stsd");
            b.u32(0);
            b.u32(1);
            jpegSampleEntry(b, format_);
          }
          emptyTable(b, "stts", 1);
          emptyTable(b, "stsc", 1);
          emptyTable(b, "stsz", 2);
          emptyTable(b, "stco", 1);
        }
      }
    }
  }
  {
    const Box mvex(b, "mvex");
    {
      const Box mehd(b, "mehd");
      b.u32(0x01000000); // версия 1: длительность 64 бита
      fields_.mehd = b.size() - start;
      b.u64(0);
    }
    {
      const Box trex(b, "trex");
      b.u32(0);
      b.u32(1); // дорожка
      b.u32(1); // описание кадров
      b.u32(static_cast<std::uint32_t>(format_.rateDenominator));
      b.u32(0); // размер — у каждого кадра свой
      b.u32(0); // флаги: каждый кадр опорный
    }
  }
}

void MovWriter::patchDurations(std::uint64_t frames) {
  const std::uint32_t movieDuration = movieDurationOf(frames);
  const auto media =
      static_cast<std::uint32_t>(frames * static_cast<std::uint64_t>(format_.rateDenominator));
  const auto patch32 = [this](std::size_t at, std::uint32_t value) {
    Bytes b;
    b.u32(value);
    output_->patch(moovAt_ + at, b.data());
  };

  patch32(fields_.mvhd, movieDuration);
  patch32(fields_.tkhd, movieDuration);
  patch32(fields_.mdhd, media);
  output_->patch(moovAt_ + fields_.mehd, bigEndian64(movieDuration));
}

void MovWriter::writeFragment() {
  if (pending_.empty())
    return;

  Bytes b;
  std::size_t dataOffsetAt = 0;
  {
    const Box moof(b, "moof");
    {
      const Box mfhd(b, "mfhd");
      b.u32(0);
      b.u32(++sequence_);
    }
    {
      const Box traf(b, "traf");
      {
        const Box tfhd(b, "tfhd");
        b.u32(0x00020000); // смещения считаются от начала moof
        b.u32(1);
      }
      {
        const Box tfdt(b, "tfdt");
        b.u32(0x01000000);
        b.u64(fragmentFirstFrame_ * static_cast<std::uint64_t>(format_.rateDenominator));
      }
      {
        const Box trun(b, "trun");
        b.u32(0x00000201); // есть смещение данных и размер каждого кадра
        b.u32(static_cast<std::uint32_t>(pending_.size()));
        dataOffsetAt = b.size();
        b.u32(0);
        for (const auto &frame : pending_)
          b.u32(static_cast<std::uint32_t>(frame.size()));
      }
    }
  }

  std::uint64_t payload = 0;
  for (const auto &frame : pending_)
    payload += frame.size();

  // Данные начинаются сразу за восьмибайтным заголовком mdat.
  b.putU32At(dataOffsetAt, static_cast<std::uint32_t>(b.size() + 8));
  b.u32(static_cast<std::uint32_t>(payload + 8));
  b.fourcc("mdat");

  output_->append(b.data());
  for (const auto &frame : pending_)
    output_->append(frame);

  // Фрагмент целиком на диске — после аварии он читается.
  output_->flush();

  fragmentFirstFrame_ += pending_.size();
  pending_.clear();

  // Длительности в индексе — по последний целый фрагмент. Без них хост
  // открывает оборванный файл как видео нулевой длины: сам он фрагменты не
  // пересчитывает (findings.md, вопрос 6).
  patchDurations(fragmentFirstFrame_);
  output_->flush();
}

} // namespace cam::container::detail
