// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace. All storage
// belongs to App; no observer path reads or consumes a live UART/result buffer.
void addSniffCount(uint64_t& value, uint64_t amount=1) {
    value = amount>UINT64_MAX-value ? UINT64_MAX : value+amount;
}
Probe::Action sniffCommand(void* context, const Probe::SniffMode* requested, Probe::SniffSnapshot& out) {
    App& a=*static_cast<App*>(context);
    if (requested) {
        if (*requested>Probe::SniffMode::DECODED) return Probe::Action::INVALID;
        if (*requested!=a.sniffState.mode) {
            // Skip only old diagnostic copies when changing display mode. This
            // bounded cursor scan leaves all independent capture readers intact.
            for (std::size_t i=0;i<16 && a.traffic.copyAfter(a.sniffCursor,a.sniffScratch);++i) {}
            a.sniffRequestKnown=false;
            a.sniffState.mode=*requested;
            a.traffic.setEnabled(*requested!=Probe::SniffMode::OFF);
        }
    }
    a.sniffState.cursor=a.sniffCursor; a.sniffState.overwritten=a.traffic.overwritten(); a.sniffState.captureDropped=a.traffic.dropped();
    a.sniffState.retained=a.traffic.size(); a.sniffState.capacity=16;
    out=a.sniffState; return Probe::Action::OK;
}
void serviceSniff(App& a) {
    if (a.sniffState.mode==Probe::SniffMode::OFF) return;
    const uint64_t previous=a.sniffCursor;
    // At most one copied event and one optional formatted line per loop.
    if (!a.traffic.copyAfter(a.sniffCursor,a.sniffScratch)) return;
    if (a.sniffCursor>previous && a.sniffCursor-previous>1)
        addSniffCount(a.sniffState.dropped,a.sniffCursor-previous-1);
    addSniffCount(a.sniffState.observed);
    const auto& record=a.sniffScratch;
    if (record.kind==MotorControlRS::TrafficKind::TX) {
        a.sniffRequest=record; a.sniffRequestKnown=true;
    }
    const auto* request=a.sniffRequestKnown && a.sniffRequest.transaction==record.transaction?&a.sniffRequest:nullptr;
    // Retain room for normal command/terminal output. Diagnostics neither wait
    // for the console nor reserve its pending-response slot under pressure.
    if (a.outputCount>=OUTPUT_LINES-2 || a.console.outputPending() ||
        !a.console.reportSniff(record,a.sniffState.mode,request)) addSniffCount(a.sniffState.dropped);
    else addSniffCount(a.sniffState.emitted);
    a.sniffState.cursor=a.sniffCursor;
}
