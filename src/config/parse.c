#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "common/array.h"
#include "log/log.h"

typedef struct {
    const wchar_t* start;
    size_t length;
} mora_token_t;

static const wchar_t* mora_skip_space(const wchar_t* at, const wchar_t* end)
{
    while (at < end && (*at == L' ' || *at == L'\t')) {
        ++at;
    }

    return at;
}

static BOOL mora_take_token(const wchar_t** at, const wchar_t* end, mora_token_t* token)
{
    const wchar_t* start = mora_skip_space(*at, end);

    if (start == end) {
        return FALSE;
    }

    const wchar_t* stop = start;

    while (stop < end && *stop != L' ' && *stop != L'\t') {
        ++stop;
    }

    token->start = start;
    token->length = (size_t)(stop - start);
    *at = stop;

    return TRUE;
}

static BOOL mora_token_is(const mora_token_t* token, const wchar_t* text)
{
    size_t length = wcslen(text);

    return token->length == length && _wcsnicmp(token->start, text, length) == 0;
}

static BOOL mora_all_digits(const mora_token_t* token, unsigned long* value)
{
    if (token->length == 0 || token->length > 9) {
        return FALSE;
    }

    unsigned long number = 0;

    for (size_t i = 0; i < token->length; ++i) {
        wchar_t digit = token->start[i];

        if (digit < L'0' || digit > L'9') {
            return FALSE;
        }

        number = number * 10 + (unsigned long)(digit - L'0');
    }

    *value = number;

    return TRUE;
}

static BOOL mora_take_number(const wchar_t** at, const wchar_t* end, unsigned long* value)
{
    mora_token_t token;

    return mora_take_token(at, end, &token) && mora_all_digits(&token, value);
}

static const wchar_t* mora_trim_end(const wchar_t* start, const wchar_t* end)
{
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t')) {
        --end;
    }

    return end;
}

static void mora_append_placement(mora_config_t* config, mora_index_t workspace, mora_index_t monitor)
{
    mora_config_placement_t* entries = mora_array_reserve(config->placements,
        &config->placement_capacity, config->placement_count + 1, sizeof(*entries));

    if (!entries) {
        mora_log(L"config: out of memory, a placement line is ignored");

        return;
    }

    config->placements = entries;
    config->placements[config->placement_count++] = (mora_config_placement_t){
        .workspace = workspace, .monitor = monitor };
}

static void mora_append_launch(mora_config_t* config, const wchar_t* command, size_t length,
    mora_index_t workspace, mora_index_t monitor)
{
    mora_config_launch_t* entries = mora_array_reserve(config->launches, &config->launch_capacity,
        config->launch_count + 1, sizeof(*entries));

    if (!entries) {
        mora_log(L"config: out of memory, an exec line is ignored");

        return;
    }

    config->launches = entries;
    mora_config_launch_t* launch = &entries[config->launch_count++];

    *launch = (mora_config_launch_t){ .placement = { .workspace = workspace, .monitor = monitor } };
    wmemcpy(launch->command, command, length);
    launch->command[length] = L'\0';
}

static void mora_parse_workspace(mora_config_t* config, const wchar_t** at, const wchar_t* end,
    size_t number)
{
    unsigned long workspace = 0;
    unsigned long monitor = 0;
    mora_token_t keyword;

    if (!mora_take_number(at, end, &workspace) || workspace < 1 || workspace > MORA_WORKSPACE_COUNT) {
        mora_log(L"config: line %zu: expected a workspace number 1..9", number);

        return;
    }

    if (!mora_take_token(at, end, &keyword) || !mora_token_is(&keyword, L"monitor")) {
        mora_log(L"config: line %zu: expected \"monitor\"", number);

        return;
    }

    if (!mora_take_number(at, end, &monitor) || monitor < 1 || monitor > MORA_WORKSPACE_COUNT) {
        mora_log(L"config: line %zu: expected a monitor number 1..9", number);

        return;
    }

    mora_token_t extra;

    if (mora_take_token(at, end, &extra)) {
        mora_log(L"config: line %zu: unexpected \"%.*ls\"", number, (int)extra.length, extra.start);

        return;
    }

    mora_append_placement(config, (mora_index_t)(workspace - 1), (mora_index_t)(monitor - 1));
}

