// SPDX-License-Identifier: MIT
// Link the actual package archive without calling descriptive catalogue APIs.
#include <MotorControlRS/profiles/ess_rs/Codec.h>

int main() {
    namespace Ess = MotorControlRS::ESS_RS;
    uint8_t request[Ess::READ_REQUEST_LEN] = {};
    if (Ess::buildProbe(1, request, sizeof(request)) != sizeof(request) ||
        request[0] != 1 || request[1] != 3 || Ess::calcCrc16(request, sizeof(request)) != 0)
        return 1;

    const uint8_t reply[] = {1, 3, 2, 3, 5, 0x78, 0xB7};
    uint16_t model = 0;
    if (!Ess::parseProbe(reply, sizeof(reply), 1, model) || model != 0x0305)
        return 2;
    if (Ess::parseProbe(reply, sizeof(reply), 2, model) || model != 0x0305)
        return 3;
    uint16_t words[2] = {0xAAAA, 0xBBBB};
    std::size_t count = 2;
    if (Ess::parseRegisters(reply, sizeof(reply), 1, 2, words, 2, count) ||
        count != 0 || words[0] != 0xAAAA || words[1] != 0xBBBB)
        return 4;
    return 0;
}
