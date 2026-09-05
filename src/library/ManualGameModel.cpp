#include "library/ManualGameModel.h"
#include "library/GameRoles.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

// GLib parses desktop-file escaping and argument quoting without executing shell text.
#include <glib.h>

namespace {
QString stringValue(GKeyFile* file, const char* key) {
  gchar* raw = g_key_file_get_string(file, "Desktop Entry", key, nullptr);
  const QString value = QString::fromUtf8(raw ? raw : "");
  g_free(raw);
  return value;
}
QString encodedTarget(const QVariantMap& entry) {
  return QString::fromUtf8(
      QJsonDocument(QJsonObject::fromVariantMap(entry)).toJson(QJsonDocument::Compact));
}
} // namespace

ManualGameModel::ManualGameModel(const QString& path, QObject* parent)
    : QAbstractListModel(parent), m_connection("omakade-manual-" + QUuid::createUuid().toString()) {
  m_database = QSqlDatabase::addDatabase("QSQLITE", m_connection);
  m_database.setDatabaseName(path);
  if (!m_database.open()) {
    setError("Could not open the manual games database");
    return;
  }
  QSqlQuery query(m_database);
  if (!query.exec(
          "CREATE TABLE IF NOT EXISTS manual_games (id TEXT PRIMARY KEY, entry TEXT NOT NULL, "
          "favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, "
          "active INTEGER NOT NULL DEFAULT 1)")) {
    setError(query.lastError().text());
    return;
  }
  reload();
}
ManualGameModel::~ManualGameModel() {
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connection);
}
void ManualGameModel::setError(const QString& error) {
  m_error = error;
  emit stateChanged();
}
int ManualGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> ManualGameModel::roleNames() const {
  return {{GameRoles::Title, "title"},
          {GameRoles::AppId, "appId"},
          {GameRoles::Source, "source"},
          {GameRoles::Subtitle, "subtitle"},
          {GameRoles::Description, "description"},
          {GameRoles::Runner, "runner"},
          {GameRoles::Flatpak, "flatpak"},
          {GameRoles::Favorite, "favorite"},
          {GameRoles::Hidden, "hidden"},
          {GameRoles::Installed, "installed"},
          {GameRoles::InstallPath, "installPath"},
          {GameRoles::LaunchTarget, "launchTarget"},
          {GameRoles::CoverPath, "coverPath"},
          {GameRoles::HeroPath, "heroPath"},
          {GameRoles::LogoPath, "logoPath"},
          {GameRoles::Hours, "hours"},
          {GameRoles::Recent, "recent"},
          {GameRoles::LastPlayed, "lastPlayed"},
          {GameRoles::Progress, "progress"},
          {GameRoles::AchievementsUnlocked, "achievementsUnlocked"},
          {GameRoles::AchievementsTotal, "achievementsTotal"},
          {GameRoles::CoverMark, "coverMark"},
          {GameRoles::AccentStart, "accentStart"},
          {GameRoles::AccentEnd, "accentEnd"},
          {GameRoles::Year, "year"}};
}
QVariant ManualGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= count())
    return {};
  const auto& entry = m_entries.at(index.row());
  switch (role) {
  case GameRoles::Title:
    return entry.value("title");
  case GameRoles::AppId:
    return entry.value("id");
  case GameRoles::Source:
    return QStringLiteral("Manual");
  case GameRoles::Subtitle:
    return QStringLiteral("Native game");
  case GameRoles::Description:
    return entry.value("executable");
  case GameRoles::InstallPath:
    return entry.value("executable");
  case GameRoles::LaunchTarget:
    return encodedTarget(entry);
  case GameRoles::Favorite:
    return entry.value("favorite");
  case GameRoles::Hidden:
    return entry.value("hidden");
  // Keep unavailable entries visible so their paths can be repaired.
  case GameRoles::Installed:
    return true;
  case GameRoles::Flatpak:
  case GameRoles::Recent:
    return false;
  case GameRoles::CoverPath:
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
  case GameRoles::Runner:
    return QStringLiteral("");
  case GameRoles::CoverMark:
    return entry.value("title").toString().left(2).toUpper();
  case GameRoles::AccentStart:
    return QColor("#58756f");
  case GameRoles::AccentEnd:
    return QColor("#263944");
  default:
    return 0;
  }
}
void ManualGameModel::reload() {
  QVector<QVariantMap> entries;
  QSqlQuery query(m_database);
  if (!query.exec(
          "SELECT id, entry, favorite, hidden FROM manual_games WHERE active = 1 ORDER BY id")) {
    setError(query.lastError().text());
    return;
  }
  while (query.next()) {
    QVariantMap entry =
        QJsonDocument::fromJson(query.value(1).toByteArray()).object().toVariantMap();
    entry.insert("id", query.value(0));
    entry.insert("favorite", query.value(2).toBool());
    entry.insert("hidden", query.value(3).toBool());
    entries.append(entry);
  }
  beginResetModel();
  m_entries = entries;
  endResetModel();
  emit stateChanged();
}
QVariantMap ManualGameModel::get(const QString& id) const {
  for (const auto& entry : m_entries)
    if (entry.value("id").toString() == id)
      return entry;
  return {};
}

