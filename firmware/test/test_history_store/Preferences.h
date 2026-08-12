#pragma once
// Host-only stand-in for ESP32's <Preferences.h>, used exclusively by this
// test. Backs each (namespace, key) pair with an in-memory byte blob so
// history_store.cpp's begin()/putBytes()/getBytes()/getBytesLength()/end()
// calls all work unmodified.
//
// The backing store is a function-local static, i.e. it survives across
// separate `Preferences` instances and separate begin()/end() pairs for the
// life of the test process -- exactly like real NVS content survives a
// reboot while RAM does not. That's what lets the test simulate "reboot" by
// simply calling history_init() again: it resets history_store.cpp's RAM
// ring but the fake flash underneath is untouched, so the reload path is
// exercised for real rather than mocked.
//
// write_count() lets the test assert on flush *frequency* -- the entire
// point of history_store's rate-limiting -- without history_store.cpp having
// to expose any test-only hook. wipe_storage() resets to a factory-blank
// device between independent test cases.
//
// This file lives only under firmware/test/ -- PlatformIO's build_src_filter
// never looks there, so real board builds never see it.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class Preferences {
public:
    bool begin(const char* name, bool read_only = false) {
        ns_ = name ? name : "";
        read_only_ = read_only;
        return true;
    }

    void end() {}

    size_t putBytes(const char* key, const void* value, size_t len) {
        if (read_only_) return 0;
        const uint8_t* p = static_cast<const uint8_t*>(value);
        storage()[full_key(key)] = std::vector<uint8_t>(p, p + len);
        write_count()++;
        return len;
    }

    size_t getBytesLength(const char* key) {
        auto it = storage().find(full_key(key));
        return it == storage().end() ? 0 : it->second.size();
    }

    size_t getBytes(const char* key, void* buf, size_t max_len) {
        auto it = storage().find(full_key(key));
        if (it == storage().end()) return 0;
        size_t n = it->second.size();
        if (n > max_len) n = max_len;
        memcpy(buf, it->second.data(), n);
        return n;
    }

    // ---- Test-only inspection/reset hooks (not part of the real API) ----
    static size_t& write_count() {
        static size_t n = 0;
        return n;
    }
    static void wipe_storage() { storage().clear(); }

private:
    std::string ns_;
    bool read_only_ = false;

    std::string full_key(const char* key) const {
        return ns_ + "/" + (key ? key : "");
    }

    static std::map<std::string, std::vector<uint8_t>>& storage() {
        static std::map<std::string, std::vector<uint8_t>> s;
        return s;
    }
};
