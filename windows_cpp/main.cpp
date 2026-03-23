/// Wi-Fi Direct CLI Demo (C++/WinRT)
/// ===================================
/// Link-layer operations:
///   1. Discover nearby Wi-Fi Direct devices
///   2. Connect / join group
///   3. Obtain in-group reachable IP addresses
///
/// Usage:
///   wifi_direct_cli.exe host      -- start as Group Owner, wait for connections
///   wifi_direct_cli.exe discover  -- discover peers, pick one and connect

#include "wifi_direct_manager.h"

#include <winrt/Windows.Foundation.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <locale>
#include <codecvt>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void print_usage()
{
    std::wcout << L"Usage:\n"
               << L"  wifi_direct_cli.exe host      Start as Group Owner (advertise & wait)\n"
               << L"  wifi_direct_cli.exe discover   Discover peers, select one, and connect\n";
}

static void log_handler(const std::wstring& msg)
{
    std::wcout << L"[LOG] " << msg << std::endl;
}

// ---------------------------------------------------------------------------
// Host mode – advertise, accept connection, print IPs
// ---------------------------------------------------------------------------

static int run_host()
{
    WiFiDirectManager mgr;
    mgr.set_log_callback(log_handler);

    std::wcout << L"=== Wi-Fi Direct Host (Group Owner) ===" << std::endl;

    mgr.start_advertiser();

    std::wcout << L"\nWaiting for an incoming connection (timeout: 120s) ...\n" << std::endl;

    if (!mgr.wait_for_incoming_connection(120))
    {
        std::wcout << L"No incoming connection received. Exiting." << std::endl;
        mgr.cleanup();
        return 1;
    }

    auto [local_ip, remote_ip] = mgr.get_endpoint_ips();
    std::wcout << L"\n=============================" << std::endl;
    std::wcout << L"  Connection established!" << std::endl;
    std::wcout << L"  Local  IP: " << (local_ip.empty()  ? L"(unknown)" : local_ip)  << std::endl;
    std::wcout << L"  Remote IP: " << (remote_ip.empty() ? L"(unknown)" : remote_ip) << std::endl;
    std::wcout << L"=============================" << std::endl;

    std::wcout << L"\nPress Enter to disconnect and exit..." << std::endl;
    std::wstring dummy;
    std::getline(std::wcin, dummy);

    mgr.cleanup();
    return 0;
}

// ---------------------------------------------------------------------------
// Discover mode – scan, list, pick, connect, print IPs
// ---------------------------------------------------------------------------

static int run_discover()
{
    WiFiDirectManager mgr;
    mgr.set_log_callback(log_handler);

    std::wcout << L"=== Wi-Fi Direct Discovery (Client) ===" << std::endl;

    mgr.start_discovery();

    std::wcout << L"\nScanning for peers (10 seconds) ..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    mgr.stop_discovery();

    auto peers = mgr.get_discovered_peers();
    if (peers.empty())
    {
        std::wcout << L"No peers found. Exiting." << std::endl;
        mgr.cleanup();
        return 1;
    }

    std::wcout << L"\nDiscovered peers:" << std::endl;
    for (size_t i = 0; i < peers.size(); ++i)
    {
        std::wcout << L"  [" << i << L"] " << peers[i].display_name
                   << L"  (ID: " << peers[i].device_id << L")" << std::endl;
    }

    std::wcout << L"\nEnter peer index to connect (or 'q' to quit): ";
    std::wstring input;
    std::getline(std::wcin, input);

    if (input == L"q" || input == L"Q")
    {
        mgr.cleanup();
        return 0;
    }

    size_t index = 0;
    try
    {
        index = std::stoul(input);
    }
    catch (...)
    {
        std::wcout << L"Invalid input." << std::endl;
        mgr.cleanup();
        return 1;
    }

    if (index >= peers.size())
    {
        std::wcout << L"Index out of range." << std::endl;
        mgr.cleanup();
        return 1;
    }

    std::wcout << L"\nConnecting to: " << peers[index].display_name << L" ..." << std::endl;

    if (!mgr.connect_to_peer(peers[index].device_id))
    {
        std::wcout << L"Connection failed. Exiting." << std::endl;
        mgr.cleanup();
        return 1;
    }

    auto [local_ip, remote_ip] = mgr.get_endpoint_ips();
    std::wcout << L"\n=============================" << std::endl;
    std::wcout << L"  Connection established!" << std::endl;
    std::wcout << L"  Local  IP: " << (local_ip.empty()  ? L"(unknown)" : local_ip)  << std::endl;
    std::wcout << L"  Remote IP: " << (remote_ip.empty() ? L"(unknown)" : remote_ip) << std::endl;
    std::wcout << L"=============================" << std::endl;

    std::wcout << L"\nPress Enter to disconnect and exit..." << std::endl;
    std::wstring dummy;
    std::getline(std::wcin, dummy);

    mgr.cleanup();
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int wmain(int argc, wchar_t* argv[])
{
    winrt::init_apartment();

    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    std::wstring mode(argv[1]);

    int result = 1;
    if (mode == L"host")
    {
        result = run_host();
    }
    else if (mode == L"discover")
    {
        result = run_discover();
    }
    else
    {
        std::wcout << L"Unknown mode: " << mode << std::endl;
        print_usage();
    }

    winrt::uninit_apartment();
    return result;
}
