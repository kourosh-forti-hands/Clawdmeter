#pragma once
// In-memory stand-in for ESP32 NVS Preferences (brightness.cpp, idle.cpp,
// history_store.cpp). State lasts for the process lifetime only — the sim
// boots with defaults, and a "reboot" of the sim is a fresh process.
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <map>
#include <vector>
#include <cstring>

class Preferences {
public:
    bool begin(const char* name, bool read_only = false) {
        (void)read_only;
        ns_ = name ? name : "";
        return true;
    }
    void end(void) {}
    uint8_t getUChar(const char* key, uint8_t def = 0);
    size_t  putUChar(const char* key, uint8_t value);

    // Blob API — history_store.cpp stores its whole ring as one blob. The
    // firmware relies on getBytesLength() returning 0 for an absent key so it
    // can tell "no history yet" from "history present", so preserve that.
    size_t putBytes(const char* key, const void* value, size_t len) {
        auto& v = blobs()[ns_ + "/" + key];
        v.assign((const uint8_t*)value, (const uint8_t*)value + len);
        return len;
    }
    size_t getBytesLength(const char* key) {
        auto it = blobs().find(ns_ + "/" + key);
        return it == blobs().end() ? 0 : it->second.size();
    }
    size_t getBytes(const char* key, void* buf, size_t max_len) {
        auto it = blobs().find(ns_ + "/" + key);
        if (it == blobs().end()) return 0;
        const size_t n = it->second.size() < max_len ? it->second.size() : max_len;
        memcpy(buf, it->second.data(), n);
        return n;
    }

private:
    std::string ns_;
    // Process-wide so a close/reopen inside one run behaves like real NVS.
    static std::map<std::string, std::vector<uint8_t> >& blobs() {
        static std::map<std::string, std::vector<uint8_t> > s;
        return s;
    }
};
