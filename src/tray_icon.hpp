#pragma once
#include "common.hpp"

namespace mchose {
// Software antialiasing into a small premultiplied DIB. No graphics runtime.
HICON MakeIcon(int size,int percent,bool valid,bool charging,bool dark);
}
