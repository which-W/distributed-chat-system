#pragma once
#include "global.h"
template<typename T>
class Singleton
{
protected:
	Singleton() = default;
	Singleton(const Singleton<T>&) = delete;
	Singleton & operator=(const Singleton<T>&) = delete;
	static std::shared_ptr<T> _instance;
public:
	static std::shared_ptr<T> Getinstance()
	{
		static std::once_flag _flag;
		std::call_once(_flag, [&]() {
			_instance = std::shared_ptr<T>(new T());
			});

		return _instance;
	}

	void PrintInstanceAddr() {
		std::cout<< "Instance Address: " << _instance.get()<<std::endl;
	}

	~Singleton()
	{
		std::cout << "Singleton Destructor Called" << std::endl;
	}

};

template<typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;
