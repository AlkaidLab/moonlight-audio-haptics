// SPDX-License-Identifier: Apache-2.0

#include <moonlight_haptics/audio_haptics.h>
#include <moonlight_haptics/version.h>

int main() {
    return ah_get_abi_version() == MOONLIGHT_HAPTICS_ABI_VERSION ? 0 : 1;
}
