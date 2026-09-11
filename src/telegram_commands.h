#ifndef NORELECBOT_TELEGRAM_COMMANDS_H
#define NORELECBOT_TELEGRAM_COMMANDS_H

#include "arena.h"
#include "config.h"
#include "dynamic_string.h"
#include "storage.h"

#define TELEGRAM_CONQUISTER_PLACE "@TheConquister37"
#define TELEGRAM_CONQUISTER_TRIGGER "We " TELEGRAM_CONQUISTER_PLACE

typedef enum {
    TELEGRAM_COMMAND_IGNORED,
    TELEGRAM_COMMAND_REPLIED,
    TELEGRAM_COMMAND_ERROR
} TelegramCommandResult;

typedef struct {
    Storage *storage;
    Arena *arena;
    const AppConfig *config;
    int64_t user_id;
    const char *username;
} TelegramCommandContext;

/* Ignored messages produce no reply; recognized messages always do. */
TelegramCommandResult telegram_command_dispatch(
    const TelegramCommandContext *context,
    const char *text,
    DynamicString *reply
);

#endif
