#ifndef SUSAMUNE_ASSIST_HXX
#define SUSAMUNE_ASSIST_HXX

#include <Dolphin/types.h>

namespace Assist {

enum Reason : u8 {
    OTHER             = 1 << 0,
    KING_BOO_FRUIT    = 1 << 1,
    PETEY_NO_TORNADO  = 1 << 2,
    PETEY_ROUTE       = 1 << 3,
};

}  // namespace Assist

#endif  // SUSAMUNE_ASSIST_HXX
