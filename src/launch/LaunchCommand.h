#pragma once

#include <QString>
#include <QStringList>

struct LaunchCommand {
  QString program;
  QStringList arguments;
  [[nodiscard]] bool isValid() const { return !program.isEmpty(); }
};
