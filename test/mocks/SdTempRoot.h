// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

// Host-test helper: a mounted SdCardManager whose card is a fresh temporary
// host directory (SdCardManager::setTestMountRoot), removed afterwards.
#pragma once

#include <unity.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "modules/SdCardManager.h"

class SdTempRoot {
public:
    SdTempRoot() {
        char tmpl[] = "/tmp/aura_sd_test_XXXXXX";
        TEST_ASSERT_NOT_NULL(mkdtemp(tmpl));
        root_ = tmpl;
    }
    ~SdTempRoot() { removeTree(root_); }

    const std::string &root() const { return root_; }

    // Mounts `sd` on this root through the host-test backend.
    void mount(SdCardManager &sd) const {
        sd.setTestMountRoot(root_.c_str());
        sd.setTestBackend({&ok, &ok, nullptr, nullptr, nullptr});
        TEST_ASSERT_TRUE(sd.begin(nullptr));
        TEST_ASSERT_TRUE(sd.isReady());
    }

    void write(const std::string &relative, const std::string &content) const {
        const std::string full = root_ + relative;
        for (size_t i = root_.size() + 1; i < full.size(); ++i) {
            if (full[i] == '/') mkdir(full.substr(0, i).c_str(), 0775);
        }
        FILE *f = fopen(full.c_str(), "wb");
        TEST_ASSERT_NOT_NULL(f);
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }

    std::string read(const std::string &relative) const {
        std::string out;
        FILE *f = fopen((root_ + relative).c_str(), "rb");
        if (!f) return out;
        char buf[4096];
        size_t got = 0;
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
        fclose(f);
        return out;
    }

    bool exists(const std::string &relative) const {
        struct stat st = {};
        return stat((root_ + relative).c_str(), &st) == 0;
    }

private:
    static bool ok(void *) { return true; }
    static void removeTree(const std::string &path) {
        DIR *dir = opendir(path.c_str());
        if (dir) {
            while (const dirent *e = readdir(dir)) {
                const std::string name = e->d_name;
                if (name == "." || name == "..") continue;
                const std::string child = path + "/" + name;
                struct stat st = {};
                if (stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    removeTree(child);
                } else {
                    unlink(child.c_str());
                }
            }
            closedir(dir);
        }
        rmdir(path.c_str());
    }
    std::string root_;
};
