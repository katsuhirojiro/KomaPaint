#pragma once

#include "../material/PaintMixerModel.h"

#include <QString>
#include <vector>

namespace komapaint {

struct SavedMixture {
    QString id;
    QString name;
    QString notes;
    QString createdAt;
    QString updatedAt;
    material::PaintBatch batch;
};

struct MixLibraryData {
    QString activeMediumId = QStringLiteral("gouache");
    QString activeDepositId;
    std::vector<material::MixerDeposit> workspaceDeposits;
    std::vector<SavedMixture> savedMixtures;
};

class PaintMixerStore {
public:
    static constexpr int FormatVersion = 1;

    enum class LoadResult { Loaded, Missing, Invalid };

    static LoadResult load(const QString& path,
                           const material::PigmentLibrary& pigments,
                           MixLibraryData* data,
                           QString* error = nullptr,
                           int* skippedInvalidCount = nullptr);

    static bool save(const QString& path,
                     const MixLibraryData& data,
                     QString* error = nullptr);
};

} // namespace komapaint
