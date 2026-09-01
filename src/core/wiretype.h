// Types de fils — l'equivalent des Wire Types d'AutoCAD Electrical.
//
// Un schema ne distingue pas ses conducteurs par leur seule position : un
// 24 V continu, une phase 400 V et un circuit de securite se lisent a leur
// couleur et a leur section. Le type porte ces caracteristiques une fois pour
// toutes, et chaque fil s'y rattache — changer la couleur d'un potentiel se
// fait alors en un endroit, pas fil par fil.
//
// Le type porte aussi le nom de calque : c'est lui qui structure l'export DXF,
// exactement comme AutoCAD Electrical range ses fils par calque.
//
// La couleur est un simple entier 0xRRGGBB et le style un mot-cle : QColor et
// Qt::PenStyle vivent dans QtGui, que le coeur ne reference pas. La conversion
// se fait dans render/, ou QtGui est deja lie.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace dsn {

// Styles de trait connus. Le rendu les traduit en Qt::PenStyle.
namespace WireStyle {
inline QString solid() { return QStringLiteral("solid"); }
inline QString dashed() { return QStringLiteral("dashed"); }
inline QString dotted() { return QStringLiteral("dotted"); }
inline QString dashDot() { return QStringLiteral("dashdot"); }
} // namespace WireStyle

struct WireType {
    QString id;              // cle stable, referencee par les fils
    QString name;            // « L1 — phase 1 »
    quint32 rgb = 0x0A5C9Eu; // couleur 0xRRGGBB, sans QColor
    double width = 0.35;     // epaisseur de trait, en millimetres
    QString crossSection;    // « 1,5 mm² » ou « 14 AWG »
    QString layer;           // calque d'export DXF
    QString style;           // vide = plein ; voir WireStyle
    QString note;

    bool isValid() const { return !id.isEmpty(); }

    // « #rrggbb », la forme lisible utilisee par le fichier et par le DXF.
    QString colorName() const;
    void setColorName(const QString &text);

    QJsonObject toJson() const;
    static WireType fromJson(const QJsonValue &value);
};

// Le jeu de types d'un projet. Il embarque toujours un type par defaut, pour
// qu'un fil sans type reste tracable.
class WireTypeSet
{
public:
    WireTypeSet();

    void insert(const WireType &type);
    void remove(const QString &id);
    void clear();

    bool contains(const QString &id) const;
    const WireType *type(const QString &id) const;
    // Type d'un fil, avec repli sur le type par defaut : un identifiant
    // inconnu ne doit jamais rendre un fil invisible.
    const WireType &resolve(const QString &id) const;

    QList<WireType> all() const;
    int count() const { return int(m_types.size()); }

    static QString defaultId() { return QStringLiteral("default"); }

    // Jeux fournis selon la norme : couleurs de conducteurs CEI ou usage
    // nord-americain.
    static WireTypeSet forNorm(const QString &norm);

    QJsonArray toJson() const;
    void readJson(const QJsonValue &value);

private:
    QList<WireType> m_types;
};

} // namespace dsn
