// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace. The session
// observes the ordinary owner path; it never reads UART or consumes live results.
void addDebugCount(uint64_t& value, uint64_t amount = 1) {
    value = amount > UINT64_MAX - value ? UINT64_MAX : value + amount;
}
bool copyDebugRecord(Probe::DebugSession& debug) {
    const uint64_t previous = debug.cursor;
    if (!debug.capture.copyAfter(debug.cursor, debug.scratch)) return false;
    if (debug.cursor > previous && debug.cursor - previous > 1)
        addDebugCount(debug.state.missed, debug.cursor - previous - 1);
    return true;
}
Probe::Action debugCommand(void* context, const Probe::DebugMode* requested,
                           Probe::DebugSnapshot& out) {
    App& a = *static_cast<App*>(context);
    auto& debug = a.debug;
    if (requested) {
        if (*requested > Probe::DebugMode::DECODED) return Probe::Action::INVALID;
        if (*requested != debug.state.mode) {
            // Disabling first publishes any partial RX copy. Account for every
            // retained copy intentionally skipped by this display-mode change;
            // other readers retain independent cursors and the same raw records.
            debug.capture.setEnabled(false);
            for (std::size_t i = 0;
                 i < Probe::DebugSession::CAPACITY && copyDebugRecord(debug); ++i)
                addDebugCount(debug.state.skipped);
            debug.requestKnown = false;
            debug.state.mode = *requested;
            debug.capture.setEnabled(*requested != Probe::DebugMode::OFF);
        }
    }
    debug.state.cursor = debug.cursor;
    debug.state.overwritten = debug.capture.overwritten();
    debug.state.captureDropped = debug.capture.dropped();
    debug.state.retained = debug.capture.size();
    debug.state.capacity = Probe::DebugSession::CAPACITY;
    out = debug.state;
    return Probe::Action::OK;
}
void serviceDebug(App& a) {
    auto& debug = a.debug;
    if (debug.state.mode == Probe::DebugMode::OFF || !copyDebugRecord(debug)) return;
    // At most one copied event and one optional formatted line per loop.
    addDebugCount(debug.state.observed);
    const auto& record = debug.scratch;
    if (record.kind == MotorControlRS::TrafficKind::TX) {
        debug.request = record;
        debug.requestKnown = true;
    }
    const auto* request = debug.requestKnown && debug.request.transaction == record.transaction ?
        &debug.request : nullptr;
    // Preserve ordinary replies/results under output pressure. A dropped display
    // copy is separate from a record overwritten before this reader copied it.
    if (a.outputCount >= OUTPUT_LINES - 2 || a.console.outputPending() ||
        !a.console.reportTraffic(record, debug.state.mode, request))
        addDebugCount(debug.state.dropped);
    else addDebugCount(debug.state.emitted);
    debug.state.cursor = debug.cursor;
}
