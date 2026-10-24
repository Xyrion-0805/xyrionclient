#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_FILE_IO_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_FILE_IO_H

#include <string>

class IStorage;

namespace modsystem
{
// Relative paths use the save directory. Exports may also use absolute paths.
// A failed write or replacement leaves an existing destination intact.
bool WriteModFileAtomic(IStorage &Storage, const std::string &Path, bool AllowAbsolute, const std::string &Text, std::string &Error);
}

#endif
