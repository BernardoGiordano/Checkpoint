#include "KeyboardManager.hpp"

std::u16string KeyboardManager::keyboard(const std::string& suggestion)
{
    swkbdSetInitialText(&mSwkbd, suggestion.c_str());
    char buf[CUSTOM_PATH_LEN] = {0};
    SwkbdButton button        = swkbdInputText(&mSwkbd, buf, CUSTOM_PATH_LEN);
    buf[CUSTOM_PATH_LEN - 1]  = '\0';
    return button == SWKBD_BUTTON_CONFIRM ? StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(buf)) : StringUtils::UTF8toUTF16("");
}

int KeyboardManager::numericPad(void)
{
    static SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 3);
    swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
    swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
    char buf[4]        = {0};
    SwkbdButton button = swkbdInputText(&swkbd, buf, sizeof(buf));
    buf[3]             = '\0';
    return button == SWKBD_BUTTON_CONFIRM ? atoi(buf) : -1;
}

KeyboardManager::KeyboardManager(void)
{
    swkbdInit(&mSwkbd, SWKBD_TYPE_NORMAL, 2, CUSTOM_PATH_LEN - 1);
    swkbdSetHintText(&mSwkbd, "Choose a name for your backup.");
}