#include "KeyboardManager.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
    struct NumpadRange {
        int min;
        int max;
    };

    // swkbd filter callback: reject anything outside [min, max] and keep the
    // keyboard open with an explanatory message instead of accepting the input.
    SwkbdCallbackResult numpadRangeFilter(void* user, const char** ppMessage, const char* text, size_t textlen)
    {
        (void)textlen;
        const NumpadRange* range = (const NumpadRange*)user;
        char* end                = nullptr;
        const long value         = strtol(text, &end, 10);
        if (end == text || *end != '\0' || value < range->min || value > range->max) {
            static char message[64];
            snprintf(message, sizeof(message), "Enter a value between %d and %d.", range->min, range->max);
            *ppMessage = message;
            return SWKBD_CALLBACK_CONTINUE;
        }
        return SWKBD_CALLBACK_OK;
    }
}

std::u16string KeyboardManager::keyboard(const std::string& suggestion)
{
    swkbdSetInitialText(&mSwkbd, suggestion.c_str());
    char buf[CUSTOM_PATH_LEN] = {0};
    SwkbdButton button        = swkbdInputText(&mSwkbd, buf, CUSTOM_PATH_LEN);
    buf[CUSTOM_PATH_LEN - 1]  = '\0';
    return button == SWKBD_BUTTON_CONFIRM ? StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(buf)) : StringUtils::UTF8toUTF16("");
}

std::string KeyboardManager::text(const std::string& suggestion, const std::string& hint, size_t maxLen)
{
    SwkbdState swkbd;
    const size_t kBufSize = 64;
    size_t limit          = std::min(maxLen, kBufSize);
    if (limit < 2) {
        limit = 2;
    }
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, limit - 1);
    swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
    swkbdSetInitialText(&swkbd, suggestion.c_str());
    swkbdSetHintText(&swkbd, hint.c_str());
    char buf[kBufSize]   = {0};
    SwkbdButton button   = swkbdInputText(&swkbd, buf, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    return button == SWKBD_BUTTON_CONFIRM ? std::string(buf) : std::string();
}

int KeyboardManager::numpad(const std::string& hint, int min, int max)
{
    // Cap the digit count to the widest value the range can produce.
    int digits = 1;
    for (int m = max; m >= 10; m /= 10) {
        digits++;
    }

    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, digits);
    swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
    swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
    swkbdSetHintText(&swkbd, hint.c_str());
    NumpadRange range = {min, max};
    swkbdSetFilterCallback(&swkbd, numpadRangeFilter, &range);

    char buf[16]       = {0};
    SwkbdButton button = swkbdInputText(&swkbd, buf, sizeof(buf));
    return button == SWKBD_BUTTON_CONFIRM ? (int)strtol(buf, nullptr, 10) : -1;
}

KeyboardManager::KeyboardManager(void)
{
    swkbdInit(&mSwkbd, SWKBD_TYPE_NORMAL, 2, CUSTOM_PATH_LEN - 1);
    swkbdSetValidation(&mSwkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
    swkbdSetHintText(&mSwkbd, "Choose a name for your backup.");
}