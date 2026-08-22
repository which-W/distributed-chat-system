#include "ConfigMgr.h"
#include "ConfigSupport.h"
ConfigMgr::ConfigMgr(){
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
		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;
			const std::string& value = key_value_pair.second.get_value<std::string>();
			section_config[key] = value;
		}
		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
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

}

std::string ConfigMgr::GetValue(const std::string& section, const std::string& key) {
	if (_config_map.find(section) == _config_map.end()) {
		return "";
	}

	return _config_map[section].GetValue(key);
}
