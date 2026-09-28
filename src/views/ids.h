// Числовые идентификаторы форм и контролов модуля.
//
// Формат требует именно чисел: SWELL сопоставляет контролы диалога с кодом по
// ним, а не по именам. Файл один на все формы: нумерация диалогов в SWELL
// сквозная на модуль, и столкновение здесь видно глазами.

#pragma once

// Именно макросы, а не enum: этот заголовок включается в forms.rc, который
// читает не компилятор C++, а генератор таблицы диалога.
// NOLINTBEGIN(modernize-macro-to-enum,cppcoreguidelines-macro-usage)

// --- Настройки формата «видео с камеры» ---------------------------------------

#define IDD_FORMAT_CONFIG 100

#define IDC_FORMAT_CAMERA_LABEL 1000
#define IDC_FORMAT_CAMERA 1001
#define IDC_FORMAT_MODE_LABEL 1002
#define IDC_FORMAT_MODE 1003
#define IDC_FORMAT_STATUS 1004

// NOLINTEND(modernize-macro-to-enum,cppcoreguidelines-macro-usage)
