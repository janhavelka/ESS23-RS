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
    contains(output, "No position profile snapshot");
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
    assert(renderHuman("{\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"outcome\":\"cancelled\",\"interrupted_by_stop\":true,\"uncertain\":true,\"execution\":\"acknowledged\"}", output, sizeof(output)));
    contains(output, "Command acknowledged; final completion is uncertain");
    assert(!std::strstr(output, "Execution is unknown"));
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
    assert(!std::strstr(output, "After reviewing"));
    assert(!std::strstr(output, "result 0") && !std::strstr(output, "completion was not observed"));
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"operation_id\":99,\"move_operation_id\":0,\"read_operation_id\":100,\"no_motion_sent\":true,\"message\":\"Configuration read failed; motor settings are unknown.\"}", output, sizeof(output)));
    contains(output, "Configuration read failed; motor settings are unknown.");
    contains(output, "No motion command was sent.");
    contains(output, "Details: @1 result 100.");
    assert(!std::strstr(output, "After reviewing"));
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":true,\"state\":\"succeeded\",\"completion\":\"observed\",\"running_observed\":true,\"operation_id\":99,\"move_operation_id\":101}", output, sizeof(output)));
    contains(output, "Move complete (operation 99)");
    contains(output, "Details: @1 result 101.");
    assert(!std::strstr(output, "result 99"));
    assert(!std::strstr(output, "After reviewing"));
    assert(renderHuman("{\"type\":\"simple_move\",\"command\":\"moveby\",\"ok\":false,\"state\":\"failed\",\"execution\":\"unknown\",\"operation_id\":99,\"move_operation_id\":101}", output, sizeof(output)));
    contains(output, "Do not repeat the write.");
    contains(output, "Details: @1 result 101.");
    contains(output, "Retained details: release 101 after review; this frees result storage only.");
    char tiny[100];
    assert(!renderHuman(complete, tiny, sizeof(tiny)) && tiny[0] == '\0');
}
void conciseSettings() {
    char output[4096];
    const char* config = "{\"command\":\"read-config\",\"operation_id\":7,\"read_kind\":\"config\",\"ok\":true,\"config\":{\"raw\":{\"direction\":0,\"subdivision\":1000,\"word_order\":1,\"algorithm\":2,\"encoder_resolution\":4000,\"soft_limit_enable\":0},\"known\":{\"direction\":true,\"word_order\":true,\"algorithm\":true,\"soft_limit_enable\":true}},\"steps\":[{\"tx\":\"0103\",\"attempted_us\":100}]}";
    assert(renderHuman(config, output, sizeof(output)));
    contains(output, "Drive configuration (readback) (operation 7)");
    contains(output, "Microstep / subdivision: 1000");
    contains(output, "steps/turn mapping unresolved");
    contains(output, "Direction: normal (readback 0)");
    contains(output, "Word order: low word first (readback 1)");
    contains(output, "Control algorithm: closed loop algorithm 1");
    contains(output, "Configured encoder resolution: 4000 (readback; not measured)");
    contains(output, "Details: @1 result 7");
    assert(!std::strstr(output, "Attempted") && !std::strstr(output, "0103"));
    assert(renderHuman("{\"command\":\"result\",\"read_kind\":\"config\",\"ok\":true,\"config\":{\"raw\":{\"direction\":9,\"subdivision\":0,\"word_order\":1,\"algorithm\":2},\"known\":{\"direction\":false,\"word_order\":false,\"algorithm\":false}}}", output, sizeof(output)));
    contains(output, "Microstep / subdivision: 0");
    contains(output, "Direction: unknown (readback 9)");
    contains(output, "Word order: unknown (readback 1)");
    contains(output, "Control algorithm: unknown (readback 2)");
    assert(renderHuman("{\"command\":\"read-config\",\"ok\":false,\"status\":\"TIMEOUT\",\"operation_id\":8,\"config\":null}", output, sizeof(output)));
    contains(output, "Drive configuration read failed");
    contains(output, "Reason: TIMEOUT");
    assert(!std::strstr(output, "Microstep / subdivision"));
    assert(renderHuman("{\"command\":\"motion-profile\",\"ok\":true,\"session_ok\":true,\"saved\":true,\"current\":[30,100,200,60,1,2]}", output, sizeof(output)));
    contains(output, "Starting speed: 30 rpm");
    contains(output, "Acceleration ramp time: 100 ms");
    contains(output, "Deceleration ramp time: 200 ms");
    contains(output, "Positioning speed: 60 rpm");
    contains(output, "Target first register: 1");
    contains(output, "Target second register: 2");
    contains(output, "Target value: unavailable until word order is known");
    assert(renderHuman("{\"command\":\"motion-profile\",\"ok\":true,\"session_ok\":false,\"saved\":true,\"execution_unknown\":true,\"error\":\"write_timeout\",\"current\":[30,100,200,60,1,2]}", output, sizeof(output)));
    contains(output, "Position profile is not confirmed");
    contains(output, "Do not repeat the write");
    assert(!std::strstr(output, "Starting speed"));
    assert(renderHuman("{\"command\":\"driver\",\"driver_kind\":\"read\",\"driver_group\":\"drive\",\"ok\":true,\"operation_id\":10,\"observation\":{\"raw\":[1,1600,0,1,0,0,1],\"known_fields\":127},\"evidence\":[[1,2,3]]}", output, sizeof(output)));
    contains(output, "Drive settings read (operation 10) complete");
    contains(output, "Microstep / subdivision: 1600");
    contains(output, "Direction: reversed");
    contains(output, "Software limit setting: after homing");
    contains(output, "External position mode: absolute");
    assert(!std::strstr(output, "Evidence"));
    assert(renderHuman("{\"command\":\"driver\",\"driver_kind\":\"update\",\"ok\":false,\"status\":\"FRAME_ERROR\",\"uncertain\":true,\"operation_id\":11}", output, sizeof(output)));
    contains(output, "Drive settings update (operation 11) not confirmed");
    contains(output, "Write outcome is uncertain");
    contains(output, "Details: @1 result 11");
    assert(renderHuman("{\"command\":\"driver\",\"driver_kind\":\"update\",\"ok\":true,\"progress\":[[2,17,1000,1600,true,true,1600,false,0,\"acknowledged\"]]}", output, sizeof(output)));
    contains(output, "Microstep / subdivision: requested 1600; readback 1600");
    contains(output, "Active behavior is not established");
    assert(renderHuman("{\"command\":\"driver\",\"driver_kind\":\"update\",\"ok\":false,\"uncertain\":true,\"progress\":[[2,17,1000,1600,false,false,0,false,0,\"unknown\"]]}", output, sizeof(output)));
    contains(output, "Microstep / subdivision: requested 1600; readback not confirmed");
    contains(output, "Do not repeat the write");
}
void consolidatedSettings() {
    char output[4096];
    assert(renderHuman("{\"type\":\"motor_settings\",\"command\":\"settings\",\"pending\":true,\"ok\":true}", output, sizeof(output)));
    assert(std::strcmp(output, "Settings: reading motor...\n") == 0);
    const char* actual = "\"actual\":{\"config_known\":true,\"subdivision\":1000,\"direction\":0,\"direction_known\":true,\"word_order\":0,\"word_order_known\":true,\"algorithm\":2,\"algorithm_known\":true,\"encoder_resolution\":4000,\"soft_limit_enable\":0,\"soft_limit_known\":true,\"profile_known\":true,\"profile\":[30,100,200,60,1,2]}";
    std::string report = std::string("{\"type\":\"motor_settings\",\"ok\":true,\"operation_id\":12,") + actual + ",\"desired\":{\"speed_rpm\":50,\"acceleration\":null,\"deceleration\":0,\"steps_per_turn\":{\"numerator\":2000,\"denominator\":3}}}";
    assert(renderHuman(report.c_str(), output, sizeof(output)));
    contains(output, "Motor settings (read from drive)");
    contains(output, "Microstep / subdivision: 1000");
    contains(output, "Stored target: 65538 (unsigned native bits");
    contains(output, "Next move (host preferences; applied when requested)");
    contains(output, "  Speed: 50 rpm");
    contains(output, "  Acceleration ramp time: unavailable");
    contains(output, "  Deceleration ramp time: 0 ms");
    contains(output, "Host command steps/turn: 2000/3 (host declaration; not inferred from subdivision)");
    const auto order = report.find("\"word_order\":0"); assert(order != std::string::npos);
    report[order + std::strlen("\"word_order\":")] = '1';
    assert(renderHuman(report.c_str(), output, sizeof(output)));
    contains(output, "Stored target: 131073");
    assert(renderHuman("{\"type\":\"motor_settings\",\"ok\":true,\"actual\":{\"config_known\":false,\"subdivision\":0,\"profile_known\":false,\"profile\":[0,0,0,0,0,0]}}", output, sizeof(output)));
    contains(output, "Drive configuration: unavailable");
    contains(output, "Position profile: unavailable");
    assert(!std::strstr(output, "Microstep / subdivision: 0") && !std::strstr(output, "Starting speed"));
    assert(renderHuman("{\"type\":\"motor_settings\",\"ok\":false,\"operation_id\":13,\"read_operation_id\":14,\"message\":\"Configuration read timed out\",\"actual\":{\"config_known\":true,\"subdivision\":1000}}", output, sizeof(output)));
    contains(output, "Settings read failed");
    contains(output, "Configuration read timed out");
    contains(output, "Details: @1 result 14");
    contains(output, "After reviewing: release 13");
    assert(!std::strstr(output, "Microstep / subdivision"));
    assert(renderHuman("{\"type\":\"motor_settings\",\"ok\":true,\"actual\":{},\"desired\":{\"steps_per_turn\":{\"numerator\":0,\"denominator\":1}}}", output, sizeof(output)));
    contains(output, "Host command steps/turn: unavailable");
    assert(!std::strstr(output, "steps/turn: 0/1"));
    char small[128];
    assert(!renderHuman(report.c_str(), small, sizeof(small)) && !small[0]);
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
    conciseSettings();
    consolidatedSettings();
    nestedFamiliesAndTraffic();
    boundsAndMalformed();
}
