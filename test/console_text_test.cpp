// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ConsoleText.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using MotorControlRSExample::Probe::renderHuman;

namespace {
void contains(const char* output, const char* expected) {
    if (!std::strstr(output, expected)) std::fprintf(stderr, "Missing '%s' in:\n%s\n", expected, output);
    assert(std::strstr(output, expected));
}
void statusAndAdmission() {
    char output[2048];
    assert(renderHuman("{\"type\":\"reply\",\"command\":\"status\",\"ok\":true,\"ready\":true,\"busy\":false,\"raw_position\":null,\"age_ms\":14}", output, sizeof(output)));
    contains(output, "[OK] status");
    contains(output, "Ready: yes");
    contains(output, "Busy: no");
    contains(output, "Position (raw): unavailable");
    contains(output, "Age (ms): 14");
    assert(!std::strchr(output, '{') && !std::strchr(output, '}'));
    assert(renderHuman("{\"command\":\"enable\",\"ok\":true,\"result\":\"accepted\",\"operation_id\":18}", output, sizeof(output)));
    assert(std::strcmp(output, "Enable accepted (operation 18).\n") == 0);
    // These local callbacks use accepted as the host return code, with no
    // asynchronous operation implied by the word alone.
    assert(renderHuman("{\"command\":\"debug\",\"ok\":true,\"result\":\"accepted\",\"mode\":\"raw\"}", output, sizeof(output)));
    contains(output, "[OK] debug");
    assert(renderHuman("{\"command\":\"motion-profile\",\"ok\":true,\"result\":\"accepted\",\"pending\":false,\"restored\":true}", output, sizeof(output)));
    contains(output, "[OK] motion-profile");
}
void evidenceAndHints() {
    char output[2048];
    const char* failure = "{\"type\":\"move\",\"command\":\"move-relative\",\"ok\":false,\"operation_id\":9,\"state\":\"failed\",\"outcome\":\"transport_error\",\"execution\":\"unknown\",\"setup_execution\":\"acknowledged\",\"completion\":\"not_observed\",\"uncertain\":true,\"failure_evidence\":{\"tx_accepted\":3,\"tx_complete\":false,\"raw_hex\":\"0103\"}}";
    assert(renderHuman(failure, output, sizeof(output)));
    contains(output, "Move uncertain (operation 9).");
    contains(output, "Reason: communication failed.");
    contains(output, "Execution is unknown. Do not repeat the write.");
    contains(output, "Motion settings were acknowledged.");
    contains(output, "completion was not observed.");
    contains(output, "Details: @1 result 9.");
    assert(!std::strstr(output, "Failure evidence:") && !std::strstr(output, "Tx accepted"));
    assert(renderHuman("{\"command\":\"move-relative\",\"ok\":false,\"error\":\"invalid_arguments\"}", output, sizeof(output)));
    contains(output, "Move rejected.");
    contains(output, "check the arguments, motor setup and readiness");
    assert(renderHuman("{\"command\":\"stop\",\"ok\":false,\"error\":\"recovery_required\"}", output, sizeof(output)));
    contains(output, "host transport needs explicit recovery");
    assert(renderHuman("{\"command\":\"cancel\",\"ok\":false,\"operation_id\":4,\"outcome\":\"cancelled\"}", output, sizeof(output)));
    contains(output, "Local cancellation does not stop the motor.");
}
void conciseMotion() {
    char output[2048];
    const char* complete = "{\"type\":\"move\",\"command\":\"move-relative\",\"ok\":true,\"operation_id\":118,\"state\":\"succeeded\",\"outcome\":\"observed\",\"completion\":\"observed\",\"execution\":\"acknowledged\",\"running_observed\":true,\"observation_known\":true,\"raw_alarm\":0,\"staging_words\":[100,100,60,0,100],\"prerequisites\":{\"raw_motion\":1}}";
    assert(renderHuman(complete, output, sizeof(output)));
    assert(std::strcmp(output, "Move complete (operation 118).\nDrive reported running, then target reached.\nNo drive alarm reported.\nDetails: @1 result 118.\n") == 0);
    // Retained result inspection is equally concise; the JSON report remains
    // the authoritative detailed view, selected before this adapter is called.
    assert(renderHuman("{\"command\":\"result\",\"move_kind\":\"absolute\",\"ok\":true,\"state\":\"succeeded\",\"completion\":\"observed\",\"running_observed\":false,\"operation_id\":12}", output, sizeof(output)));
    contains(output, "running was not observed");
    assert(!std::strstr(output, "reported running"));
    assert(renderHuman("{\"command\":\"moveto\",\"ok\":true,\"result\":\"accepted\",\"operation_id\":20}", output, sizeof(output)));
    assert(std::strcmp(output, "Move accepted (operation 20).\n") == 0);
    assert(renderHuman("{\"command\":\"result\",\"move_kind\":\"relative\",\"result\":\"pending\",\"operation_id\":20}", output, sizeof(output)));
    contains(output, "Move in progress (operation 20)");
    assert(renderHuman("{\"command\":\"move-relative\",\"state\":\"succeeded\",\"ok\":true,\"outcome\":\"acknowledged\",\"execution\":\"acknowledged\",\"completion\":\"not_observed\"}", output, sizeof(output)));
    contains(output, "Move not confirmed complete");
    contains(output, "Command acknowledged by the drive");
    assert(!std::strstr(output, "Move complete"));
    assert(renderHuman("{\"command\":\"move-relative\",\"ok\":false,\"state\":\"failed\",\"outcome\":\"cancelled\",\"interrupted_by_stop\":true,\"operation_id\":21}", output, sizeof(output)));
    contains(output, "Move interrupted by stop");
    contains(output, "Check the separate stop result");
    assert(!std::strstr(output, "Local cancellation"));
    assert(renderHuman("{\"command\":\"move-relative\",\"ok\":false,\"state\":\"failed\",\"outcome\":\"cancelled\"}", output, sizeof(output)));
    contains(output, "Move cancelled locally");
    contains(output, "Local cancellation does not stop the motor");
    assert(renderHuman("{\"command\":\"stop\",\"ok\":true,\"state\":\"succeeded\",\"completion\":\"observed\",\"operation_id\":22}", output, sizeof(output)));
    contains(output, "Stop complete (operation 22)");
    contains(output, "Drive reported stopped");
    assert(renderHuman("{\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"outcome\":\"reply_error\",\"observation_known\":true,\"raw_alarm\":4}", output, sizeof(output)));
    contains(output, "Drive alarm code: 4");
    assert(!std::strstr(output, "No drive alarm"));
    // A contradictory optimistic flag cannot hide execution uncertainty.
    assert(renderHuman("{\"command\":\"moveby\",\"ok\":true,\"state\":\"succeeded\",\"completion\":\"observed\",\"execution\":\"unknown\"}", output, sizeof(output)));
    contains(output, "Move uncertain");
    contains(output, "Do not repeat the write");
    assert(!std::strstr(output, "Move complete"));
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"operation_id\":99,\"move_operation_id\":0,\"no_motion_sent\":true,\"message\":\"Command scale is not configured.\",\"hint\":\"Set steps-per-turn before using degrees.\"}", output, sizeof(output)));
    contains(output, "Command scale is not configured.");
    contains(output, "No motion command was sent.");
    contains(output, "Next: Set steps-per-turn before using degrees.");
    contains(output, "Details: @1 result 99.");
    contains(output, "After reviewing: release 99.");
    assert(!std::strstr(output, "result 0") && !std::strstr(output, "completion was not observed"));
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"operation_id\":99,\"move_operation_id\":0,\"read_operation_id\":100,\"no_motion_sent\":true,\"message\":\"Configuration read failed; motor settings are unknown.\"}", output, sizeof(output)));
    contains(output, "Configuration read failed; motor settings are unknown.");
    contains(output, "No motion command was sent.");
    contains(output, "Details: @1 result 100.");
    contains(output, "After reviewing: release 99.");
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":true,\"state\":\"succeeded\",\"completion\":\"observed\",\"running_observed\":true,\"operation_id\":99,\"move_operation_id\":101}", output, sizeof(output)));
    contains(output, "Move complete (operation 99)");
    contains(output, "Details: @1 result 101.");
    assert(!std::strstr(output, "result 99"));
    assert(!std::strstr(output, "After reviewing"));
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"execution\":\"unknown\",\"operation_id\":99,\"move_operation_id\":101}", output, sizeof(output)));
    contains(output, "Do not repeat the write.");
    contains(output, "Details: @1 result 101.");
    contains(output, "After reviewing: release 99.");
    char tiny[100];
    assert(!renderHuman(complete, tiny, sizeof(tiny)) && tiny[0] == '\0');
}
void nestedFamiliesAndTraffic() {
    char output[4096];
    const char* nested = "{\"command\":\"future-typed-operation\",\"ok\":true,\"configuration\":{\"words\":[1,2,65535],\"fields\":[{\"name\":\"algorithm\",\"known\":false},{\"name\":\"origin\",\"value\":-9223372036854775808}]},\"timestamp_us\":18446744073709551615,\"note\":\"a \\\"quote\\\" and \\n line\"}";
    assert(renderHuman(nested, output, sizeof(output)));
    contains(output, "Configuration:\n  Words: 1, 2, 65535\n  Fields:\n    Item 1:");
    contains(output, "Known: no");
    contains(output, "Value: -9223372036854775808");
    contains(output, "Timestamp (us): 18446744073709551615");
    contains(output, "a \"quote\" and \\n line");
    const char* traffic = "{\"type\":\"traffic\",\"mode\":\"decoded\",\"transaction\":21,\"sequence\":27,\"kind\":\"RX\",\"raw_hex\":\"0103024EEACC00\",\"complete\":true,\"decode_status\":\"OK\",\"decoded\":{\"address\":1,\"function\":3,\"function_name\":\"read_registers\",\"register_names\":[\"MODEL\"],\"words\":[20202]}}";
    assert(renderHuman(traffic, output, sizeof(output)));
    contains(output, "[RX] transaction 21 (#27)\n");
    contains(output, "Bytes: 01 03 02 4E EA CC 00");
    contains(output, "Decode status: OK");
    contains(output, "Register names: MODEL");
    contains(output, "Words: 20202");
    unsigned decodedLines = 0;
    for (const char* p = output; *p; ++p) if (*p == '\n') ++decodedLines;
    assert(decodedLines == 5);
    assert(renderHuman("{\"type\":\"traffic\",\"mode\":\"decoded\",\"kind\":\"TX\",\"transaction\":2,\"raw_hex\":\"0106\",\"complete\":false,\"decode_status\":\"INVALID_RESPONSE\",\"decoded\":null}", output, sizeof(output)));
    contains(output, "[TX] transaction 2");
    contains(output, "Decode status: INVALID_RESPONSE");
    assert(!std::strstr(output, "unavailable"));
    const char* raw = "{\"type\":\"traffic\",\"mode\":\"raw\",\"sequence\":62,\"transaction\":11,\"kind\":\"RX\",\"at_us\":43521554,\"start_us\":43521000,\"end_us\":43521500,\"uncertainty_us\":20,\"length\":2,\"code\":3,\"complete\":false,\"raw_hex\":\"0103\",\"expected_request_hex\":\"\",\"expected_request_sequence\":0,\"decode_status\":null,\"decode_detail\":0,\"frame_error\":0,\"decoded\":null,\"dropped\":2}";
    assert(renderHuman(raw, output, sizeof(output)));
    contains(output, "[RX] transaction 11 at 43521554 us (#62)");
    contains(output, "Start (us): 43521000; End (us): 43521500; Uncertainty (us): 20");
    contains(output, "Length: 2; Complete: no; Code: 3; Dropped: 2");
    contains(output, "Bytes: 01 03");
    assert(!std::strstr(output, "Decode") && !std::strstr(output, "Expected") && !std::strstr(output, "unavailable"));
    unsigned lines = 0;
    for (const char* p = output; *p; ++p) if (*p == '\n') ++lines;
    assert(lines == 3);
    assert(renderHuman("{\"type\":\"traffic\",\"mode\":\"decoded\",\"transaction\":11,\"kind\":\"DIRECTION\",\"at_us\":44,\"length\":0,\"complete\":false,\"code\":1,\"raw_hex\":\"\",\"decode_status\":null,\"decoded\":null}", output, sizeof(output)));
    assert(!std::strstr(output, "Bytes:") && !std::strstr(output, "Decode"));
    contains(output, "Code: 1");
}
void boundsAndMalformed() {
    char output[512];
    for (const char* invalid : {"", "[]", "{", "{\"x\":true,}", "{\"x\":01}", "{\"x\":-}",
        "{\"x\":1.}", "{\"x\":1e+}", "{\"x\":NaN}", "{\"x\":truX}",
        "{\"x\":\"\\q\"}", "{\"x\":\"\\u1Q00\"}", "{} trailing", "{\"x\":\"unfinished}"}) {
        std::strcpy(output, "old");
        assert(!renderHuman(invalid, output, sizeof(output)) && output[0] == '\0');
    }
    std::string deep = "{\"x\":";
    for (unsigned i = 0; i < 12; ++i) deep += '[';
    deep += '0';
    for (unsigned i = 0; i < 12; ++i) deep += ']';
    deep += '}';
    assert(!renderHuman(deep.c_str(), output, sizeof(output)) && output[0] == '\0');
    std::string huge = "{\"command\":\"status\",\"ok\":true,\"data\":\"" + std::string(16384, 'x') + "\"}";
    assert(!renderHuman(huge.c_str(), output, sizeof(output)) && output[0] == '\0');
    assert(!renderHuman(nullptr, output, sizeof(output)) && output[0] == '\0');
    assert(!renderHuman("{}", nullptr, 99));
    char sentinel[3] = {'A', 'B', 'C'};
    assert(!renderHuman("{}", sentinel + 1, 0) && sentinel[1] == 'B');
    assert(!renderHuman("{}", sentinel + 1, 1));
    assert(sentinel[0] == 'A' && sentinel[1] == '\0' && sentinel[2] == 'C');
    const std::string bounded = "{\"command\":\"move-relative\",\"ok\":false,\"operation_id\":4,\"execution\":\"unknown\",\"completion\":\"not_observed\",\"evidence\":\"" + std::string(2000, 'x') + "\"}";
    assert(renderHuman(bounded.c_str(), output, sizeof(output)));
    contains(output, "Move uncertain (operation 4)");
    contains(output, "Execution is unknown");
    contains(output, "completion was not observed");
    // Long debug-only evidence does not overwhelm or truncate the summary.
    assert(!std::strstr(output, "truncated") && !std::strstr(output, "xxxx"));
    assert(std::strlen(output) < sizeof(output));
    const std::string nested = "{\"command\":\"communication\",\"ok\":true,\"snapshot\":\"" + std::string(2000, 'x') + "\",\"context\":{\"operation_id\":12,\"execution\":4,\"uncertain\":true,\"activation_unknown\":true}}";
    assert(renderHuman(nested.c_str(), output, sizeof(output)));
    contains(output, "[WAIT] communication");
    contains(output, "Operation context:\n  Operation: 12\n  Execution: 4");
    contains(output, "Uncertain: yes");
    contains(output, "Activation unknown: yes");
    contains(output, "Details truncated");
    assert(renderHuman("{\"command\":\"status\"}", output, sizeof(output)));
    contains(output, "[INFO] status");
}
} // namespace
int main() {
    statusAndAdmission();
    evidenceAndHints();
    conciseMotion();
    nestedFamiliesAndTraffic();
    boundsAndMalformed();
}
