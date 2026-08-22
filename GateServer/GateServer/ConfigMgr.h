#pragma once
#include "const.h"
struct Sectioninfo
{
	Sectioninfo();
	~Sectioninfo();
	Sectioninfo(const Sectioninfo& session);
	Sectioninfo& operator = (const Sectioninfo& session);
	std::unordered_map<std::string, std::string> _section_datas;
	std::string operator[](const std::string key);

};

class ConfigMgr
{
public:
	~ConfigMgr();
	Sectioninfo operator[](const std::string key);
	ConfigMgr& operator=(const ConfigMgr& src) = delete;
	ConfigMgr(const ConfigMgr& src) = delete;
	static ConfigMgr& ins() {
		static ConfigMgr configMgr;
		return configMgr;
	}

private:
	ConfigMgr();
	std::unordered_map<std::string, Sectioninfo> _config_map;
};
