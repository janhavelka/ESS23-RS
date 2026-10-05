// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace.
// Production movement admission consumes the same cached observations and typed
// profile snapshot used by the public operations. Diagnostics grant no access.
bool motionProfileEndpoint(const App& a, const App::MotionProfileState& profile) {
    const auto& view = profile.view;
    return view.saved && view.address == a.axis.target.address &&
        view.serialGeneration == a.serial.generation && profile.bindingGeneration == a.bindingGeneration &&
        a.configuration.operationId && Probe::sameTarget(a.configuration.target, a.axis.target) &&
        std::memcmp(&profile.configuration, &a.configuration.raw, sizeof(profile.configuration)) == 0;
}
bool motionProfileEndpoint(const App& a) { return motionProfileEndpoint(a, a.motionProfile); }
bool motionProfileBound(const App& a) {
    return motionProfileEndpoint(a) && a.motionProfile.view.generation == a.axis.generation;
}
bool motionProfileStationary(const App& a, uint64_t now) {
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    return Probe::fresh(motion, a.axis.target, now, a.observationAgeUs()) && !motion.value.rawAlarm &&
        !motion.value.alarmFlag && !motion.value.running &&
        !motion.value.positiveSoftLimit && !motion.value.negativeSoftLimit;
}
// Native absolute targets need no manufactured current-position reference.
// Core preparation still enforces caller limits, units, origin and wire range;
// the application requires fresh stopped feedback before starting the move.
bool absoluteMoveReady(const App& a, const MotorControlRS::MoveRequest& request, uint64_t now) {
    MotorControlRS::PreparedTarget target;
    const auto reference = axisReference(a);
    if (!MotorControlRS::preparePosition(request.position, a.axis,
            reference.nativeKnown ? &reference : nullptr, target)) return false;
    const auto& feedback = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
    return !request.position.relative && motionProfileBound(a) && motionProfileStationary(a, now) &&
        Probe::fresh(feedback, a.axis.target, now, a.observationAgeUs()) && feedback.value.pairKnown &&
        !feedback.value.rawSpeed;
}
bool moveRequirements(const App& a, const MotorControlRS::MoveRequest& request,
                      uint64_t now, ESS::MovePrerequisites& out) {
    using namespace MotorControlRS;
    const auto& profile = a.motionProfile.view;
    if (request.position.frame == CoordinateFrame::NATIVE && request.position.unit == PositionUnit::STEPS &&
        (request.position.relative ? request.position.value.numerator <= 0 : request.position.value.numerator < 0)) return false;
    if (!motionProfileBound(a) || !profile.ok || profile.pending || !profile.closureQualified ||
        !MotorControlRS::evidenceAgeValid(profile.closureEarliestUs, now, a.observationAgeUs()) ||
        request.position.wrapped ||
        request.position.basis != RelativeBasis::ACTUAL || !motionProfileStationary(a, now)) return false;
    const auto& raw = a.configuration.raw;
    const auto& io = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::IO)];
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (!Probe::fresh(io, a.axis.target, now, a.observationAgeUs()) || io.value.unknownInputBits ||
        raw.softLimitEnable != 0 || !a.configuration.wordOrderKnown || motion.value.released ||
        profile.current[0] > request.speedRpm || profile.current[1] > 2000 || profile.current[2] > 2000)
        return false;
    // Unconnected wiring does not establish inactive assignments. Preserve them
    // and require the checked logical input report for each assigned input.
    for (uint8_t i = 0; i < 4; ++i)
        if (raw.inputFunctions[i] > 3 || (raw.inputFunctions[i] && io.value.inputs[i])) return false;
    if (!request.position.relative && !absoluteMoveReady(a, request, now)) return false;

    // Publish only after every guard passes; rejected preparation preserves out.
    // prepareMove performs the shared unit conversion and signed/range checks.
    // These facts qualify native command words, not the caller's physical scales.
    out = ESS::MovePrerequisites();
    out.target = a.axis.target;
    out.configurationGeneration = a.axis.generation;
    out.commandUnitsVerified = out.relativeBasisVerified = true;
    out.configuredRampVerified = out.serialInputsPermit = out.readinessQualified = true;
    out.accelerationTime = profile.current[1];
    out.decelerationTime = profile.current[2];
    out.startSpeedKnown = true;
    out.startSpeed = profile.current[0];
    out.maximumAgeUs = a.observationAgeUs();
    return true;
}
