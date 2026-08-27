#include "ConfigMgr.h"
#include "ConfigSupport.h"

//config文件的每一项的具体内容
Sectioninfo::Sectioninfo()
{

}

Sectioninfo::~Sectioninfo()
{
    _section_datas.clear();
}

Sectioninfo::Sectioninfo(const Sectioninfo& session)
{
    _section_datas = session._section_datas;
}

Sectioninfo& Sectioninfo::operator=(const Sectioninfo& session)
{
    if (this == &session) {
        return *this;
    }
    this->_section_datas = session._section_datas;
    return *this;
}

std::string Sectioninfo::operator[](const std::string key)
{
    // 边界检查：key 是否为空
    if (key.empty()) {
        std::cerr << "Warning: key is empty." << std::endl;
        return "";
    }

    // 边界检查：key 是否过长（例如超过256字符）
    if (key.length() > 256) {
        std::cerr << "Warning: key is too long." << std::endl;
        return "";
    }


    if (_section_datas.find(key) == _section_datas.end()) {
        return "";
    }

    return _section_datas[key];

}

//读取config文件的每一项
ConfigMgr::~ConfigMgr()
{
    _config_map.clear();
}

ConfigMgr::ConfigMgr()
{
    const auto config_path = chat::config::resolve_file();
    std::cout << "Config path: " << config_path << std::endl;

    // 使用Boost.PropertyTree来读取INI文件
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(config_path.string(), pt);


    // 遍历INI文件中的所有section
    for (const auto& section_pair : pt) {
        const std::string& section_name = section_pair.first;
        const boost::property_tree::ptree& section_tree = section_pair.second;

        // 对于每个section，遍历其所有的key-value对
        std::unordered_map<std::string, std::string> section_config;
        for (const auto& key_value_pair : section_tree) {
            const std::string& key = key_value_pair.first;
            const std::string& value = key_value_pair.second.get_value<std::string>();
            section_config[key] = value;
        }
        Sectioninfo sectionInfo;
        sectionInfo._section_datas = std::move(section_config);
        // 将section的key-value对保存到config_map中
        _config_map[section_name] = sectionInfo;
    }

    const auto override_value = [this](const char* section, const char* key, const char* env_name) {
        if (const char* value = chat::config::env(env_name)) {
            _config_map[section]._section_datas[key] = value;
        }
    };
    override_value("Redis", "Host", "CHAT_REDIS_HOST");
    override_value("Redis", "Port", "CHAT_REDIS_PORT");
    override_value("Redis", "Passwd", "CHAT_REDIS_PASSWORD");
    override_value("Redis", "User", "CHAT_REDIS_USER");
    override_value("Mysql", "Host", "CHAT_MYSQL_HOST");
    override_value("Mysql", "Port", "CHAT_MYSQL_PORT");
    override_value("Mysql", "Passwd", "CHAT_MYSQL_PASSWORD");
    override_value("Mysql", "User", "CHAT_MYSQL_USER");
    override_value("Mysql", "Schema", "CHAT_MYSQL_SCHEMA");
	override_value("InternalRpc", "StatusToken", "CHAT_GATE_STATUS_TOKEN");
	override_value("InternalRpc", "VarifyToken", "CHAT_GATE_VARIFY_TOKEN");

}

Sectioninfo ConfigMgr::operator[](const std::string key)
{
    // 边界检查：key 是否为空
    if (key.empty()) {
        std::cerr << "Warning: key is empty." << std::endl;
        return Sectioninfo();
    }

    // 边界检查：key 是否过长
    if (key.length() > 256) {
        std::cerr << "Warning: key is too long." << std::endl;
        return Sectioninfo();
    }

    if (_config_map.find(key) == _config_map.end()) {
        return Sectioninfo();
    }

    return _config_map[key];
}