QVariantMap ManualGameModel::draftFromFile(const QUrl& url) {
  setError({});
  const QString path = url.toLocalFile();
  if (!url.isLocalFile() || !url.host().isEmpty() || !QFileInfo(path).isFile()) {
    setError("Choose a local executable or desktop entry");
    return {};
  }
  QVariantMap draft{{"title", QFileInfo(path).completeBaseName()},
                    {"executable", path},
                    {"arguments", QStringList{}},
                    {"directory", QFileInfo(path).absolutePath()}};
  if (!path.endsWith(".desktop", Qt::CaseInsensitive))
    return draft;
  if (QFileInfo(path).size() > 1024 * 1024) {
    setError("That desktop entry is too large");
    return {};
  }
  GKeyFile* file = g_key_file_new();
  const auto release = [&] { g_key_file_free(file); };
  if (!g_key_file_load_from_file(file, QFile::encodeName(path).constData(), G_KEY_FILE_NONE,
                                 nullptr)) {
    release();
    setError("Could not read that desktop entry");
    return {};
  }
  if (stringValue(file, "Type") != "Application" || stringValue(file, "Hidden") == "true" ||
      stringValue(file, "Terminal") == "true") {
    release();
    setError("Choose an application entry that does not require a terminal");
    return {};
  }
  gchar* localized = g_key_file_get_locale_string(file, "Desktop Entry", "Name", nullptr, nullptr);
  const QString title = QString::fromUtf8(localized ? localized : "");
  g_free(localized);
  const QString icon = stringValue(file, "Icon");
  const QString command = stringValue(file, "Exec");
  const QString workdir = stringValue(file, "Path");
  const QString tryExec = stringValue(file, "TryExec");
  release();
  if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty()) {
    setError("The application required by this desktop entry is not installed");
    return {};
  }
  gint argc = 0;
  gchar** argv = nullptr;
  if (!g_shell_parse_argv(command.toUtf8().constData(), &argc, &argv, nullptr) || argc == 0) {
    setError("The desktop entry has an invalid Exec command");
    return {};
  }
  QStringList args;
  bool valid = true;
  int fileCodes = 0;
  for (int n = 0; n < argc; ++n) {
    const QString token = QString::fromUtf8(argv[n]);
    if (n > 0 && token == "%i") {
      if (!icon.isEmpty())
        args.append({"--icon", icon});
      continue;
    }
    QString expanded;
    bool omitted = false;
    for (int i = 0; i < token.size(); ++i) {
      if (token[i] != '%') {
        expanded += token[i];
        continue;
      }
      if (++i == token.size()) {
        valid = false;
        break;
      }
      const QChar code = token[i];
      if (code == '%')
        expanded += '%';
      else if (n == 0)
        valid = false;
      else if (code == 'c')
        expanded += title;
      else if (code == 'k')
        expanded += path;
      else if (QStringLiteral("fFuU").contains(code)) {
        ++fileCodes;
        if (token.size() != 2)
          valid = false;
        omitted = true;
      } else if (QStringLiteral("dDnNvm").contains(code))
        omitted = token.size() == 2;
      else
        valid = false;
    }
    if (!omitted)
      args.append(expanded);
  }
  g_strfreev(argv);
  if (!valid || fileCodes > 1 || args.isEmpty() || args.first().contains('=')) {
    setError("The desktop entry uses unsupported Exec field codes");
    return {};
  }
  const QString executable = QStandardPaths::findExecutable(args.takeFirst());
  if (executable.isEmpty()) {
    setError("The desktop entry's executable could not be found");
    return {};
  }
  draft.insert("title", title);
  draft.insert("executable", executable);
  draft.insert("arguments", args);
  draft.insert("directory", workdir.isEmpty() ? QFileInfo(executable).absolutePath() : workdir);
  draft.insert("desktopEntry", path);
  return draft;
}

