// SPDX-License-Identifier: MIT
#include "ProbeApp.h"
#include "../common/BoardPins.h"
using namespace MotorControlRSExample;

void setup() {
    beginApplication({Board::kRs485TxPin, Board::kRs485RxPin, Board::kRs485DeRePin,
                      Board::kRs485DeReActiveHigh}, Board::kRs485ReceiverDisabledDuringTransmit);
}
void loop() { serviceApplication(); }
