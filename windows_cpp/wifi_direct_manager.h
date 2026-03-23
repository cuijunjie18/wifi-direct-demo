#pragma once

// C++/WinRT headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

namespace wfd = winrt::Windows::Devices::WiFiDirect;
namespace wde = winrt::Windows::Devices::Enumeration;
namespace wn  = winrt::Windows::Networking;

/// Represents a discovered Wi-Fi Direct peer.
struct PeerInfo
{
    std::wstring display_name;
    std::wstring device_id;
};

/// Wi-Fi Direct link-layer manager (CLI version).
/// Provides: discover nearby devices, connect / join group, obtain in-group IP.
class WiFiDirectManager
{
public:
    WiFiDirectManager();
    ~WiFiDirectManager();

    // ---- Host (Group Owner) APIs ----

    /// Start advertising as a Wi-Fi Direct Group Owner.
    void start_advertiser();

    /// Stop advertising.
    void stop_advertiser();

    // ---- Client (Discoverer) APIs ----

    /// Start discovering nearby Wi-Fi Direct peers.
    void start_discovery();

    /// Stop the current discovery session.
    void stop_discovery();

    /// Return a snapshot of the currently discovered peers.
    std::vector<PeerInfo> get_discovered_peers() const;

    // ---- Connection ----

    /// Connect to a peer by its device ID (blocking coroutine wrapper).
    /// Returns true on success.
    bool connect_to_peer(const std::wstring& device_id);

    /// Accept the next incoming connection request (blocks until one arrives or timeout).
    /// Returns true on success.
    bool wait_for_incoming_connection(int timeout_seconds = 60);

    // ---- IP Information ----

    /// After a connection is established, retrieve local and remote IP addresses.
    /// Returns {local_ip, remote_ip}. Empty strings if unavailable.
    std::pair<std::wstring, std::wstring> get_endpoint_ips() const;

    // ---- Cleanup ----

    /// Tear down all resources.
    void cleanup();

    // ---- Logging ----

    using LogCallback = std::function<void(const std::wstring&)>;
    void set_log_callback(LogCallback cb);

private:
    void log(const std::wstring& msg) const;

    // Advertiser
    wfd::WiFiDirectAdvertisementPublisher  m_publisher{ nullptr };
    wfd::WiFiDirectConnectionListener      m_listener{ nullptr };
    winrt::event_token m_publisher_token{};
    winrt::event_token m_listener_token{};

    // Discovery
    wde::DeviceWatcher m_watcher{ nullptr };
    winrt::event_token m_watcher_added_token{};
    winrt::event_token m_watcher_updated_token{};
    winrt::event_token m_watcher_removed_token{};
    winrt::event_token m_watcher_completed_token{};

    mutable std::mutex m_peers_mutex;
    std::vector<PeerInfo> m_peers;

    // Connection
    wfd::WiFiDirectDevice m_device{ nullptr };
    winrt::event_token m_device_status_token{};

    // Incoming connection event
    mutable std::mutex m_incoming_mutex;
    std::condition_variable m_incoming_cv;
    std::atomic<bool> m_incoming_ready{ false };
    wde::DeviceInformation m_incoming_device_info{ nullptr };

    // Logging
    LogCallback m_log_cb;
};
