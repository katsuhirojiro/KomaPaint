#pragma once

#include <QString>

namespace komapaint {

// Explicit result of a project creation attempt. Never leaves the caller
// guessing whether creation succeeded or why it failed.
struct ProjectCreationResult {
    bool ok = false;
    QString error;
};

} // namespace komapaint
