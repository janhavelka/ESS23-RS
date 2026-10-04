// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace.
// Production movement admission consumes the same cached observations and typed
// profile snapshot used by the public operations. Diagnostics grant no access.
bool motionProfileEndpoint(const App& a) {
    const auto& profile = a.motionProfile;
    const auto& view = profile.view;
    return view.saved && view.address == a.axis.target.address &&
        view.serialGeneration == a.serial.generation && profile.bindingGeneration == a.bindingGeneration &&
        a.configuration.operationId && Probe::sameTarget(a.configuration.target, a.axis.target) &&
        std::memcmp(&profile.configuration, &a.configuration.raw, sizeof(profile.configuration)) == 0;
}
bool motionProfileBound(const App& a) {
    return motionProfileEndpoint(a) && a.motionProfile.view.generation == a.axis.generation;
}
bool motionProfileStationary(const App& a, uint64_t now) {
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    return Probe::fresh(motion, a.axis.target, now, 5000000) && !motion.value.rawAlarm &&
        !motion.value.alarmFlag && !motion.value.running &&
        !motion.value.positiveSoftLimit && !motion.value.negativeSoftLimit;
}
// The example bounds an unreferenced absolute command using fresh raw feedback.
// This application envelope does not manufacture a command-coordinate reference
// or claim a physical displacement; the core permits general native targets.
bool absoluteMoveInEnvelope(const App& a, const MotorControlRS::MoveRequest& request, uint64_t now) {
    const auto& value = request.position.value;
    if (value.numerator < 0 || !value.denominator) return false;
    const uint64_t numerator = static_cast<uint64_t>(value.numerator);
    const uint64_t integral = numerator / value.denominator;
    if (integral > 250 || (integral == 250 && numerator % value.denominator)) return false;
    const auto& feedback = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
    return !request.position.relative && request.position.frame == MotorControlRS::CoordinateFrame::NATIVE &&
        request.position.unit == MotorControlRS::PositionUnit::STEPS &&
        motionProfileBound(a) && motionProfileStationary(a, now) &&
        Probe::fresh(feedback, a.axis.target, now, 5000000) && feedback.value.pairKnown &&
        !feedback.value.rawSpeed && feedback.value.rawPosition <= 250;
}
bool moveRequirements(const App& a, const MotorControlRS::MoveRequest& request,
                      uint64_t now, ESS::MovePrerequisites& out) {
    using namespace MotorControlRS;
    const auto& profile = a.motionProfile.view;
    if (!motionProfileBound(a) || !profile.ok || profile.pending || !profile.closureQualified ||
        now < profile.closureEarliestUs || now - profile.closureEarliestUs > 30000000 ||
        request.position.wrapped || request.position.frame != CoordinateFrame::NATIVE ||
        request.position.unit != PositionUnit::STEPS ||
        (request.position.relative ? request.position.value.numerator <= 0 : request.position.value.numerator < 0) ||
        request.position.basis != RelativeBasis::ACTUAL || !motionProfileStationary(a, now)) return false;
    const auto& raw = a.configuration.raw;
    const auto& io = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::IO)];
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (!Probe::fresh(io, a.axis.target, now, 5000000) || io.value.unknownInputBits ||
        raw.softLimitEnable != 0 || !a.configuration.wordOrderKnown || motion.value.released ||
        profile.current[0] > request.speedRpm || profile.current[1] > 2000 || profile.current[2] > 2000)
        return false;
    // Unconnected wiring does not establish inactive assignments. Preserve them
    // and require the checked logical input report for each assigned input.
    for (uint8_t i = 0; i < 4; ++i)
        if (raw.inputFunctions[i] > 3 || (raw.inputFunctions[i] && io.value.inputs[i])) return false;
    if (!request.position.relative && !absoluteMoveInEnvelope(a, request, now)) return false;

    // Publish only after every guard passes; rejected preparation preserves out.
    // Native words need no physical scale or undocumented algorithm claim.
    out = ESS::MovePrerequisites();
    out.target = a.axis.target;
    out.configurationGeneration = a.axis.generation;
    out.commandUnitsVerified = out.relativeBasisVerified = true;
    out.configuredRampVerified = out.serialInputsPermit = out.readinessQualified = true;
    out.accelerationTime = profile.current[1];
    out.decelerationTime = profile.current[2];
    out.startSpeedKnown = true;
    out.startSpeed = profile.current[0];
    out.maximumAgeUs = 3000000;
    return true;
}