bool ManualGameModel::validateLaunch(const QString& target, const QString& id, QString* executable,
                                     QStringList* arguments, QString* directory, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error)
      *error = message;
    return false;
  };
  if (target.size() > 128 * 1024)
    return fail("Manual launch details are too large");
  const QJsonDocument document = QJsonDocument::fromJson(target.toUtf8());
  if (!document.isObject())
    return fail("The manual game's launch details are invalid. Edit the entry.");
  const auto object = document.object();
  if (object.value("id").toString() != id || id.isEmpty())
    return fail("The manual game identity is invalid");
  const QString program = object.value("executable").toString();
  const QString cwd = object.value("directory").toString();
  if (!QDir::isAbsolutePath(program) || !QFileInfo(program).isFile() ||
      !QFileInfo(program).isExecutable())
    return fail(
        "The executable is missing or cannot be run. Edit the manual game's path or permissions.");
  QFile executableFile(program);
  if (executableFile.open(QIODevice::ReadOnly) && executableFile.read(2) == "MZ") {
    return fail(
        "Add Windows games through Steam, Heroic, Lutris, or Faugus to configure their runner.");
  }
  if (!QDir::isAbsolutePath(cwd) || !QFileInfo(cwd).isDir())
    return fail("The working folder is missing. Edit the manual game's working folder.");
  if (!object.value("arguments").isArray())
    return fail("The argument list is invalid");
  QStringList parsed;
  for (const QJsonValue& value : object.value("arguments").toArray()) {
    if (!value.isString() || value.toString().contains(QChar::Null))
      return fail("The argument list is invalid");
    parsed.append(value.toString());
  }
  if (parsed.size() > 256 || program.contains(QChar::Null) || cwd.contains(QChar::Null))
    return fail("The manual launch details are invalid");
  if (executable)
    *executable = program;
  if (arguments)
    *arguments = parsed;
  if (directory)
    *directory = cwd;
  return true;
}
QString ManualGameModel::saveEntry(const QVariantMap& input) {
  setError({});
  const QString existing = input.value("id").toString();
  if (!existing.isEmpty() && get(existing).isEmpty()) {
    setError("That manual game no longer exists");
    return {};
  }
  const QString id =
      existing.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existing;
  const QString title = input.value("title").toString().trimmed();
  if (title.isEmpty() || title.size() > 256) {
    setError("Enter a title up to 256 characters");
    return {};
  }
  QVariantMap entry{{"id", id},
                    {"title", title},
                    {"executable", input.value("executable")},
                    {"directory", input.value("directory")},
                    {"arguments", input.value("arguments")}};
  QString error;
  const QString encoded = encodedTarget(entry);
  if (!validateLaunch(encoded, id, nullptr, nullptr, nullptr, &error)) {
    setError(error);
    return {};
  }
  QSqlQuery query(m_database);
  query.prepare("INSERT INTO manual_games(id, entry) VALUES(?, ?) ON CONFLICT(id) DO UPDATE SET "
                "entry=excluded.entry");
  query.addBindValue(id);
  query.addBindValue(encoded);
  if (!query.exec()) {
    setError("Could not save the manual game");
    return {};
  }
  reload();
  return id;
}
bool ManualGameModel::removeEntry(const QString& id) {
  if (get(id).isEmpty())
    return false;
  QSqlQuery query(m_database);
  query.prepare("UPDATE manual_games SET active=0 WHERE id=?");
  query.addBindValue(id);
  if (!query.exec()) {
    setError("Could not remove the manual game");
    return false;
  }
  reload();
  return true;
}
void ManualGameModel::toggle(int row, bool hidden) {
  if (row < 0 || row >= count())
    return;
  const QString column = hidden ? "hidden" : "favorite";
  QSqlQuery query(m_database);
  query.prepare("UPDATE manual_games SET " + column + "=? WHERE id=?");
  query.addBindValue(!m_entries.at(row).value(column).toBool());
  query.addBindValue(m_entries.at(row).value("id"));
  if (!query.exec()) {
    setError("Could not save the manual game's state");
    return;
  }
  m_entries[row].insert(column, !m_entries.at(row).value(column).toBool());
  emit dataChanged(index(row), index(row), {hidden ? GameRoles::Hidden : GameRoles::Favorite});
}
void ManualGameModel::toggleFavorite(int row) { toggle(row, false); }
void ManualGameModel::toggleHidden(int row) { toggle(row, true); }
