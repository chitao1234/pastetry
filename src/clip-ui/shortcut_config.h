#pragma once

#include <QHash>
#include <QKeySequence>
#include <QString>
#include <QVector>

namespace pastetry {

enum class ShortcutBindingMode {
    Disabled = 0,
    Direct = 1,
    Chord = 2,
};

struct ShortcutBindingConfig {
    ShortcutBindingMode mode = ShortcutBindingMode::Disabled;
    QKeySequence directSequence;
    QKeySequence chordFirstSequence;
    QKeySequence chordSecondSequence;

    bool operator==(const ShortcutBindingConfig &other) const {
        return mode == other.mode && directSequence == other.directSequence &&
               chordFirstSequence == other.chordFirstSequence &&
               chordSecondSequence == other.chordSecondSequence;
    }
    bool operator!=(const ShortcutBindingConfig &other) const {
        return !(*this == other);
    }
};

struct ShortcutActionSpec {
    QString id;
    QString label;
    QString groupLabel;
    bool isSlotAction = false;
    bool isPasteAction = false;
    bool pinnedGroup = false;
    int slot = 0;
};

const QVector<ShortcutActionSpec> &allShortcutActionSpecs();
const ShortcutActionSpec *findShortcutActionSpec(const QString &id);
QHash<QString, ShortcutBindingConfig> defaultShortcutBindings();

QString shortcutBindingModeToString(ShortcutBindingMode mode);
ShortcutBindingMode shortcutBindingModeFromString(const QString &text);

}  // namespace pastetry

