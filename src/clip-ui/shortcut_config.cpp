#include "clip-ui/shortcut_config.h"

namespace pastetry {
namespace {

QString slotId(const QString &prefix, int slot) {
    return QStringLiteral("%1_%2").arg(prefix).arg(slot);
}

}  // namespace

const QVector<ShortcutActionSpec> &allShortcutActionSpecs() {
    static const QVector<ShortcutActionSpec> specs = [] {
        QVector<ShortcutActionSpec> list;
        list.reserve(39);

        list.push_back({QStringLiteral("quick_paste_popup"),
                        QStringLiteral("Quick paste popup"),
                        QStringLiteral("Core"), false, false, false, 0});
        list.push_back({QStringLiteral("open_history_window"),
                        QStringLiteral("Open history window"),
                        QStringLiteral("Core"), false, false, false, 0});
        list.push_back({QStringLiteral("open_inspector"),
                        QStringLiteral("Open clipboard inspector"),
                        QStringLiteral("Core"), false, false, false, 0});

        for (int slot = 1; slot <= 9; ++slot) {
            list.push_back({slotId(QStringLiteral("copy_recent"), slot),
                            QStringLiteral("Copy recent #%1").arg(slot),
                            QStringLiteral("Recent Slots"), true, false, false, slot});
        }
        for (int slot = 1; slot <= 9; ++slot) {
            list.push_back({slotId(QStringLiteral("paste_recent"), slot),
                            QStringLiteral("Paste recent #%1").arg(slot),
                            QStringLiteral("Recent Slots"), true, true, false, slot});
        }
        for (int slot = 1; slot <= 9; ++slot) {
            list.push_back({slotId(QStringLiteral("copy_pinned"), slot),
                            QStringLiteral("Copy pinned #%1").arg(slot),
                            QStringLiteral("Pinned Slots"), true, false, true, slot});
        }
        for (int slot = 1; slot <= 9; ++slot) {
            list.push_back({slotId(QStringLiteral("paste_pinned"), slot),
                            QStringLiteral("Paste pinned #%1").arg(slot),
                            QStringLiteral("Pinned Slots"), true, true, true, slot});
        }

        return list;
    }();
    return specs;
}

const ShortcutActionSpec *findShortcutActionSpec(const QString &id) {
    const auto &specs = allShortcutActionSpecs();
    for (const auto &spec : specs) {
        if (spec.id == id) {
            return &spec;
        }
    }
    return nullptr;
}

QHash<QString, ShortcutBindingConfig> defaultShortcutBindings() {
    QHash<QString, ShortcutBindingConfig> defaults;
    const auto &specs = allShortcutActionSpecs();
    for (const auto &spec : specs) {
        defaults.insert(spec.id, ShortcutBindingConfig{});
    }
    return defaults;
}

QString shortcutBindingModeToString(ShortcutBindingMode mode) {
    switch (mode) {
        case ShortcutBindingMode::Direct:
            return QStringLiteral("direct");
        case ShortcutBindingMode::Chord:
            return QStringLiteral("chord");
        case ShortcutBindingMode::Disabled:
        default:
            return QStringLiteral("disabled");
    }
}

ShortcutBindingMode shortcutBindingModeFromString(const QString &text) {
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("direct")) {
        return ShortcutBindingMode::Direct;
    }
    if (normalized == QStringLiteral("chord")) {
        return ShortcutBindingMode::Chord;
    }
    return ShortcutBindingMode::Disabled;
}

}  // namespace pastetry

