#ifndef LOOTCLI_STUB_H
#define LOOTCLI_STUB_H

// Stub lootcli header for Fluorine port (real lootcli lib not vendored).
// Only the types referenced by non-LOOT code (Settings::lootLogLevel) remain.

namespace lootcli
{

enum class LogLevels
{
  Trace,
  Debug,
  Info,
  Warning,
  Error,
};

}  // namespace lootcli

#endif  // LOOTCLI_STUB_H
