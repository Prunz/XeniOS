#ifndef XENIA_KERNEL_UPNP_H_
#define XENIA_KERNEL_UPNP_H_

#include <cstdint>
#include <map>
#include <string>

#if !XE_PLATFORM_IOS

#include <future>
#include <optional>
#include <set>
#include <shared_mutex>
#include <third_party/miniupnp/miniupnpc/include/miniupnpc.h>
#include "xenia/base/threading.h"

using namespace std::chrono_literals;

#endif  // !XE_PLATFORM_IOS

namespace xe {
namespace kernel {

#if XE_PLATFORM_IOS

// iOS stub — UPnP is not supported on iOS.
// All port mapping calls pass through unchanged.
class UPnP {
 public:
  UPnP() = default;
  ~UPnP() = default;

  bool IsActive() const { return false; }
  bool IsVariableLeaseSupported() const { return false; }
  static void SetUPnPState(bool) {}
  void Initialize() {}
  void Start() {}
  std::string GetLocalIP() { return ""; }

  uint16_t GetMappedConnectPort(uint16_t port) { return port; }
  uint16_t GetMappedBindPort(uint16_t port) { return port; }

  void AddMappedConnectPort(uint16_t port, uint16_t mapped_port) {}
  void AddMappedBindPort(uint16_t port, uint16_t mapped_port) {}
};

#else

class UPnP {
 public:
  enum class UPnPErrorCodes : int32_t {
    Success = 0,
    HttpUnauthorized = 401,
    ActionNotAuthorized = 606,
    InactiveConnectionStateRequired = 703,
    ConnectionSetupFailed = 704,
    ConnectionSetupInProgress = 705,
    ConnectionNotConfigured = 706,
    DisconnectInProgress = 707,
    InvalidLayer2Address = 708,
    InternetAccessDisabled = 709,
    InvalidConnectionType = 710,
    ConnectionAlreadyTerminated = 711,
    SpecifiedArrayIndexInvalid = 713,
    NoSuchEntryInArray = 714,
    WildcardNotPermittedInSourceIP = 715,
    WildcardNotPermittedInExternalPort = 716,
    ConflictInMappingEntry = 718,
    SamePortValuesRequired = 724,
    OnlyPermanentLeasesSupported = 725,
    RemoteHostOnlySupportsRawTcp = 726,
    ExternalPortOnlySupportsWildcard = 727,
    NoPortMappingsAvailable = 728,
    ConflictWithOtherMechanisms = 729,
    PortMappingNotFound = 730,
    InconsistentParameters = 733
  };

  UPnP();
  ~UPnP();

  bool IsActive() const { return active_; }
  bool IsVariableLeaseSupported() const { return leases_supported_; }
  static void SetUPnPState(bool upnp_state);
  void Initialize();
  void Start();
  std::optional<std::string> GetValidIGD();
  std::optional<std::string> DiscoverValidIGD();
  UPNPDev* DiscoverUPnPDevices();
  bool LoadIGD(std::string igd_root);
  std::future<int32_t> AddPortAsync(std::string addr, uint16_t internal_port,
                                    std::string protocol);
  int32_t AddPort(std::string addr, uint16_t internal_port,
                  std::string protocol);
  std::future<int32_t> RemovePortAsync(uint16_t port, std::string protocol);
  int32_t RemovePort(uint16_t internal_port, std::string protocol);
  std::string GetLocalIP();
  static std::string GetLocalIP_wget();
  void TrackPort(uint16_t port, std::string protocol);
  void OpenTrackedPorts();
  void OpenPorts(std::map<std::string, std::map<uint16_t, uint16_t>> open_ports);
  void CloseOpenPorts();
  void RefreshPorts();
  uint16_t GetMappedConnectPort(uint16_t external_port);
  uint16_t GetMappedBindPort(uint16_t external_port);
  const std::map<std::string, std::map<uint16_t, uint16_t>> GetOpenedPorts();
  const std::map<std::string, std::map<uint16_t, int32_t>> GetPortBindingResults();
  const std::map<std::string, std::set<uint16_t>> GetTrackedPorts();
  static std::string_view GetMiniUPnPcErrorCodeToDesc(int32_t error) noexcept;
  static std::string_view GetUPnPErrorCodeToDesc(int32_t error) noexcept;
  static std::string_view GetUPnPErrorCodeToDesc(UPnPErrorCodes error) noexcept;
  void AddMappedConnectPort(uint16_t port, uint16_t mapped_port) {
    mapped_connect_ports_.insert({port, mapped_port});
  }
  void AddMappedBindPort(uint16_t port, uint16_t mapped_port) {
    mapped_bind_ports_.insert({port, mapped_port});
  }

 private:
  void CleanupIGD();
  void StartPeriodicPortsRefresher();

  std::future<std::optional<std::string>> get_valid_IGD_;
  std::atomic<bool> active_ = false;
  std::atomic<bool> leases_supported_ = true;
  std::mutex igd_mutex_;
  IGDdatas igd_data_ = {};
  UPNPUrls igd_urls_ = {};
  char lan_addr_[64] = {};
  const std::chrono::seconds default_lease_time_ = 1h;
  const std::chrono::minutes refresh_ports_interval_ = 45min;
  std::unique_ptr<xe::threading::PeriodicCallback> refresh_ports_timer_;
  std::mutex mutex_tracked_ports_;
  std::map<std::string, std::set<uint16_t>> tracked_ports_;
  std::mutex mutex_bindings_;
  std::map<std::string, std::map<uint16_t, uint16_t>> port_bindings_;
  std::map<std::string, std::map<uint16_t, int32_t>> port_binding_results_;
  std::mutex mapped_mutex_;
  std::map<uint16_t, uint16_t> mapped_connect_ports_;
  std::map<uint16_t, uint16_t> mapped_bind_ports_;
};

#endif  // XE_PLATFORM_IOS

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UPNP_H_