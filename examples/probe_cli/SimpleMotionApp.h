// SPDX-License-Identifier: MIT
// Private application orchestration. All traffic uses the ordinary typed
// executors and the same bus owner; this session owns no transport or retries.
struct SimpleAdmission {
    App& app;
    explicit SimpleAdmission(App& a) : app(a) { app.simple.admitting = true; }
    ~SimpleAdmission() { app.simple.admitting = false; }
};
void finishSimpleMotion(App& a, const MotorControlRS::Status& status, const char* error,
                        MotorControlRS::ActionOutcome outcome = MotorControlRS::ActionOutcome::NONE) {
    auto& v = a.simple.view;
    v.pending = false; v.phase = Probe::SimpleMotionPhase::TERMINAL;
    v.status = status; v.error = error; v.outcome = outcome;
}
bool releaseSimpleChild(App& a, uint32_t& id) {
    if (!id) return true;
    auto* record = findRecord(a, id);
    if (!record || !record->delivered || (record->requestId.owner && !a.owner.release(record->requestId))) return false;
    clearRecord(*record); id = 0; return true;
}
Probe::Action releaseSimpleMotion(App& a, bool retainFailedMove = false) {
    auto& s = a.simple; auto& v = s.view;
    if (v.pending || !v.delivered || !releaseSimpleChild(a, v.readOperationId)) return Probe::Action::BUSY;
    if (retainFailedMove && v.moveOperationId) {
        auto* child = findRecord(a, v.moveOperationId);
        if (!child || !child->delivered) return Probe::Action::BUSY;
        // Keep the ordinary result and its transport evidence inspectable by
        // child ID. Historical failure is not the current axis interlock.
        child->simpleChild = false;
    } else if (!releaseSimpleChild(a, v.moveOperationId)) return Probe::Action::BUSY;
    const auto desired = s.desired;
    s = App::SimpleMotionSession(); s.desired = desired;
    static_cast<Probe::SimpleMotionSettings&>(s.view) = desired;
    return Probe::Action::OK;
}
void cancelSimplePreparation(App& a, uint8_t address, uint64_t now) {
    auto& s = a.simple;
    if (!s.view.pending || s.view.address != address) return;
    s.cancelled = true; s.view.interruptedByStop = true;
    if (auto* record = findRecord(a, s.view.readOperationId)) {
        record->cancelContinuation = true;
        if (record->requestId.owner) a.owner.cancelUnsent(record->requestId, now);
    }
    auto& profile = s.view.settingsOnly ? s.settingsProfile : a.motionProfile;
    if (s.view.phase == Probe::SimpleMotionPhase::PROFILE && profile.request.owner)
        a.owner.cancelUnsent(profile.request, now);
}
bool simpleProfileReady(const App& a, uint64_t now) {
    const auto& v = a.motionProfile.view;
    return motionProfileBound(a) && v.ok && !v.pending && !v.restoreUnsettled && v.closureQualified &&
        MotorControlRS::evidenceAgeValid(v.closureEarliestUs, now, a.observationAgeUs());
}
bool simpleConfigurationReady(const App& a, uint64_t now) {
    if (!a.configuration.operationId || !Probe::sameTarget(a.configuration.target, a.axis.target) ||
        !simpleProfileReady(a, now)) return false;
    for (const auto& observation : a.configuration.provenance)
        if (!observation.qualified || !observation.status ||
            !MotorControlRS::evidenceAgeValid(observation.earliestUs, now, a.observationAgeUs())) return false;
    return true;
}
Probe::Action simpleMotion(void* context, uint32_t commandId,
                            const Probe::SimpleMotionCommand* command, Probe::SimpleMotionView& out) {
    using namespace MotorControlRS;
    using Kind = Probe::SimpleMotionCommandKind;
    App& a = *static_cast<App*>(context); auto& s = a.simple; auto& v = s.view;
    out = v; static_cast<Probe::SimpleMotionSettings&>(out) = s.desired;
    if (!command || command->kind == Kind::QUERY) return Probe::Action::OK;
    if (v.pending) return Probe::Action::BUSY;
    switch (command->kind) {
    case Kind::SPEED:
        if (command->nativeValue > 3000) return Probe::Action::INVALID;
        s.desired.speedRpm = command->nativeValue; break;
    case Kind::ACCEL: case Kind::DECEL:
        if (command->nativeValue > 2000) return Probe::Action::INVALID;
        if (command->kind == Kind::ACCEL) { s.desired.acceleration = command->nativeValue; s.desired.accelerationKnown = true; }
        else { s.desired.deceleration = command->nativeValue; s.desired.decelerationKnown = true; } break;
    case Kind::SETUP:
        if (command->setup != MoveSetup::WRITE_ALL && command->setup != MoveSetup::USE_STORED) return Probe::Action::INVALID;
        s.desired.setup = command->setup; break;
    case Kind::SCALE:
        if (command->position.value.numerator <= 0 || command->position.value.numerator > UINT32_MAX ||
            command->position.value.denominator <= 0 || command->position.value.denominator > UINT32_MAX)
            return Probe::Action::INVALID;
        s.desired.stepsPerTurn = command->position.value; s.desired.scaleKnown = true; break;
    case Kind::SETTINGS: case Kind::MOVE_BY: case Kind::MOVE_TO: {
        if (v.operationId && !v.delivered) return Probe::Action::BUSY;
        if (!platformReady) return Probe::Action::UNAVAILABLE;
        if (a.owner.needsRecovery() || uart.needsRecovery() || a.serial.blocked || !a.serial.activeKnown)
            return Probe::Action::RECOVERY_REQUIRED;
        if (a.owner.active() || a.owner.pending() || a.owner.commissioningOwned() || a.owner.configurationOwned() ||
            a.discovery.owned || acting(a) || reading(a) || a.monitorState.settings.enabled) return Probe::Action::BUSY;
        if (axisReserved(a, a.axis.target.address)) return Probe::Action::AXIS_CONFLICT;
        if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
        const bool retainFailedMove = v.moveAdmitted && !v.ok;
        bool space = false;
        for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i) {
            const auto id = a.records[i].operationId;
            space = space || !id || id == v.readOperationId || (!retainFailedMove && id == v.moveOperationId);
        }
        if (!space) return Probe::Action::RESULTS_FULL;
        if (v.operationId && releaseSimpleMotion(a, retainFailedMove) != Probe::Action::OK)
            return Probe::Action::BUSY;
        static_cast<Probe::SimpleMotionSettings&>(v) = s.desired;
        s.position = command->position; s.position.relative = command->kind == Kind::MOVE_BY;
        v.operationId = a.nextOperationId++; v.commandId = commandId;
        v.target = a.axis.target.id; v.address = a.axis.target.address; v.relative = s.position.relative;
        v.settingsOnly = command->kind == Kind::SETTINGS;
        v.pending = true; v.phase = !v.settingsOnly && simpleConfigurationReady(a, nowUs()) ? Probe::SimpleMotionPhase::STATE : Probe::SimpleMotionPhase::CONFIG;
        s.bindingGeneration = a.bindingGeneration; s.serialGeneration = a.serial.generation;
        s.deadlineUs = nowUs() + 5000000;
        a.latestOperationId = v.operationId;
        break;
    }
    default: return Probe::Action::INVALID;
    }
    out = v; static_cast<Probe::SimpleMotionSettings&>(out) = s.desired; return Probe::Action::OK;
}
void serviceSimpleMotion(App& a, uint64_t now) {
    using namespace MotorControlRS;
    using Phase = Probe::SimpleMotionPhase;
    auto& s = a.simple; auto& v = s.view;
    if (!v.operationId || v.delivered) return;
    if (!v.pending) {
        if (a.console.reportSimpleMotion(v.commandId, v.operationId, v)) v.delivered = true;
        return;
    }
    SimpleAdmission own(a);
    auto& profileSession = v.settingsOnly ? s.settingsProfile : a.motionProfile;
    if (v.settingsOnly) serviceMotionProfile(a, profileSession, now);
    if (v.moveOperationId) {
        const auto* child = findRecord(a, v.moveOperationId);
        if (!child) { finishSimpleMotion(a, Status(Err::INVALID_CONFIG, 0, "missing move result"), "Move result is unavailable"); return; }
        v.move = &child->move; v.runningObserved = child->move.runningObserved;
        v.uncertain = child->move.uncertain; v.execution = child->move.execution;
        v.completion = child->move.completion; v.interruptedByStop = child->interruptedByStop;
        if (!child->delivered) return;
        v.ok = child->move.state == ActionState::SUCCEEDED && !v.uncertain && v.execution == ActionExecution::ACKNOWLEDGED;
        if (v.ok && child->move.prepared.endpointKnown) {
            a.simpleEndpoint = child->move.prepared.endpointNative;
            a.simpleEndpointGeneration = a.axis.generation;
            a.simpleEndpointKnown = true;
        }
        finishSimpleMotion(a, child->move.status, v.ok ? "none" : "Move did not complete with a definite acknowledged result", child->move.outcome);
        return;
    }
    if (s.bindingGeneration != a.bindingGeneration || s.serialGeneration != a.serial.generation || now >= s.deadlineUs)
        s.cancelled = true;
    if (s.cancelled) {
        if (auto* read = findRecord(a, s.view.readOperationId)) {
            read->cancelContinuation = true;
            if (read->requestId.owner) a.owner.cancelUnsent(read->requestId, now);
            if (!read->delivered) return;
        }
        if (v.phase == Phase::PROFILE && profileSession.view.pending) {
            if (profileSession.request.owner) a.owner.cancelUnsent(profileSession.request, now);
            return;
        }
        finishSimpleMotion(a, Status(Err::INVALID_CONFIG, 0, "preparation cancelled"),
            v.interruptedByStop ? "Preparation interrupted by stop; no move was admitted" :
            "Preparation cancelled or expired; no move was admitted", ActionOutcome::CANCELLED);
        return;
    }
    if (v.phase == Phase::CONFIG || v.phase == Phase::STATE) {
        if (!s.view.readOperationId) {
            const auto admitted = startTypedRead(&a, v.commandId, v.address,
                v.phase == Phase::CONFIG ? ESS::ReadKind::CONFIG : ESS::ReadKind::STATE, s.view.readOperationId, false);
            a.latestOperationId = v.operationId;
            if (admitted != Probe::Action::OK) {
                finishSimpleMotion(a, Status(Err::INVALID_CONFIG, static_cast<int32_t>(admitted), "read admission"), "Preparatory read could not be admitted"); return;
            }
            findRecord(a, s.view.readOperationId)->simpleChild = true; return;
        }
        auto* child = findRecord(a, s.view.readOperationId);
        if (!child || !child->delivered) return;
        if (child->read.state != ReadState::SUCCEEDED) {
            finishSimpleMotion(a, child->read.status, "Preparatory read failed; no move was admitted", ActionOutcome::TRANSPORT_ERROR); return;
        }
        if (!releaseSimpleChild(a, s.view.readOperationId)) return;
        if (v.phase == Phase::CONFIG) {
            const auto& config = a.configuration; const auto& raw = config.raw;
            v.configKnown = true; v.subdivision = raw.subdivision; v.direction = raw.direction;
            v.wordOrder = raw.wordOrder; v.algorithm = raw.algorithm; v.encoderResolution = raw.encoderResolution;
            v.softLimitEnable = raw.softLimitEnable;
            v.directionKnown = config.directionKnown; v.wordOrderKnown = config.wordOrderKnown;
            v.algorithmKnown = config.algorithmKnown; v.softLimitKnown = config.softLimitEnableKnown;
            if (!v.settingsOnly) { v.phase = Phase::STATE; return; }
            Probe::MotionProfileView profile;
            v.profileEvidence = &s.settingsProfile.view;
            const auto admitted = motionProfileOperation(a, s.settingsProfile, Probe::MotionProfileCommand::SNAPSHOT, profile);
            if (admitted != Probe::Action::OK) {
                finishSimpleMotion(a, Status(Err::INVALID_CONFIG, static_cast<int32_t>(admitted), "profile admission"), "Cannot read motion parameters; use the retained read result for details"); return;
            }
            v.phase = Phase::PROFILE; return;
        }
        // ARRIVED is not a zero-speed predicate. A following ordinary move
        // waits through fresh, successful state observations only; failed reads
        // remain terminal, and no write is retried or replayed.
        const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
        const auto& feedback = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
        if (!Probe::fresh(motion, a.axis.target, now, a.observationAgeUs()) ||
            !Probe::fresh(feedback, a.axis.target, now, a.observationAgeUs()) || !feedback.value.pairKnown ||
            motion.value.rawAlarm || motion.value.alarmFlag || motion.value.released) {
            finishSimpleMotion(a, Status(Err::INVALID_CONFIG, 0, "stationary readiness unavailable"),
                "Fresh enabled and alarm-free state with known feedback is required"); return;
        }
        if (motion.value.running || feedback.value.rawSpeed) return;
        if (v.scaleKnown) {
            const auto& scale = a.axis.units.commandStepsPerMotorTurn;
            if (!scale.numerator || scale.numerator != static_cast<uint32_t>(v.stepsPerTurn.numerator) ||
                scale.denominator != static_cast<uint32_t>(v.stepsPerTurn.denominator)) {
                Probe::AxisCommand command; command.kind = Probe::AxisCommandKind::CONFIGURE;
                command.field = Probe::AxisField::COMMAND_SCALE; command.value = v.stepsPerTurn;
                Probe::AxisView view; const auto status = axisCommand(&a, command, view);
                if (!status) { finishSimpleMotion(a, status, "Cannot apply steps per turn without stationary axis evidence"); return; }
            }
        }
        if (a.bootOriginPending) {
            // The standalone bench uses subdivision-equivalent feedback as its
            // command-coordinate convention. Keep this explicit ASSUMED host
            // policy outside the profile; high-bit signed encoding is unresolved.
            if (!a.axis.originKnown && feedback.value.rawPosition <= INT32_MAX) {
                auto evidence = axisReference(a);
                evidence.nativeKnown = true;
                evidence.nativePosition = feedback.value.rawPosition;
                evidence.source = ScaleSource::ASSUMED;
                evidence.observedUs = std::min(motion.observedEarliestUs, feedback.observedEarliestUs);
                const auto status = setAxisOrigin(a.axis, evidence.nativePosition, evidence);
                if (!status) { finishSimpleMotion(a, status, "Cannot establish stationary boot zero"); return; }
                a.bootCoordinates = true;
            }
            a.bootOriginPending = false;
        }
        if (a.bootCoordinates && a.axis.originKnown) {
            if (feedback.value.rawPosition > INT32_MAX) invalidateAxis(a);
            else {
                auto evidence = axisReference(a);
                evidence.nativeKnown = true; evidence.nativePosition = feedback.value.rawPosition;
                evidence.source = ScaleSource::ASSUMED;
                evidence.observedUs = std::min(motion.observedEarliestUs, feedback.observedEarliestUs);
                a.coordinateReference = evidence;
            }
        }
        if (simpleProfileReady(a, now)) { v.phase = Phase::PROFILE; return; }
        Probe::MotionProfileView profile;
        const auto admitted = motionProfileCommand(&a, Probe::MotionProfileCommand::SNAPSHOT, profile);
        if (admitted != Probe::Action::OK) {
            finishSimpleMotion(a, Status(Err::INVALID_CONFIG, static_cast<int32_t>(admitted), "profile admission"), "Position profile read could not be admitted"); return;
        }
        v.phase = Phase::PROFILE; return;
    }
    if (v.phase != Phase::PROFILE || profileSession.view.pending) return;
    if (!profileSession.view.ok) {
        finishSimpleMotion(a, Status(Err::FRAME_ERROR, 0, "profile read failed"), "Position profile read failed; no move was admitted", ActionOutcome::TRANSPORT_ERROR); return;
    }
    if (v.settingsOnly) {
        std::memcpy(v.profile, profileSession.view.current, sizeof(v.profile));
        v.profileKnown = v.ok = true;
        finishSimpleMotion(a, Ok(), "none", ActionOutcome::OBSERVED);
        return;
    }
    if (!v.speedRpm) {
        finishSimpleMotion(a, Status(Err::ILLEGAL_VALUE, 0, "zero positioning speed"), "Speed is zero; set speed above zero before requesting movement"); return;
    }
    if (v.speedRpm < a.motionProfile.view.current[0]) {
        finishSimpleMotion(a, Status(Err::ILLEGAL_VALUE, 0, "speed below drive starting speed"),
            "Requested speed is below the drive starting speed; see settings"); return;
    }
    MoveRequest request; request.position = s.position; request.position.configurationGeneration = a.axis.generation;
    request.speedRpm = v.speedRpm; request.ramp = MoveRamp::VERIFIED_CONFIGURED; request.setup = v.setup;
    const auto reference = axisReference(a); PreparedTarget target;
    const auto converted = preparePosition(request.position, a.axis, reference.nativeKnown ? &reference : nullptr, target);
    if (!converted) {
        const bool missingScale = request.position.unit != PositionUnit::STEPS && !a.axis.units.commandStepsPerMotorTurn.numerator;
        finishSimpleMotion(a, converted, missingScale ? "Command scale is unknown; set stepsperturn to the established steps per motor turn" :
            (!request.position.relative && request.position.frame != CoordinateFrame::NATIVE && !a.axis.originKnown) ?
            "Absolute host coordinates need an established axis origin; use help axis" : converted.msg);
        return;
    }
    const auto& currentMotion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    const bool completedTarget = !request.position.relative && a.simpleEndpointKnown &&
        a.simpleEndpointGeneration == a.axis.generation && target.effectiveNative == a.simpleEndpoint &&
        Probe::fresh(currentMotion, a.axis.target, now, a.observationAgeUs()) && currentMotion.value.inPosition;
    if (target.zeroDisplacement || completedTarget) {
        v.ok = v.alreadyAtTarget = true;
        v.completion = ActionCompletion::OBSERVED;
        finishSimpleMotion(a, Ok(), "Requested target already satisfied; no new motion command was sent", ActionOutcome::OBSERVED);
        return;
    }
    const auto admitted = startMove(&a, v.commandId, v.address, request, v.moveOperationId);
    a.latestOperationId = v.operationId;
    if (admitted != Probe::Action::OK) {
        finishSimpleMotion(a, Status(Err::INVALID_CONFIG, static_cast<int32_t>(admitted), "move prerequisites"),
            request.setup == MoveSetup::USE_STORED ? "Stored setup does not match the checked move or motion prerequisites are unavailable" :
            "Motion prerequisites are unavailable; check enable, inputs, limits, starting speed and position units"); return;
    }
    auto* child = findRecord(a, v.moveOperationId); child->simpleChild = true;
    v.move = &child->move; v.moveAdmitted = true; v.phase = Phase::MOVE;
}
