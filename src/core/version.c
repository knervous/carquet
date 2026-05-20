#include <carquet/carquet.h>

const char* carquet_version(void) {
    return CARQUET_VERSION_STRING;
}

void carquet_version_components(int* major, int* minor, int* patch) {
    if (major) *major = CARQUET_VERSION_MAJOR;
    if (minor) *minor = CARQUET_VERSION_MINOR;
    if (patch) *patch = CARQUET_VERSION_PATCH;
}
