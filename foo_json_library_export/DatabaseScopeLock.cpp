#include "DatabaseScopeLock.h"

namespace libraryexport {

//------------------------------------------------------------------------------

DatabaseScopeLock::DatabaseScopeLock()
	: db()
{
	db->database_lock();
}

//------------------------------------------------------------------------------

DatabaseScopeLock::~DatabaseScopeLock()
{
	db->database_unlock();
}

//------------------------------------------------------------------------------

} // namespace libraryexport
