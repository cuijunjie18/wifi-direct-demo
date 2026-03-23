#include "wifi_direct_manager.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Networking.h>

#include <iostream>
#include <chrono>
#include <algorithm>
#include <sstream>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::WiFiDirect;
using namespace winrt::Windows::Networking;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WiFiDirectManager::WiFiDirectManager() = default;

WiFiDirectManager::~WiFiDirectManager()
{
    cleanup();
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

void WiFiDirectManager::set_log_callback(LogCallback cb)
{
    m_log_cb = std::move(cb);
}

void WiFiDirectManager::log(const std::wstring& msg) const
{
    if (m_log_cb)
    {
        m_log_cb(msg);
    }
}

// ---------------------------------------------------------------------------
// Host – Advertise as Group Owner
// ---------------------------------------------------------------------------

void WiFiDirectManager::start_advertiser()
{
    try
    {
        if (!m_publisher)
        {
            m_publisher = WiFiDirectAdvertisementPublisher();
            m_publisher.Advertisement().ListenStateDiscoverability(
                WiFiDirectAdvertisementListenStateDiscoverability::Normal);
            m_publisher.Advertisement().IsAutonomousGroupOwnerEnabled(true);

            m_publisher_token = m_publisher.StatusChanged(
                [this](WiFiDirectAdvertisementPublisher const& /*sender*/,
                       WiFiDirectAdvertisementPublisherStatusChangedEventArgs const& args)
                {
                    std::wstringstream ss;
                    ss << L"Advertiser status: " << static_cast<int>(args.Status());
                    log(ss.str());
                });
        }

        if (!m_listener)
        {
            m_listener = WiFiDirectConnectionListener();
            m_listener_token = m_listener.ConnectionRequested(
                [this](WiFiDirectConnectionListener const& /*sender*/,
                       WiFiDirectConnectionRequestedEventArgs const& args)
                {
                    auto request = args.GetConnectionRequest();
                    auto info = request.DeviceInformation();
                    log(L"Incoming connection request from: " + std::wstring(info.Name()));

                    {
                        std::lock_guard<std::mutex> lk(m_incoming_mutex);
                        m_incoming_device_info = info;
                        m_incoming_ready.store(true);
                    }
                    m_incoming_cv.notify_one();
                });
        }

        m_publisher.Start();
        log(L"Host mode started. Advertising Wi-Fi Direct presence.");
    }
    catch (const winrt::hresult_error& ex)
    {
        log(L"start_advertiser failed: " + std::wstring(ex.message()));
    }
}

void WiFiDirectManager::stop_advertiser()
{
    try
    {
        if (m_publisher)
        {
            m_publisher.StatusChanged(m_publisher_token);
            m_publisher.Stop();
            m_publisher = nullptr;
        }
        if (m_listener)
        {
            m_listener.ConnectionRequested(m_listener_token);
            m_listener = nullptr;
        }
        log(L"Advertiser stopped.");
    }
    catch (const winrt::hresult_error& ex)
    {
        log(L"stop_advertiser failed: " + std::wstring(ex.message()));
    }
}

// ---------------------------------------------------------------------------
// Client – Discover nearby peers
// ---------------------------------------------------------------------------

void WiFiDirectManager::start_discovery()
{
    try
    {
        {
            std::lock_guard<std::mutex> lk(m_peers_mutex);
            m_peers.clear();
        }

        if (m_watcher)
        {
            m_watcher.Added(m_watcher_added_token);
            m_watcher.Updated(m_watcher_updated_token);
            m_watcher.Removed(m_watcher_removed_token);
            m_watcher.EnumerationCompleted(m_watcher_completed_token);
            m_watcher.Stop();
            m_watcher = nullptr;
        }

        auto selector = WiFiDirectDevice::GetDeviceSelector(
            WiFiDirectDeviceSelectorType::AssociationEndpoint);
        m_watcher = DeviceInformation::CreateWatcher(selector);

        m_watcher_added_token = m_watcher.Added(
            [this](DeviceWatcher const& /*sender*/, DeviceInformation const& info)
            {
                PeerInfo peer;
                auto name = info.Name();
                peer.display_name = name.empty() ? L"(unnamed)" : std::wstring(name);
                peer.device_id = std::wstring(info.Id());

                {
                    std::lock_guard<std::mutex> lk(m_peers_mutex);
                    m_peers.push_back(peer);
                }
                log(L"Discovered peer: " + peer.display_name);
            });

        m_watcher_updated_token = m_watcher.Updated(
            [this](DeviceWatcher const& /*sender*/, DeviceInformationUpdate const& info)
            {
                log(L"Peer updated: " + std::wstring(info.Id()));
            });

        m_watcher_removed_token = m_watcher.Removed(
            [this](DeviceWatcher const& /*sender*/, DeviceInformationUpdate const& info)
            {
                auto id = std::wstring(info.Id());
                {
                    std::lock_guard<std::mutex> lk(m_peers_mutex);
                    m_peers.erase(
                        std::remove_if(m_peers.begin(), m_peers.end(),
                                       [&id](const PeerInfo& p) { return p.device_id == id; }),
                        m_peers.end());
                }
                log(L"Peer removed: " + id);
            });

        m_watcher_completed_token = m_watcher.EnumerationCompleted(
            [this](DeviceWatcher const& /*sender*/, IInspectable const& /*args*/)
            {
                log(L"Peer discovery enumeration completed.");
            });

        m_watcher.Start();
        log(L"Peer discovery started.");
    }
    catch (const winrt::hresult_error& ex)
    {
        log(L"start_discovery failed: " + std::wstring(ex.message()));
    }
}

void WiFiDirectManager::stop_discovery()
{
    try
    {
        if (m_watcher)
        {
            m_watcher.Added(m_watcher_added_token);
            m_watcher.Updated(m_watcher_updated_token);
            m_watcher.Removed(m_watcher_removed_token);
            m_watcher.EnumerationCompleted(m_watcher_completed_token);
            m_watcher.Stop();
            m_watcher = nullptr;
        }
        log(L"Discovery stopped.");
    }
    catch (const winrt::hresult_error& ex)
    {
        log(L"stop_discovery failed: " + std::wstring(ex.message()));
    }
}

std::vector<PeerInfo> WiFiDirectManager::get_discovered_peers() const
{
    std::lock_guard<std::mutex> lk(m_peers_mutex);
    return m_peers;
}

// ---------------------------------------------------------------------------
// Connect to a peer (client side)
// ---------------------------------------------------------------------------

bool WiFiDirectManager::connect_to_peer(const std::wstring& device_id)
{
    try
    {
        log(L"Connecting to device: " + device_id);

        // Dispose previous device if any
        if (m_device)
        {
            m_device.ConnectionStatusChanged(m_device_status_token);
            m_device.Close();
            m_device = nullptr;
        }

        auto async_op = WiFiDirectDevice::FromIdAsync(device_id);
        m_device = async_op.get(); // blocking wait

        if (!m_device)
        {
            log(L"Connection failed: device is null.");
            return false;
        }

        m_device_status_token = m_device.ConnectionStatusChanged(
            [this](WiFiDirectDevice const& sender, IInspectable const& /*args*/)
            {
                std::wstringstream ss;
                ss << L"Connection status changed: " << static_cast<int>(sender.ConnectionStatus());
                log(ss.str());
            });

        auto pairs = m_device.GetConnectionEndpointPairs();
        if (pairs.Size() > 0)
        {
            auto ep = pairs.GetAt(0);
            auto local_ip  = ep.LocalHostName()  ? std::wstring(ep.LocalHostName().DisplayName())  : L"n/a";
            auto remote_ip = ep.RemoteHostName() ? std::wstring(ep.RemoteHostName().DisplayName()) : L"n/a";
            log(L"Connected. Local=" + local_ip + L", Remote=" + remote_ip);
        }
        else
        {
            log(L"Connected but no endpoint pairs returned.");
        }

        return true;
    }
    catch (const winrt::hresult_error& ex)
    {
        log(L"connect_to_peer failed: " + std::wstring(ex.message()));
        return false;
    }
}

// ---------------------------------------------------------------------------
// Wait for incoming connection (host side)
// ---------------------------------------------------------------------------

bool WiFiDirectManager::wait_for_incoming_connection(int timeout_seconds)
{
    try
    {
        log(L"Waiting for incoming connection...");

        std::unique_lock<std::mutex> lk(m_incoming_mutex);
        bool got_it = m_incoming_cv.wait_for(
            lk,
            std::chrono::seconds(timeout_seconds),
            [this]() { return m_incoming_ready.load(); });

        if (!got_it)
        {
            log(L"Timed out waiting for incoming connection.");
            return false;
        }

        m_incoming_ready.store(false);
        auto device_info = m_incoming_device_info;
        lk.unlock();

        log(L"Accepting connection from: " + std::wstring(device_info.Name()));

        // Dispose previous device if any
        if (m_device)
        {
            m_device.ConnectionStatusChanged(m_device_status_token);
            m_device.Close();
            m_device = nullptr;
        }

        auto async_op = WiFiDirectDevice::FromIdAsync(device_info.Id());
        m_device = async_op.get(); // blocking wait

        if (!m_device)
        {
            log(L"Failed to accept Wi-Fi Direct connection.");
            return false;
        }

        m_device_status_token = m_device.ConnectionStatusChanged(
            [this](WiFiDirectDevice const& sender, IInspectable const& /*args*/)
            {
                std::wstringstream ss;
                ss << L"Connection status changed: " << static_cast<int>(sender.ConnectionStatus());
                log(ss.str());
            });

        auto pairs = m_device.GetConnectionEndpointPairs();
        if (pairs.Size() > 0)
        {
            auto ep = pairs.GetAt(0);
            auto local_ip  = ep.LocalHostName()  ? std::wstring(ep.LocalHostName().DisplayName())  : L"n/a";
            auto remote_ip = ep.RemoteHostName() ? std::wstring(ep.RemoteHostName().DisplayName()) : L"n/a";
            log(L"Accepted. Local=" + local_ip + L", Remote=" + remote_ip);
        }
        else
        {
            log(L"Accepted but no endpoint pairs returned.");
        }

        return true;
    }
    catch (const winrt::hresult_error& ex)
    {
        log(L"wait_for_incoming_connection failed: " + std::wstring(ex.message()));
        return false;
    }
}

// ---------------------------------------------------------------------------
// Endpoint IP retrieval
// ---------------------------------------------------------------------------

std::pair<std::wstring, std::wstring> WiFiDirectManager::get_endpoint_ips() const
{
    if (!m_device)
    {
        return { L"", L"" };
    }

    try
    {
        auto pairs = m_device.GetConnectionEndpointPairs();
        if (pairs.Size() == 0)
        {
            return { L"", L"" };
        }

        auto ep = pairs.GetAt(0);
        auto local_ip  = ep.LocalHostName()  ? std::wstring(ep.LocalHostName().DisplayName())  : L"";
        auto remote_ip = ep.RemoteHostName() ? std::wstring(ep.RemoteHostName().DisplayName()) : L"";
        return { local_ip, remote_ip };
    }
    catch (const winrt::hresult_error&)
    {
        return { L"", L"" };
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void WiFiDirectManager::cleanup()
{
    stop_discovery();
    stop_advertiser();

    if (m_device)
    {
        m_device.ConnectionStatusChanged(m_device_status_token);
        m_device.Close();
        m_device = nullptr;
    }

    log(L"All resources cleaned up.");
}
