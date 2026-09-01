// Le document ouvert : le projet, sa pile d'annulation, son fichier et sa
// netlist en cache.
//
// La netlist est recalculee paresseusement. Elle est invalidee a chaque
// modification et reconstruite au premier besoin : la recalculer a chaque
// frappe couterait cher pour rien, la laisser perimee donnerait des rapports
// faux.
#pragma once

#include "core/command.h"
#include "core/netlist.h"
#include "core/project.h"
#include "rules/profile.h"

#include <QObject>
#include <QString>

#include <memory>

namespace dsn {

class Document : public QObject
{
    Q_OBJECT

public:
    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    Project &project() { return m_project; }
    const Project &project() const { return m_project; }
    CommandStack &commands() { return m_commands; }

    // Profil du projet, avec les reglages de reperage que le projet impose.
    // Le profil seul dirait la norme ; c'est le projet qui dit la convention
    // de la maison.
    Profile profile() const;
    void setProfileId(const QString &id);

    const Netlist &netlist() const;
    void invalidateNetlist() { m_netlistValid = false; }

    const QString &filePath() const { return m_filePath; }
    bool isModified() const { return !m_commands.isClean(); }
    QString displayName() const;

    void newProject(const SymbolLibrary &library);
    bool load(const QString &path, QString *error, QStringList *warnings = nullptr);
    bool save(const QString &path, QString *error);

    int folioCount() const { return m_project.folioCount(); }
    int currentFolioIndex() const { return m_currentFolio; }
    void setCurrentFolioIndex(int index);
    Folio *currentFolio();
    const Folio *currentFolio() const;

    // Toute modification passe par ici : la pile d'annulation, la netlist et
    // les vues restent ainsi synchronisees par construction.
    void push(CommandPtr command);
    void pushMacro(const QString &text, const std::function<void()> &body);

    void undo();
    void redo();

Q_SIGNALS:
    void changed();
    void folioListChanged();
    void currentFolioChanged(int index);
    void modifiedChanged(bool modified);
    void undoStateChanged();

private:
    void onStackChanged();

    Project m_project;
    CommandStack m_commands;
    QString m_filePath;
    int m_currentFolio = 0;
    bool m_lastModified = false;

    mutable Netlist m_netlist;
    mutable bool m_netlistValid = false;
};

} // namespace dsn
