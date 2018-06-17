#include "KeyboardManager.hpp"

std::u16string KeyboardManager::keyboard(const std::string& suggestion)
{
    swkbdSetInitialText(&mSwkbd, suggestion.c_str()); 
    char buf[CUSTOM_PATH_LEN] = {0};
    SwkbdButton button = swkbdInputText(&mSwkbd, buf, CUSTOM_PATH_LEN);
    buf[CUSTOM_PATH_LEN - 1] = '\0';
    return button == SWKBD_BUTTON_CONFIRM ? StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(buf)) : StringUtils::UTF8toUTF16(" ");
}

KeyboardManager::KeyboardManager(void)
{
    swkbdInit(&mSwkbd, SWKBD_TYPE_NORMAL, 2, CUSTOM_PATH_LEN - 1); 
    swkbdSetHintText(&mSwkbd, "Choose a name for your backup.");   
}