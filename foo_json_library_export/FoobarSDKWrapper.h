#pragma once

#define NOMINMAX

#pragma warning (push)
#pragma warning (disable: 4100) // unreferenced formal parameter
#pragma warning (disable: 4127) // conditional expression is constant
#pragma warning (disable: 4189) // local variable is initialized but not referenced
#pragma warning (disable: 4239) // nonstandard extension used : 'argument' : conversion from X to Y
#pragma warning (disable: 4245) // conversion from X to Y, signed/unsigned mismatch
#pragma warning (disable: 4510) // default constructor could not be generated
#pragma warning (disable: 4512) // assignment operator could not be generated
#pragma warning (disable: 4610) // class X can never be instantiated - user defined constructor required
#pragma warning (disable: 4995) // name was marked as #pragma deprecated
#include "foobar2000/SDK/foobar2000.h"
#pragma warning (pop)

// Sadly disabled at file scope as it can't be done per-header.
#pragma warning (disable: 4505) // unreferenced local function has been removed
