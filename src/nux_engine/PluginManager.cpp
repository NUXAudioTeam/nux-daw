#include "PluginManager.h"

namespace nux
{
PluginManager::PluginManager()
    : juce::Thread ("NUX plugin scanner")
{
    addDefaultFormatsToManager (formatManager);
}

PluginManager::~PluginManager()
{
    signalThreadShouldExit();
    stopThread (4000);
}

juce::File PluginManager::getCacheFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("NUX DAW")
             .getChildFile ("plugin-cache.xml");
}

juce::FileSearchPath PluginManager::getSearchPaths() const
{
    juce::FileSearchPath paths;

    // Per-user VST3 folders used by every major Windows DAW, plus the system folder.
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto commonAppData = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
    const auto commonFiles = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);

    paths.add (appData.getChildFile ("VST3"));
    paths.add (commonFiles.getChildFile ("VST3"));
    paths.add (commonAppData.getChildFile ("VST3"));

    // On Windows the system VST3 folder is under Program Files.
   #if JUCE_WINDOWS
    paths.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
    paths.add (juce::File ("C:\\Program Files (x86)\\Common Files\\VST3"));
   #endif

    return paths;
}

void PluginManager::loadCachedList()
{
    const auto cache = getCacheFile();

    if (cache.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse (cache))
        {
            knownList.recreateFromXml (*xml);
            rebuildInfoList();
            return;
        }
    }

    // No cache yet: do a first scan in the background.
    startScan();
}

void PluginManager::startScan()
{
    if (scanning.load())
        return;

    scanning.store (true);
    scanProgress.store (0.0f);
    startThread (juce::Thread::Priority::low);

    if (onScanProgress)
        onScanProgress();
}

juce::String PluginManager::getScanStatus() const
{
    if (! scanning.load())
        return juce::String (plugins.size()) + " plugins available";

    const juce::ScopedLock lock (scanFileLock);
    return currentScanFile.isEmpty() ? "Scanning plugins..."
                                     : "Scanning " + juce::File (currentScanFile).getFileName();
}

void PluginManager::rebuildInfoList()
{
    plugins.clear();

    for (const auto& description : knownList.getTypes())
    {
        PluginInfo info;
        info.name         = description.name;
        info.manufacturer = description.manufacturerName;
        info.version      = description.version;
        info.format       = description.pluginFormatName;
        info.isInstrument = description.isInstrument;
        info.description  = description;
        plugins.push_back (std::move (info));
    }

    std::sort (plugins.begin(), plugins.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });
}

void PluginManager::run()
{
    const auto deadMansPedal = getCacheFile().getSiblingFile ("plugin-scan-in-progress.xml");
    deadMansPedal.getParentDirectory().createDirectory();
    deadMansPedal.replaceWithText ("scanning", false, false, "\n");

    juce::Array<juce::AudioPluginFormat*> formatsToScan;

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat (i);

        if (format != nullptr && format->getName().containsIgnoreCase ("VST3"))
            formatsToScan.add (format);
    }

    for (auto* format : formatsToScan)
    {
        if (threadShouldExit())
            break;

        juce::PluginDirectoryScanner scanner (knownList, *format, getSearchPaths(), true,
                                             deadMansPedal, false);

        juce::String pluginBeingScanned;

        while (scanner.scanNextFile (true, pluginBeingScanned))
        {
            if (threadShouldExit())
                break;

            {
                const juce::ScopedLock lock (scanFileLock);
                currentScanFile = pluginBeingScanned;
            }

            scanProgress.store (scanner.getProgress());

            if (onScanProgress)
                onScanProgress();
        }

        if (threadShouldExit())
            break;
    }

    deadMansPedal.deleteFile();

    rebuildInfoList();
    saveCache();

    scanProgress.store (1.0f);
    scanning.store (false);

    {
        const juce::ScopedLock lock (scanFileLock);
        currentScanFile.clear();
    }

    if (onScanFinished)
        onScanFinished();

    if (onScanProgress)
        onScanProgress();
}

void PluginManager::saveCache()
{
    const auto cache = getCacheFile();
    cache.getParentDirectory().createDirectory();

    if (auto xml = knownList.createXml())
        xml->writeTo (cache);
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createInstance (const juce::PluginDescription& description,
                                                                         double sampleRate, int blockSize,
                                                                         juce::String& error)
{
    error.clear();

    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, error);

    if (instance == nullptr && error.isEmpty())
        error = "The plugin could not be loaded.";

    return instance;
}
}
