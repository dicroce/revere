
#include "r_utils/r_startup.h"
#include "r_utils/r_file.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include <algorithm>

#ifdef IS_WINDOWS
#include <windows.h>
#endif

#ifdef IS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

#ifdef IS_LINUX
#include <fstream>
#include <sys/stat.h>
#endif

using namespace r_utils;
using namespace std;

#ifdef IS_WINDOWS

bool r_utils::r_startup::set_autostart(bool enable, const string& app_name, const string& exe_path, const string& args)
{
    HKEY hKey;
    const char* keyPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    if (result != ERROR_SUCCESS)
    {
        R_LOG_ERROR("Failed to open registry key for autostart: %ld", result);
        return false;
    }

    bool success = false;
    if (enable)
    {
        // Build command with quotes around exe path and optional args
        string value = "\"" + exe_path + "\"";
        if (!args.empty())
            value += " " + args;

        result = RegSetValueExA(hKey, app_name.c_str(), 0, REG_SZ,
                                (BYTE*)value.c_str(), (DWORD)(value.length() + 1));
        if (result == ERROR_SUCCESS)
        {
            R_LOG_INFO("Added %s to Windows startup", app_name.c_str());
            success = true;
        }
        else
        {
            R_LOG_ERROR("Failed to add to startup: %ld", result);
        }
    }
    else
    {
        result = RegDeleteValueA(hKey, app_name.c_str());
        if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND)
        {
            R_LOG_INFO("Removed %s from Windows startup", app_name.c_str());
            success = true;
        }
        else
        {
            R_LOG_ERROR("Failed to remove from startup: %ld", result);
        }
    }

    RegCloseKey(hKey);
    return success;
}

bool r_utils::r_startup::is_autostart_enabled(const string& app_name)
{
    HKEY hKey;
    const char* keyPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE, &hKey);
    if (result != ERROR_SUCCESS)
        return false;

    char value[MAX_PATH * 2];
    DWORD value_size = sizeof(value);
    result = RegQueryValueExA(hKey, app_name.c_str(), NULL, NULL, (LPBYTE)value, &value_size);

    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

#endif // IS_WINDOWS

#ifdef IS_MACOS

bool r_utils::r_startup::set_autostart(bool enable, const string& app_name, const string& exe_path, const string& args)
{
    // Get the main bundle URL
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (!mainBundle)
    {
        R_LOG_ERROR("Failed to get main bundle for autostart");
        return false;
    }

    CFURLRef bundleURL = CFBundleCopyBundleURL(mainBundle);
    if (!bundleURL)
    {
        R_LOG_ERROR("Failed to get bundle URL for autostart");
        return false;
    }

    LSSharedFileListRef loginItems = LSSharedFileListCreate(NULL,
        kLSSharedFileListSessionLoginItems, NULL);

    if (!loginItems)
    {
        CFRelease(bundleURL);
        R_LOG_ERROR("Failed to access login items");
        return false;
    }

    bool success = false;

    if (enable)
    {
        // Add to login items
        LSSharedFileListItemRef item = LSSharedFileListInsertItemURL(
            loginItems, kLSSharedFileListItemLast, NULL, NULL,
            bundleURL, NULL, NULL);

        if (item)
        {
            R_LOG_INFO("Added %s to macOS login items", app_name.c_str());
            CFRelease(item);
            success = true;
        }
        else
        {
            R_LOG_ERROR("Failed to add to login items");
        }
    }
    else
    {
        // Remove from login items - find matching entry
        UInt32 seedValue;
        CFArrayRef loginItemsArray = LSSharedFileListCopySnapshot(loginItems, &seedValue);

        if (loginItemsArray)
        {
            for (CFIndex i = 0; i < CFArrayGetCount(loginItemsArray); i++)
            {
                LSSharedFileListItemRef item = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(loginItemsArray, i);
                CFURLRef itemURL = NULL;

                if (LSSharedFileListItemResolve(item, 0, &itemURL, NULL) == noErr)
                {
                    if (itemURL && CFEqual(itemURL, bundleURL))
                    {
                        LSSharedFileListItemRemove(loginItems, item);
                        R_LOG_INFO("Removed %s from macOS login items", app_name.c_str());
                        success = true;
                        CFRelease(itemURL);
                        break;
                    }
                    if (itemURL)
                        CFRelease(itemURL);
                }
            }
            CFRelease(loginItemsArray);
        }
    }

    CFRelease(loginItems);
    CFRelease(bundleURL);
    return success;
}

