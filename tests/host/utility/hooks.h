/* utility/hooks.h's names for the tests on this computer (a hook called
 * as a C function: hook, object, message). MIT licence, (c) 2026 Brian King. */
#ifndef UTILITY_HOOKS_H
#define UTILITY_HOOKS_H
#include <exec/types.h>
typedef ULONG (*HOOKFUNC)();
struct MinNode {
    struct MinNode *mln_Succ, *mln_Pred;
};
struct Hook {
    struct MinNode h_MinNode;
    HOOKFUNC h_Entry;
    HOOKFUNC h_SubEntry;
    APTR h_Data;
};
#endif
