// SPDX-License-Identifier: MIT
#pragma once
#include "RtuBusOwner.h"

namespace MotorControlRSExample { namespace Rtu {
/** ESS FC03/06/reviewed FC10 checks using the shipped profile builders/parsers.
 * This validates raw codec access and acknowledgements, not motion readiness or
 * write value meaning. No I/O or retained storage. Console integration is separate. */
Validator essValidator() noexcept;
}}
