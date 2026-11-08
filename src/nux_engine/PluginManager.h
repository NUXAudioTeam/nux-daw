/* NUX DAW - VST3 discovery and hosting.

   Uses the VST3 SDK bundled with JUCE 9 (no external download required).
   Scanning happens on a background thread through juce::PluginDirectoryScanner
   and the resulting list is cached in the user application data folder so that
   startup never blocks on a plugin scan.

   Plugin *audio* hosting is wired through PluginHostNode; plugin editors are
   created on demand by the Plugin panel. */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

namespace nux
{
class PluginManager final : private juce::Thread
{
public:
    PluginManager();
    ~PluginManager() override;

    struct PluginInfo
    {
        juce::String name;
        juce::String manufacturer;
        juce::String version;
        juce::String format;
        bool isInstrument = false;
        juce::PluginDescription description;
    };

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownList() noexcept { return knownList; }

    /** Returns the cached plugin list, without scanning. */
    const std::vector<PluginInfo>& getPlugins() const noexcept { return plugins; }

    /** The folders that are scanned for VST3 bundles. */
    juce::FileSearchPath getSearchPaths() const;

    /** Starts an asynchronous scan (no-op if one is already running). */
    void startScan();
    bool isScanning() const noexcept { return scanning.load(); }
    float getScanProgress() const noexcept { return scanProgress.load(); }
    juce::String getScanStatus() const;

    /** Loads the cached list, or starts a first scan if the cache is missing. */
    void loadCachedList();

    /** Scans the given folders right now and blocks until the scan finishes.
        Used by the tests and by the 'rescan' button, which wants a completion point.
        Returns the number of plugins found. */
    int scanNow (const juce::FileSearchPath& folders, juce::StringArray& errors);

    /** Adds folders to the scan list and starts a background scan. */
    void addSearchPaths (const juce::FileSearchPath& folders);

    /** The folders this manager will scan (user folders plus anything added). */
    const juce::FileSearchPath& getAdditionalSearchPaths() const noexcept { return extraSearchPaths; }

    /** Builds a live instance for a plugin, on the message thread. */
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription& description,
                                                              double sampleRate, int blockSize,
                                                              juce::String& error);

    std::function<void()> onScanFinished;
    std::function<void()> onScanProgress;

private:
    void run() override;
    juce::File getCacheFile() const;
    void saveCache();
    void rebuildInfoList();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
    juce::FileSearchPath extraSearchPaths;
    std::vector<PluginInfo> plugins;

    std::atomic<bool> scanning { false };
    std::atomic<float> scanProgress { 0.0f };
    juce::String currentScanFile;
    juce::CriticalSection scanFileLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};
}
