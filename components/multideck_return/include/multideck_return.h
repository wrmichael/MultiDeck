// Drop-in for firmware started from MultiDeck: a "Back to MultiDeck" button.
//
//   #include "multideck_return.h"
//   if (multideck_present()) { ...show a "MultiDeck" button... }
//   multideck_return();            // on tap: restarts into MultiDeck
//
// Copy this folder into your project's components/ (or add it with EXTRA_COMPONENT_DIRS).
// Without MultiDeck on the board, multideck_present() is false and multideck_return() does nothing.
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// true when the factory partition holds MultiDeck
bool multideck_present(void);

// Selects MultiDeck for the next start and restarts. Returns only if MultiDeck is not there.
void multideck_return(void);

#ifdef __cplusplus
}
#endif