bool r_utils::r_startup::is_autostart_enabled(const string& app_name)
{
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (!mainBundle)
        return false;

    CFURLRef bundleURL = CFBundleCopyBundleURL(mainBundle);
    if (!bundleURL)
        return false;

    LSSharedFileListRef loginItems = LSSharedFileListCreate(NULL,
        kLSSharedFileListSessionLoginItems, NULL);

    if (!loginItems)
    {
        CFRelease(bundleURL);
        return false;
    }

    bool found = false;
    UInt32 seedValue;
    CFArrayRef loginItemsArray = LSSharedFileListCopySnapshot(loginItems, &seedValue);

    if (loginItemsArray)
    {
        for (CFIndex i = 0; i < CFArrayGetCount(loginItemsArray); i++)
        {
            LSSharedFileListItemRef item = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(loginItemsArray, i);
            CFURLRef itemURL = NULL;

            if (LSSharedFileListItemResolve(item, 0, &itemURL, NULL) == noErr)
            {
                if (itemURL && CFEqual(itemURL, bundleURL))
                {
                    found = true;
                    CFRelease(itemURL);
                    break;
                }
                if (itemURL)
                    CFRelease(itemURL);
            }
        }
        CFRelease(loginItemsArray);
    }

    CFRelease(loginItems);
    CFRelease(bundleURL);
    return found;
}

#endif // IS_MACOS

#ifdef IS_LINUX

bool r_utils::r_startup::set_autostart(bool enable, const string& app_name, const string& exe_path, const string& args)
{
    const char* home = getenv("HOME");
    if (!home)
    {
        R_LOG_ERROR("Failed to get HOME directory for autostart");
        return false;
    }

    string autostart_dir = string(home) + "/.config/autostart";
    string desktop_file = autostart_dir + "/" + app_name + ".desktop";

    // Check if running in snap or flatpak
    const char* snap = getenv("SNAP");
    const char* flatpak_id = getenv("FLATPAK_ID");

    if (enable)
    {
        // Create autostart directory if it doesn't exist
        if (!r_fs::file_exists(autostart_dir))
        {
            if (!r_fs::create_directory(autostart_dir))
            {
                R_LOG_ERROR("Failed to create autostart directory: %s", autostart_dir.c_str());
                return false;
            }
        }

        string exec_line;
        string icon_name;

        if (snap != nullptr)
        {
            // In snap, use the snap command name, not the full path
            // Snap command format: <snap-name>
            // The snap name is typically lowercase, so "revere" not "Revere"
            string snap_name = app_name;
            std::transform(snap_name.begin(), snap_name.end(), snap_name.begin(), ::tolower);

            exec_line = snap_name;
            if (!args.empty())
                exec_line += " " + args;

            // Snap desktop files use qualified icon names
            icon_name = snap_name + "_" + snap_name;

            R_LOG_INFO("Configuring autostart for snap installation");
        }
        else if (flatpak_id != nullptr)
        {
            // Flatpak uses reverse-DNS app ID (e.g., io.github.dicroce.Revere)
            exec_line = string(flatpak_id);
            if (!args.empty())
                exec_line += " " + args;

            icon_name = flatpak_id;

            R_LOG_INFO("Configuring autostart for flatpak installation");
        }
        else
        {
            // Native installation - use full executable path
            exec_line = exe_path;
            if (!args.empty())
                exec_line += " " + args;

            icon_name = "revere";

            R_LOG_INFO("Configuring autostart for native installation");
        }

        // Create .desktop file
        string content =
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=" + app_name + "\n"
            "Exec=" + exec_line + "\n"
            "Icon=" + icon_name + "\n"
            "Comment=Revere Video Surveillance System\n"
            "X-GNOME-Autostart-enabled=true\n"
            "StartupNotify=false\n"
            "Terminal=false\n";

        ofstream file(desktop_file);
        if (!file.is_open())
        {
            R_LOG_ERROR("Failed to create desktop file: %s", desktop_file.c_str());
            return false;
        }

        file << content;
        file.close();

        // Make executable
        chmod(desktop_file.c_str(), 0755);

        R_LOG_INFO("Added %s to Linux autostart (snap: %s, flatpak: %s, native: %s)",
                   app_name.c_str(),
                   snap ? "yes" : "no",
                   flatpak_id ? "yes" : "no",
                   (!snap && !flatpak_id) ? "yes" : "no");
        return true;
    }
    else
    {
        // Remove desktop file - same for all packaging formats
        if (r_fs::file_exists(desktop_file))
        {
            if (r_fs::remove_file(desktop_file))
            {
                R_LOG_INFO("Removed %s from Linux autostart", app_name.c_str());
                return true;
            }
            else
            {
                R_LOG_ERROR("Failed to remove desktop file: %s", desktop_file.c_str());
                return false;
            }
        }
        return true; // Already not in autostart
    }
}

bool r_utils::r_startup::is_autostart_enabled(const string& app_name)
{
    const char* home = getenv("HOME");
    if (!home)
        return false;

    string desktop_file = string(home) + "/.config/autostart/" + app_name + ".desktop";
    return r_fs::file_exists(desktop_file);
}

#endif // IS_LINUX
