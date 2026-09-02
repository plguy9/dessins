#include "document.h"

#include "io/dsnfile.h"

#include <QFileInfo>

namespace dsn {

Document::Document(QObject *parent) : QObject(parent)
{
    m_commands.setChangedCallback([this] { onStackChanged(); });
}

Document::~Document() = default;

void Document::onStackChanged()
{
    m_netlistValid = false;
    Q_EMIT changed();
    Q_EMIT undoStateChanged();

    const bool modified = isModified();
    if (modified != m_lastModified) {
        m_lastModified = modified;
        Q_EMIT modifiedChanged(modified);
    }
}

Profile Document::profile() const
{
    Profile profile = Profile::byId(m_project.profileId);
    if (!m_project.designationFormat.isEmpty())
        profile.designation.tagFormat = m_project.designationFormat;
    if (!m_project.designationMode.isEmpty()) {
        profile.designation.mode = DesignationRule::modeFromTag(m_project.designationMode);
        // Le mode par reference de ligne repart de son propre format par
        // defaut : garder %F%N le rendrait identique au sequentiel.
        if (m_project.designationFormat.isEmpty())
            profile.designation.tagFormat.clear();
    }
    return profile;
}

const Netlist &Document::netlist() const
{
    if (!m_netlistValid) {
        m_netlist = Netlist::build(m_project);
        m_netlistValid = true;
    }
    return m_netlist;
}

void Document::setProfileId(const QString &id)
{
    if (m_project.profileId == id)
        return;
    m_project.profileId = id;
    onStackChanged();
}

QString Document::displayName() const
{
    if (m_filePath.isEmpty())
        return m_project.info.title.isEmpty() ? tr("Projet sans titre") : m_project.info.title;
    return QFileInfo(m_filePath).completeBaseName();
}

void Document::newProject(SymbolLibrary library)
{
    // `library` est une COPIE (voir document.h) : la vider par clear() ne peut
    // donc pas la vider elle-meme, meme quand l'appelant a passe la
    // bibliotheque du projet.
    m_project.clear();
    m_project.library = std::move(library);
    m_project.info.title = tr("Nouveau projet");
    m_project.info.date = QDate::currentDate();

    Folio *folio = m_project.addFolio(tr("Folio 1"));
    folio->sheet = sheetFormatById(profile().defaultSheetFormat);

    m_filePath.clear();
    m_currentFolio = 0;
    m_commands.clear();
    m_lastModified = false;
    m_netlistValid = false;

    Q_EMIT folioListChanged();
    Q_EMIT currentFolioChanged(0);
    Q_EMIT changed();
    Q_EMIT modifiedChanged(false);
}

bool Document::load(const QString &path, QString *error, QStringList *warnings)
{
    Project loaded;
    // La bibliotheque du poste sert de repli : le document embarque la sienne,
    // mais un fichier ancien peut ne pas tout contenir.
    loaded.library = m_project.library;

    const DsnLoadResult result = DsnFile::load(path, loaded);
    if (!result.ok) {
        if (error)
            *error = result.error;
        return false;
    }
    if (warnings)
        *warnings = result.warnings;

    m_project = std::move(loaded);
    m_project.resolveSymbolBounds();
    m_filePath = path;
    m_currentFolio = 0;
    m_commands.clear();
    m_lastModified = false;
    m_netlistValid = false;

    Q_EMIT folioListChanged();
    Q_EMIT currentFolioChanged(0);
    Q_EMIT changed();
    Q_EMIT modifiedChanged(false);
    return true;
}

bool Document::save(const QString &path, QString *error)
{
    if (!DsnFile::save(path, m_project, error))
        return false;
    m_filePath = path;
    m_commands.setClean();
    m_lastModified = false;
    Q_EMIT modifiedChanged(false);
    return true;
}

void Document::setCurrentFolioIndex(int index)
{
    if (index < 0 || index >= m_project.folioCount() || index == m_currentFolio)
        return;
    m_currentFolio = index;
    Q_EMIT currentFolioChanged(index);
}

Folio *Document::currentFolio() { return m_project.folioAt(m_currentFolio); }

const Folio *Document::currentFolio() const { return m_project.folioAt(m_currentFolio); }

void Document::push(CommandPtr command)
{
    if (!command)
        return;
    m_commands.push(std::move(command));
}

void Document::pushMacro(const QString &text, const std::function<void()> &body)
{
    m_commands.beginMacro(text);
    body();
    m_commands.endMacro();
}

void Document::undo()
{
    m_commands.undo();
    if (m_currentFolio >= m_project.folioCount())
        setCurrentFolioIndex(std::max(0, m_project.folioCount() - 1));
    Q_EMIT folioListChanged();
}

void Document::redo()
{
    m_commands.redo();
    if (m_currentFolio >= m_project.folioCount())
        setCurrentFolioIndex(std::max(0, m_project.folioCount() - 1));
    Q_EMIT folioListChanged();
}

} // namespace dsn
