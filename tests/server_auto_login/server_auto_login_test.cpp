#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <libcockatrice/settings/servers_settings.h>
#include <memory>

// Use the settings class's existing factory friendship in this isolated executable;
// the real client SettingsCache is not linked and no user settings are read.
class SettingsCache
{
public:
    static std::unique_ptr<ServersSettings> create(const QString &path)
    {
        return std::unique_ptr<ServersSettings>(new ServersSettings(path));
    }
};

void require(bool condition, const char *message)
{
    if (!condition) {
        qFatal("%s", message);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "Temporary settings directory is required");
    const QString path = directory.path() + "/";
    const auto writeValue = [&path](const QVariant &value, const QString &name, const QString &group = "server",
                                    const QString &subgroup = QString()) {
        QSettings raw(path + "servers.ini", QSettings::IniFormat);
        raw.setValue(group + "/" + (subgroup.isEmpty() ? QString() : subgroup + "/") + name, value);
        raw.sync();
    };
    auto settings = SettingsCache::create(path);
    require(!settings->getAutoConnect(), "An empty profile must not auto-connect");
    settings->addNewServer("Test", "example.invalid", "4748", "test-user", "test-password", true);
    settings->setPrevioushostName("Test");
    writeValue(0, "autoConnect");
    require(settings->getAutoConnect(), "Legacy default-off profiles with saved credentials must auto-connect");
    require(settings->getPassword() == "test-password", "Reading the preference must not alter stored credentials");
    settings->setAutoConnect(0);
    require(!settings->getAutoConnect(), "Explicit opt-out must be respected");
    settings.reset();
    settings = SettingsCache::create(path);
    require(!settings->getAutoConnect(), "Opt-out must survive restart");
    settings->setAutoConnect(1);
    require(settings->getAutoConnect(), "Explicit opt-in must work");
    settings->setServerPassword("Test", "$scrypt$test-verifier");
    require(settings->getAutoConnect(), "Saved verifiers must qualify without a plaintext password");
    require(settings->getPassword() == "$scrypt$test-verifier", "Verifier must be preserved");

    for (const auto &invalid : {QString(), QString("0"), QString("65536"), QString("invalid")}) {
        writeValue(invalid, "port1", "server", "server_details");
        require(!settings->getAutoConnect(), "Invalid server ports must not auto-connect");
    }
    writeValue("4748", "port1", "server", "server_details");
    writeValue("", "server1", "server", "server_details");
    require(!settings->getAutoConnect(), "Missing hosts must not connect to a fallback server");
    writeValue("example.invalid", "server1", "server", "server_details");
    writeValue(false, "savePassword1", "server", "server_details");
    require(!settings->getAutoConnect(), "Unremembered credentials must not auto-connect");
    writeValue(true, "savePassword1", "server", "server_details");
    writeValue("", "username1", "server", "server_details");
    require(!settings->getAutoConnect(), "Missing usernames must not auto-connect");
    writeValue("test-user", "username1", "server", "server_details");
    settings->setServerPassword("Test", "");
    require(!settings->getAutoConnect(), "Missing passwords must not auto-connect");
    settings->setPrevioushostName("Missing");
    require(!settings->getAutoConnect(), "Deleted or missing profiles must not auto-connect");
    std::puts("PASS: saved login defaults, opt-out persistence, verifiers and incomplete profiles");
    return 0;
}
