// LatestBrowsingHistory - get recent browsing history for Edge and Chrome
// by Michael Haephrati
// https://github.com/securedglobe/browsinghistory

#include <windows.h>
#include <string>
#include <iostream>
#include "sqlite3.h"
#include <chrono>
#include <ctime>
#include <shlobj.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

// Convert WebKit timestamp (microseconds) to Unix timestamp (seconds)
time_t ConvertWebKitToUnixTime(int64_t webkitTime)
{
    return static_cast<time_t>(webkitTime / 1000000 - 11644473600LL); // Adjusting for WebKit epoch
}

// Function to convert a UTF-8 string to a wide string using the Windows API
std::wstring ConvertUtf8ToWide(const std::string& utf8Str)
{
    if (utf8Str.empty())
    {
        return std::wstring();
    }

    // Determine the required buffer size
    int wideCharCount = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wideCharCount == 0)
    {
        return std::wstring();
    }

    // Convert the UTF-8 string to a wide string
    std::wstring wideStr(wideCharCount, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wideStr[0], wideCharCount);

    return wideStr;
}

// Get the current user's profile path (e.g., C:\Users\<username>\)
std::wstring GetUserProfilePath()
{
    WCHAR path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path)))
    {
        return std::wstring(path);
    }
    return L"";
}

// Convert time_t to human-readable UTC time string
std::wstring FormatUnixTimeToUTC(time_t unixTime)
{
    struct tm timeInfo;
    if (gmtime_s(&timeInfo, &unixTime) != 0) // Safe version of gmtime
    {
        return L"Invalid time";
    }

    wchar_t buffer[80];
    wcsftime(buffer, sizeof(buffer), L"%Y-%m-%d %H:%M:%S", &timeInfo); // Format time
    return std::wstring(buffer);
}

// Function to copy the locked database to a temporary file for querying
bool CopyDatabaseToTemp(const std::wstring& dbPath, std::wstring& tempDbPath)
{
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0)
    {
        return false;
    }

    wchar_t tempFileName[MAX_PATH];
    if (GetTempFileNameW(tempPath, L"dbcopy", 0, tempFileName) == 0)
    {
        return false;
    }

    tempDbPath = std::wstring(tempFileName);

    try
    {
        std::filesystem::copy_file(dbPath, tempDbPath, std::filesystem::copy_options::overwrite_existing);
        return true;
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        wprintf(L"Failed to copy database: %s\n", ConvertUtf8ToWide(e.what()).c_str());
        return false;
    }
}

// Function to read browsing history from a given database path
void PrintUrlsFromDatabase(const std::wstring& dbPath, const time_t currentTime, const time_t timeRangeInSeconds)
{
    std::wstring tempDbPath;
    if (!CopyDatabaseToTemp(dbPath, tempDbPath))
    {
        wprintf(L"Failed to copy database to temporary file: %s\n", dbPath.c_str());
        return;
    }

    sqlite3* db;
    if (sqlite3_open16(tempDbPath.c_str(), &db) != SQLITE_OK)
    {
        wprintf(L"Failed to open database: %s\n", tempDbPath.c_str());
        return;
    }

    // Query to get URLs and visit times
    const char* query = "SELECT u.url, v.visit_time FROM urls u JOIN visits v ON u.id = v.url ORDER BY v.visit_time DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
    {
        wprintf(L"Failed to prepare statement: %S\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    // Execute the query and process the results
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* foundUrlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int64_t visitTimeWebKit = sqlite3_column_int64(stmt, 1);
        time_t visitTimeUnix = ConvertWebKitToUnixTime(visitTimeWebKit);

        // Check if the URL was visited within the last 10 minutes
        if (difftime(currentTime, visitTimeUnix) <= timeRangeInSeconds)
        {
            // Convert the URL from UTF-8 to wide string using the Windows API function
            std::wstring foundUrl = ConvertUtf8ToWide(foundUrlUtf8);

            // Format the visit time to a human-readable UTC string
            std::wstring visitTimeStr = FormatUnixTimeToUTC(visitTimeUnix);

            wprintf(L"URL: %s, Visit Time (UTC): %s\n", foundUrl.c_str(), visitTimeStr.c_str());
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // Remove the temporary file after use
    std::filesystem::remove(tempDbPath);
}

// Main function to check browsing history from Chrome and Edge
int wmain()
{
    // Get the current time
    time_t currentTime = std::time(nullptr);

    // Define the time range (10 minutes in seconds)
    time_t timeRangeInSeconds = 10 * 60;

    // Get the user profile path
    std::wstring userProfilePath = GetUserProfilePath();
    if (userProfilePath.empty())
    {
        wprintf(L"Failed to get user profile path.\n");
        return 1;
    }

    // Chrome history path
    std::wstring chromeHistoryPath = userProfilePath + L"\\AppData\\Local\\Google\\Chrome\\User Data\\Default\\History";

    // Edge history path
    std::wstring edgeHistoryPath = userProfilePath + L"\\AppData\\Local\\Microsoft\\Edge\\User Data\\Default\\History";

    wprintf(L"Checking Chrome browsing history:\n");
    PrintUrlsFromDatabase(chromeHistoryPath, currentTime, timeRangeInSeconds);

    wprintf(L"\nChecking Edge browsing history:\n");
    PrintUrlsFromDatabase(edgeHistoryPath, currentTime, timeRangeInSeconds);

    return 0;
}