static void mora_parse_exec(mora_config_t* config, const wchar_t* begin, const wchar_t* end,
    size_t number)
{
    mora_index_t workspace = MORA_INVALID_INDEX;
    mora_index_t monitor = MORA_INVALID_INDEX;
    const wchar_t* command_end = end;

    /* "workspace N" / "monitor N" are peeled off the END of the line, one
     * pair at a time, so the command itself may contain those words. */
    for (;;) {
        const wchar_t* tail = mora_trim_end(begin, command_end);

        if (tail == begin) {
            break;
        }

        const wchar_t* digits = tail;

        while (digits > begin && digits[-1] != L' ' && digits[-1] != L'\t') {
            --digits;
        }

        mora_token_t value_token = { digits, (size_t)(tail - digits) };
        unsigned long value = 0;

        if (!mora_all_digits(&value_token, &value)) {
            break;
        }

        const wchar_t* keyword_end = mora_trim_end(begin, digits);

        if (keyword_end == begin) {
            break;
        }

        const wchar_t* keyword = keyword_end;

        while (keyword > begin && keyword[-1] != L' ' && keyword[-1] != L'\t') {
            --keyword;
        }

        mora_token_t keyword_token = { keyword, (size_t)(keyword_end - keyword) };
        BOOL is_workspace = mora_token_is(&keyword_token, L"workspace");

        if (!is_workspace && !mora_token_is(&keyword_token, L"monitor")) {
            break;
        }

        if (value < 1 || value > MORA_WORKSPACE_COUNT) {
            mora_log(L"config: line %zu: %ls needs a number 1..9", number,
                is_workspace ? L"workspace" : L"monitor");

            return;
        }

        mora_index_t* target = is_workspace ? &workspace : &monitor;

        if (*target != MORA_INVALID_INDEX) {
            mora_log(L"config: line %zu: %ls appears twice", number,
                is_workspace ? L"workspace" : L"monitor");

            return;
        }

        *target = (mora_index_t)(value - 1);
        command_end = keyword;
    }

    const wchar_t* command = begin;

    while (command < command_end && (*command == L' ' || *command == L'\t')) {
        ++command;
    }

    command_end = mora_trim_end(command, command_end);
    size_t length = (size_t)(command_end - command);

    if (length == 0) {
        mora_log(L"config: line %zu: exec needs a command", number);

        return;
    }

    if (length >= MORA_CONFIG_COMMAND_CAPACITY) {
        mora_log(L"config: line %zu: the command is longer than %d characters", number,
            MORA_CONFIG_COMMAND_CAPACITY - 1);

        return;
    }

    mora_append_launch(config, command, length, workspace, monitor);
}

static void mora_parse_line(const wchar_t* begin, const wchar_t* end, size_t number,
    mora_config_t* config)
{
    /* A # at the start of a line or after whitespace begins a comment, so a #
     * inside a path or URL is kept. */
    for (const wchar_t* scan = begin; scan < end; ++scan) {
        if (*scan == L'#' && (scan == begin || scan[-1] == L' ' || scan[-1] == L'\t')) {
            end = scan;
            break;
        }
    }

    /* A stray CR from a CRLF file is not part of the line. */
    while (end > begin && (end[-1] == L' ' || end[-1] == L'\t' || end[-1] == L'\r')) {
        --end;
    }

    const wchar_t* at = begin;
    mora_token_t directive;

    if (!mora_take_token(&at, end, &directive)) {
        return;
    }

    if (mora_token_is(&directive, L"workspace")) {
        mora_parse_workspace(config, &at, end, number);
    } else if (mora_token_is(&directive, L"exec")) {
        mora_parse_exec(config, at, end, number);
    } else {
        mora_log(L"config: line %zu: unknown directive \"%.*ls\"", number,
            (int)directive.length, directive.start);
    }
}

void mora_config_parse(const wchar_t* text, mora_config_t* config)
{
    if (!text || !config) {
        return;
    }

    size_t number = 0;
    const wchar_t* at = text;

    while (*at) {
        ++number;
        const wchar_t* end = wcschr(at, L'\n');

        if (!end) {
            end = at + wcslen(at);
        }

        mora_parse_line(at, end, number, config);
        at = *end ? end + 1 : end;
    }
}

void mora_config_free(mora_config_t* config)
{
    if (!config) {
        return;
    }

    free(config->placements);
    free(config->launches);
    *config = (mora_config_t){ 0 };
}
