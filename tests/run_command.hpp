#pragma once

#include <optional>
#include <string>

namespace cam::tests {

/// Выполняет команду оболочки и возвращает её вывод. Пусто, если команда
/// завершилась с ошибкой или её нет в системе.
std::optional<std::string> runCommand(const std::string &command);

/// Есть ли в системе ffprobe. Проверки через него необязательны: без него
/// они пропускаются с сообщением.
bool hasFfprobe();

} // namespace cam::tests
