
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

// One argument of a Desktop Entry Exec key: quoted when it holds a reserved
// character, with ", `, $ and \ escaped inside the quotes, then \ escaped again
// for the key's string value (Desktop Entry Specification, "The Exec key").
static string _desktop_exec_arg(const string& arg)
{
    static const string reserved = " \t\n\"'\\><~|&;$*?#()`";
    if(!arg.empty() && arg.find_first_of(reserved) == string::npos)
        return arg;
    string quoted = "\"";
    for(char c : arg)
    {
        if(c == '"' || c == '`' || c == '$' || c == '\\')
            quoted += '\\';
        quoted += c;
    }
    quoted += "\"";
    string escaped;
    for(char c : quoted)
    {
        if(c == '\\')
            escaped += '\\';
        escaped += c;
    }
    return escaped;
}

string r_utils::r_startup::linux_autostart_exec(const linux_launch_env& env, const string& exe_path, const string& args)
{
    string command;
    if(!env.snap.empty())
        command = "/snap/bin/" + (env.snap_name.empty() ? string("revere") : env.snap_name);
    else if(!env.flatpak_id.empty())
        command = env.flatpak_id;
    else if(!env.appimage.empty())
        command = _desktop_exec_arg(env.appimage);
    else command = _desktop_exec_arg(exe_path);
    if(!args.empty())
        command += " " + args;
    return command;
}

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
    const char* snap = getenv("SNAP");
    const char* flatpak_id = getenv("FLATPAK_ID");

    string autostart_dir;
    string desktop_file;

    if (snap != nullptr)
    {
        // Snap: write to $SNAP_USER_DATA/.config/autostart/ so snap-userd-autostart
        // can find it. The filename must match the autostart: declaration in snapcraft.yaml.
        const char* snap_user_data = getenv("SNAP_USER_DATA");
        if (!snap_user_data)
        {
            R_LOG_ERROR("SNAP_USER_DATA not set");
            return false;
        }
        autostart_dir = string(snap_user_data) + "/.config/autostart";
        desktop_file = autostart_dir + "/revere-autostart.desktop";
    }
    else
    {
        const char* home = getenv("HOME");
        if (!home)
        {
            R_LOG_ERROR("Failed to get HOME directory for autostart");
            return false;
        }
        autostart_dir = string(home) + "/.config/autostart";
        desktop_file = autostart_dir + "/" + app_name + ".desktop";
    }

    if (enable)
    {
        // Create autostart directory if it doesn't exist
        if (!r_fs::file_exists(autostart_dir))
        {
            try
            {
                r_fs::mkdir_p(autostart_dir);
            }
            catch (const exception& e)
            {
                R_LOG_ERROR("Failed to create autostart directory: %s", autostart_dir.c_str());
                return false;
            }
        }

        const auto env_or_empty = [](const char* name) { const char* v = getenv(name); return v ? string(v) : string(); };
        r_startup::linux_launch_env env;
        env.snap = env_or_empty("SNAP");
        env.snap_name = env_or_empty("SNAP_NAME");
        env.flatpak_id = env_or_empty("FLATPAK_ID");
        env.appimage = env_or_empty("APPIMAGE");
        string exec_line = r_startup::linux_autostart_exec(env, exe_path, args);

        string icon_name = "revere";
        if (snap != nullptr)
            icon_name = "snap.revere.revere";
        else if (flatpak_id != nullptr)
            icon_name = flatpak_id;

        R_LOG_INFO("Configuring autostart (%s): Exec=%s", snap ? "snap" : flatpak_id ? "flatpak" : !env.appimage.empty() ? "AppImage" : "native",
                   exec_line.c_str());

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

        R_LOG_INFO("Added %s to Linux autostart (flatpak: %s, native: %s)",
                   app_name.c_str(),
                   flatpak_id ? "yes" : "no",
                   !flatpak_id ? "yes" : "no");
        return true;
    }
    else
    {
        // Remove desktop file - same for flatpak and native
        if (r_fs::file_exists(desktop_file))
        {
            try
            {
                r_fs::remove_file(desktop_file);
                R_LOG_INFO("Removed %s from Linux autostart", app_name.c_str());
                return true;
            }
            catch (const exception& e)
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
    const char* snap = getenv("SNAP");
    if (snap != nullptr)
    {
        const char* snap_user_data = getenv("SNAP_USER_DATA");
        if (!snap_user_data)
            return false;
        string desktop_file = string(snap_user_data) + "/.config/autostart/revere-autostart.desktop";
        return r_fs::file_exists(desktop_file);
    }

    const char* home = getenv("HOME");
    if (!home)
        return false;

    string desktop_file = string(home) + "/.config/autostart/" + app_name + ".desktop";
    return r_fs::file_exists(desktop_file);
}

#endif // IS_LINUX
