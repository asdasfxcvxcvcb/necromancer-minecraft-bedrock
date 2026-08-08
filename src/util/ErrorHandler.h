#pragma once

#ifdef NECROMANCER_CRASH_REPORTING
#include "util/ExceptionHandler.h"

#define NECROMANCER_ERROR_HANDLER_CONCAT_INNER(a, b) a##b
#define NECROMANCER_ERROR_HANDLER_CONCAT(a, b) NECROMANCER_ERROR_HANDLER_CONCAT_INNER(a, b)

#define BEGIN_ERROR_HANDLER                                                                                    \
    DebugExceptionHandler::ErrorBoundaryScope NECROMANCER_ERROR_HANDLER_CONCAT(necromancerErrorBoundaryScope, __LINE__); \
    try {
#define END_ERROR_HANDLER                                                                \
    }                                                                                    \
    catch (const std::exception& e) {                                                    \
        LogExceptionDetails(e);                                                          \
        DebugExceptionHandler::AbortProcess();                                           \
    }                                                                                    \
    catch (...) {                                                                        \
        LogUnknownExceptionDetails("Caught unknown exception at Necromancer error boundary"); \
        DebugExceptionHandler::AbortProcess();                                           \
    }

#else
#define BEGIN_ERROR_HANDLER
#define END_ERROR_HANDLER
#endif
