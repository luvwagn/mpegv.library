/* utility/tagitem.h's names for the tests on this computer: a tag's data
 * as wide as a pointer here. MIT licence, (c) 2026 Brian King. */
#ifndef UTILITY_TAGITEM_H
#define UTILITY_TAGITEM_H
#include <exec/types.h>
typedef ULONG Tag;
struct TagItem {
    Tag ti_Tag;
    unsigned long ti_Data;
};
#define TAG_DONE   0UL
#define TAG_END    0UL
#define TAG_IGNORE 1UL
#define TAG_MORE   2UL
#define TAG_SKIP   3UL
#define TAG_USER   0x80000000UL
#endif
