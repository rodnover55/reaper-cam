#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace cam::container {

/// Есть ли в кадре JPEG таблицы Хаффмана (сегмент DHT) до начала данных
/// изображения.
bool hasHuffmanTables(std::span<const std::uint8_t> jpeg);

/// Кадр MJPEG, дополненный стандартными таблицами Хаффмана (приложение K
/// стандарта JPEG), если своих в нём нет (design.md D7).
///
/// Камеры часто опускают таблицы в кадрах MJPEG: по стандарту они подразумеваются.
/// Декодерам, которым нужен полный JPEG, их дописывают перед началом данных
/// изображения (SOS). Сжатые данные изображения не меняются ни на байт. Кадр с
/// таблицами и то, что не разбирается как JPEG, возвращается как есть.
std::vector<std::uint8_t> withHuffmanTables(std::span<const std::uint8_t> jpeg);

} // namespace cam::container
