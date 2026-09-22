// miniaudio's implementation, in a translation unit of its own so that its tens of thousands
// of lines compile once and its warnings stay out of the engine's (CMakeLists.txt builds this
// file with them off). Everything else includes the header alone.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
