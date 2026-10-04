// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Homing.h>
// This target sees only the installed package's exported include directories.
#include <MotorControlRS/ReadOperation.h>
#include <MotorControlRS/Axis.h>
#include <MotorControlRS/profiles/ess_rs/Reads.h>
#include <MotorControlRS/profiles/ess_rs/Actions.h>
#include <MotorControlRS/profiles/ess_rs/Position.h>
#include <MotorControlRS/VelocityOperation.h>
#include <MotorControlRS/profiles/ess_rs/Velocity.h>
#include <MotorControlRS/profiles/ess_rs/DriverSettings.h>
#include <MotorControlRS/profiles/ess_rs/Segments.h>
#include <MotorControlRS/profiles/ess_rs/ControlSettings.h>
#include <MotorControlRS/profiles/ess_rs/Tuning.h>

int main() {
    using namespace MotorControlRS;
    using namespace MotorControlRS::ESS_RS;
    ReadTarget target; target.id = 1; target.address = 1; target.generation = 2;
    ReadContext operation;
    if (!prepareIdentity(operation, target, 3, 100, 1000)) return 1;
    PreparedRead request;
    if (!nextRead(operation, 100, request) || request.length != 8 || request.first != 0 || request.count != 4) return 2;
    const uint8_t response[] = {1, 3, 8, 0x4E, 0xEA, 0x12, 0x34, 0, 1, 0xA5, 0x81, 3, 0xE3};
    ReadEvent event; event.target = target; event.operationId = 3; event.step = 0;
    event.frame = response; event.length = sizeof(response); event.qualified = true;
    event.txAccepted = 8;
    event.earliestUs = 200; event.latestUs = 220;
    if (!advanceRead(operation, event, 230)) return 3;
    IdentityObservation observation;
    if (!getIdentity(operation, observation) || observation.rawModel != 0x4EEA || observation.rawVersion != 0x1234) return 4;
    if (observation.activeSerial.known || observation.modelResolution != ReadResolution::MODEL_MAPPING_UNRESOLVED) return 5;
    if (!readCapabilities().config || !prepareConfig(operation, target, 4, 300, 1000)) return 6;
    if (!nextRead(operation, 300, request) || request.first != 0x10 || request.count != 2) return 7;
    ReadEvent cancelled; cancelled.target = target; cancelled.operationId = 4; cancelled.kind = ReadEventKind::CANCEL;
    if (!advanceRead(operation, cancelled, 310) || operation.outcome != ReadOutcome::CANCELLED) return 8;
    if (!readCapabilities().state || !prepareState(operation, target, 5, 400, 2000)) return 9;
    if (!nextRead(operation, 400, request) || request.first != 6 || request.count != 2) return 10;
    // Decode a completed block while the multi-read operation is still active.
    // This proves the installed consumer has the actual public state API.
    uint8_t motion[] = {1, 3, 4, 0, 0, 0, 0x10, 0, 0};
    const uint16_t crc = calcCrc16(motion, sizeof(motion) - 2);
    motion[7] = static_cast<uint8_t>(crc); motion[8] = static_cast<uint8_t>(crc >> 8);
    event = ReadEvent(); event.target = target; event.operationId = 5;
    event.frame = motion; event.length = sizeof(motion); event.qualified = true;
    event.earliestUs = 500; event.latestUs = 520; event.txAccepted = 8;
    if (!advanceRead(operation, event, 530)) return 11;
    StateObservation state;
    if (!getStateBlock(operation, 0, state) || !state.released || state.enabled || !state.alarmKnown) return 12;
    if (getStateBlock(operation, 2, state) || state.block != StateBlock::MOTION) return 13;
    AxisConfig axis; axis.target = target; axis.supportedRelativeBases = 1;
    VelocityRequest velocity; velocity.configurationGeneration = axis.generation;
    velocity.value = Rational(-30);
    PreparedVelocityTarget speed;
    if (!prepareVelocityTarget(velocity, axis, speed) || speed.nativeRpm != -30) return 31;
    PositionRequest position; position.configurationGeneration = axis.generation;
    position.value = Rational(INT64_C(9007199254740993));
    PreparedTarget prepared;
    if (!preparePosition(position, axis, nullptr, prepared) ||
        prepared.effectiveNative != position.value.numerator || !prepared.exactArithmetic) return 14;
    Rational number;
    if (!parseExactNumber("-0.125", number) || number.numerator != -1 || number.denominator != 8) return 15;
    ActionContext action;
    StopPolicy policy; policy.behavior = StopBehavior::CONFIGURED_DECELERATION;
    if (!MotorControlRS::prepareStop(action, target, 6, policy, 1000, 100000)) return 16;
    PreparedAction work;
    if (!nextAction(action, 1000, work) || !work.write || work.reg != 0x27 || work.value != 0x100) return 17;
    ActionEvent evidence; evidence.transport.target = target; evidence.transport.operationId = 6;
    evidence.transport.frame = work.bytes; evidence.transport.length = work.length;
    evidence.transport.txAccepted = work.length; evidence.transport.qualified = true;
    evidence.transport.earliestUs = 1100; evidence.transport.latestUs = 1200;
    evidence.txComplete = true; evidence.responseConfirmed = true;
    if (!advanceAction(action, evidence, 1300) || action.execution != ActionExecution::ACKNOWLEDGED ||
        action.completion != ActionCompletion::NOT_OBSERVED) return 18;
    if (!nextAction(action, 1300, work) || work.kind != ActionWork::WAIT) return 19;
    evidence = ActionEvent(); evidence.transport.target = target; evidence.transport.operationId = 6;
    evidence.transport.step = action.step; evidence.transport.kind = ReadEventKind::CANCEL;
    if (!advanceAction(action, evidence, 1400) || action.outcome != ActionOutcome::CANCELLED ||
        action.execution != ActionExecution::ACKNOWLEDGED) return 20;
    MovePrerequisites prerequisites; prerequisites.target = target;
    prerequisites.configurationGeneration = axis.generation;
    prerequisites.commandUnitsVerified = prerequisites.relativeBasisVerified = true;
    prerequisites.configuredRampVerified = prerequisites.serialInputsPermit = prerequisites.readinessQualified = true;
    prerequisites.wordOrderKnown = prerequisites.startSpeedKnown = true;
    prerequisites.accelerationTime = prerequisites.decelerationTime = 100;
    prerequisites.startSpeed = 10; prerequisites.observedUs = 1900; prerequisites.maximumAgeUs = 1000;
    MoveRequest move; move.position.configurationGeneration = axis.generation;
    move.position.value = Rational(20); move.speedRpm = 60; move.ramp = MoveRamp::VERIFIED_CONFIGURED;
    MoveContext moving;
    if (!MotorControlRS::prepareMoveRelative(moving, axis, nullptr, 7, move, prerequisites, 2000, 100000)) return 21;
    PreparedMove stage;
    if (!nextMove(moving, 2000, stage) || stage.length != 19 || stage.function != 16 || stage.count != 5) return 22;
    evidence = ActionEvent(); evidence.transport.target = target; evidence.transport.operationId = 7;
    evidence.transport.kind = ReadEventKind::CANCEL;
    if (!advanceMove(moving, evidence, 2100) || moving.outcome != ActionOutcome::CANCELLED || moving.uncertain) return 23;
    axis.originKnown = true; axis.originSource = ScaleSource::QUALIFIED;
    axis.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::QUALIFIED);
    AxisReference reference; reference.target = target; reference.configurationGeneration = axis.generation;
    reference.nativeKnown = reference.stationary = true; reference.source = ScaleSource::QUALIFIED;
    reference.nativePosition = 1990; reference.observedUs = 1900; reference.maximumAgeUs = 1000;
    move.position.relative = false; move.position.unit = PositionUnit::DEGREES;
    move.position.frame = CoordinateFrame::MOTOR; move.position.value = Rational(720);
    if (!MotorControlRS::prepareMoveAbsolute(moving, axis, &reference, 8, move, prerequisites, 2000, 100000) ||
        moving.prepared.effectiveNative != 2000 || moving.prepared.displacementNative != 10) return 24;
    reference.nativePosition = 990; move.position.wrapped = true; move.position.value = Rational(0);
    move.position.path = AnglePath::POSITIVE;
    if (!MotorControlRS::prepareMoveAngle(moving, axis, &reference, 9, move, prerequisites, 2000, 100000) ||
        moving.prepared.effectiveNative != 1000) return 25;
    if (!MotorControlRS::prepareSetDevicePosition(action, target, 10, 0, true, 2000, 100000) ||
        !nextAction(action, 2000, work) || work.reg != 0x2D || work.value != 49) return 26;
    reference.idle = true; reference.nowUs = 2000;
    AxisConfig candidate = axis;
    candidate.units.settings.velocity = VelocityUnit(PositionUnit::TURNS, TimeUnit::MINUTE);
    if (!configureAxis(axis, candidate, reference, &reference) || !reference.nativeKnown ||
        reference.configurationGeneration != axis.generation || reference.observedUs != 1900 ||
        reference.maximumAgeUs != 1000) return 27;
    if (!invalidateAxisReference(axis, reference) || axis.originKnown || reference.nativeKnown) return 28;
    DriverContext driver;
    if (!prepareDriverRead(driver, target, 11, axis.generation, 2100, 100000)) return 32;
    PreparedDriver setting;
    if (!nextDriver(driver, 2100, setting) || setting.write || setting.reg != 0x10 || setting.count != 2) return 33;
    evidence = ActionEvent(); evidence.transport.target = target; evidence.transport.operationId = 11;
    evidence.transport.kind = ReadEventKind::CANCEL;
    if (!advanceDriver(driver, evidence, 2200) || driver.outcome != DriverOutcome::CANCELLED) return 34;
    HomeContext home;
    HomeRequest homeRequest; homeRequest.configurationGeneration = axis.generation;
    HomePrerequisites homePrerequisites; homePrerequisites.target = target;
    homePrerequisites.configurationGeneration = axis.generation;
    homePrerequisites.qualifiedMethod = homeRequest.method;
    homePrerequisites.qualifiedSearchSpeed = homeRequest.searchSpeed;
    homePrerequisites.qualifiedReturnSpeed = homeRequest.returnSpeed;
    homePrerequisites.qualifiedRampTime = homeRequest.rampTime;
    homePrerequisites.methodQualified = homePrerequisites.nativeRatesQualified = homePrerequisites.nativeRampQualified = true;
    homePrerequisites.zeroOffsetQualified = homePrerequisites.auxiliaryQualified = homePrerequisites.inputsQualified = true;
    homePrerequisites.readinessQualified = true; homePrerequisites.observedUs = 2300; homePrerequisites.maximumAgeUs = 10000;
    if (!MotorControlRS::prepareHome(home, axis, 12, homeRequest, homePrerequisites, 2400, 100000)) return 35;
    PreparedHome homeWork;
    if (!nextHome(home, 2400, homeWork) || homeWork.length != 21 || homeWork.reg != 0x31) return 36;
    DriverRequest io; io.group = DriverGroup::IO;
    if (!prepareInputFunction(io, 0, InputFunction::UNDEFINED) || !prepareOutputFunction(io, 1, OutputFunction::UNDEFINED)) return 37;
    if (!prepareDriverRead(driver, target, 13, axis.generation, 2500, 100000, DriverGroup::IO) ||
        !nextDriver(driver, 2500, setting) || setting.reg != 0x40 || setting.count != 5) return 38;
    IoObservation ioObservation;
    if (getIo(driver, ioObservation)) return 39;
    if (!prepareSegmentRead(driver, target, 14, axis.generation, 2600, 100000, SegmentKind::POSITION, 16) ||
        !nextDriver(driver, 2600, setting) || setting.reg != 0xBA || setting.count != 5) return 40;
    SegmentObservation segment;
    if (getSegment(driver, segment)) return 41;
    if (!prepareControlRead(driver, target, 15, axis.generation, 2700, 100000) ||
        !nextDriver(driver, 2700, setting) || setting.reg != 0x100 || setting.count != 4) return 42;
    ControlObservation control;
    if (getControl(driver, control)) return 43;
    if (!prepareTuningRead(driver, target, 16, axis.generation, 2800, 100000, DriverGroup::LA) ||
        !nextDriver(driver, 2800, setting) || setting.reg != 0x112 || setting.count != 4) return 44;
    TuningObservation tuning;
    if (getTuning(driver, tuning)) return 45;
    DriverRequest tuningCandidate;
    if (!prepareTuningValue(tuningCandidate, laStageParameter(2, LaStageField::NODE), 65535) ||
        tuningCandidate.group != DriverGroup::LA || tuningCandidate.fields != 32 || tuningCandidate.tuningValues[5] != 65535) return 46;
    return 0;
}
